#include <iiPhotoLibrary/GooglePhotosAlbumSource.h>
#include <nlohmann/json.hpp>
#include <charconv>
#include <set>

namespace iiPhotoLibrary {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maximumBody = 16 * 1024 * 1024;
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw AlbumError(AlbumErrorCode::Cancelled, "Google Photos album refresh cancelled");
}
[[noreturn]] void invalid() { throw AlbumError(AlbumErrorCode::InvalidSnapshot, "invalid Google Photos album response"); }
std::string encode(const std::string &value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
Json decode(const std::string &body) {
    try {
        auto parsed = Json::parse(body, [](int depth, Json::parse_event_t, Json &) {
            if (depth > 32) throw AlbumError(AlbumErrorCode::LimitExceeded, "Google Photos response nesting limit exceeded");
            return true;
        });
        if (!parsed.is_object()) invalid();
        return parsed;
    } catch (const Json::exception &) { invalid(); }
}
std::string string(const Json &object, const char *key, bool required = false) {
    if (!object.is_object()) invalid();
    const auto found = object.find(key);
    if (found == object.end()) { if (required) invalid(); return {}; }
    if (!found->is_string()) invalid();
    auto value = found->get<std::string>();
    if (value.size() > 65536) throw AlbumError(AlbumErrorCode::LimitExceeded, "Google Photos metadata text limit exceeded");
    if (required && value.empty()) invalid();
    return value;
}
std::uint32_t dimension(const Json &object, const char *key) {
    if (!object.is_object()) invalid();
    const auto found = object.find(key);
    if (found == object.end()) return 0;
    if (found->is_number_unsigned()) {
        const auto value = found->get<std::uint64_t>();
        if (value > UINT32_MAX) invalid();
        return static_cast<std::uint32_t>(value);
    }
    if (found->is_number_integer()) {
        const auto value = found->get<std::int64_t>();
        if (value < 0 || value > UINT32_MAX) invalid();
        return static_cast<std::uint32_t>(value);
    }
    if (!found->is_string()) invalid();
    const auto value = found->get<std::string>();
    std::uint32_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) invalid();
    return result;
}
AlbumPhoto item(const Json &value, GooglePhotosMode mode) {
    AlbumPhoto photo;
    photo.sourcePhotoId = string(value, "id", true);
    if (photo.sourcePhotoId.size() > 4096) invalid();
    if (mode == GooglePhotosMode::PickerSelection) {
        const auto type = string(value, "type", true);
        if (type != "PHOTO" && type != "VIDEO") invalid();
        photo.mediaType = type == "VIDEO" ? "video" : "photo";
        photo.created = string(value, "createTime");
        const auto file = value.find("mediaFile");
        if (file == value.end() || !file->is_object()) invalid();
        photo.name = string(*file, "filename"); photo.mimeType = string(*file, "mimeType");
        photo.contentUrl = string(*file, "baseUrl");
        if (const auto metadata = file->find("mediaFileMetadata"); metadata != file->end()) {
            photo.width = dimension(*metadata, "width"); photo.height = dimension(*metadata, "height");
        }
    } else {
        photo.name = string(value, "filename"); photo.mimeType = string(value, "mimeType");
        photo.contentUrl = string(value, "baseUrl"); photo.productUrl = string(value, "productUrl");
        photo.mediaType = photo.mimeType.starts_with("video/") ? "video" : "photo";
        if (const auto metadata = value.find("mediaMetadata"); metadata != value.end()) {
            photo.width = dimension(*metadata, "width"); photo.height = dimension(*metadata, "height");
            photo.created = string(*metadata, "creationTime");
            if (metadata->contains("video")) photo.mediaType = "video";
        }
    }
    for (const auto *url : {&photo.contentUrl, &photo.productUrl})
        if (!url->empty() && !url->starts_with("https://")) invalid();
    return photo;
}
}
GooglePhotosAlbumSource::GooglePhotosAlbumSource(PhotosAccessToken token, AlbumHttpGet get,
    PhotosPickerSession pickerSession, GooglePhotosAlbumOptions options)
    : token_(std::move(token)), get_(std::move(get)), session_(std::move(pickerSession)), options_(options) {
    if (!token_ || !get_ || !options_.maximumPages || !options_.maximumPhotos ||
        (options_.mode != GooglePhotosMode::PickerSelection && options_.mode != GooglePhotosMode::AppCreatedItems))
        throw std::invalid_argument("Google Photos token, HTTPS transport and positive limits required");
}
AlbumCoverage GooglePhotosAlbumSource::coverage() const {
    return options_.mode == GooglePhotosMode::PickerSelection ? AlbumCoverage::SelectedItems : AlbumCoverage::AppCreatedItems;
}
AlbumHttpResponse GooglePhotosAlbumSource::request(const std::string &url, std::stop_token stop) {
    cancelled(stop);
    std::string token;
    try { token = token_(); }
    catch (...) { throw AlbumError(AlbumErrorCode::AuthenticationRequired, "Google Photos access token unavailable"); }
    if (token.empty() || token.size() > 16384) throw AlbumError(AlbumErrorCode::AuthenticationRequired, "invalid Google Photos access token");
    for (unsigned char c : token)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.' || c == '~' || c == '+' || c == '/' || c == '='))
            throw AlbumError(AlbumErrorCode::AuthenticationRequired, "invalid Google Photos access token");
    AlbumHttpResponse response;
    cancelled(stop);
    try { response = get_({url, {{"Authorization", "Bearer " + token}, {"Accept", "application/json"}}, stop}); }
    catch (...) {
        cancelled(stop);
        throw AlbumError(AlbumErrorCode::Transport, "Google Photos HTTPS request failed");
    }
    cancelled(stop);
    if (response.status == 401) throw AlbumError(AlbumErrorCode::AuthenticationRequired, "Google Photos authentication required");
    if (response.status == 403) throw AlbumError(AlbumErrorCode::PermissionDenied, "Google Photos access denied");
    if (response.status == 429) throw AlbumError(AlbumErrorCode::RateLimited, "Google Photos rate limit exceeded");
    if (response.status == 404) throw AlbumError(AlbumErrorCode::NotFound, "Google Photos selection or resource unavailable");
    if (response.status != 200) throw AlbumError(AlbumErrorCode::Transport, "Google Photos HTTP request unsuccessful");
    if (response.body.size() > maximumBody) throw AlbumError(AlbumErrorCode::LimitExceeded, "Google Photos response body limit exceeded");
    return response;
}
AlbumSnapshot GooglePhotosAlbumSource::read(std::stop_token stop) {
    cancelled(stop);
    const bool picker = options_.mode == GooglePhotosMode::PickerSelection;
    std::string session;
    if (picker) {
        try { if (session_) session = session_(); }
        catch (...) { throw AlbumError(AlbumErrorCode::SelectionRequired, "Google Photos Picker session unavailable"); }
        if (session.empty()) throw AlbumError(AlbumErrorCode::SelectionRequired, "Google Photos Picker selection required");
        if (session.size() > 4096) throw AlbumError(AlbumErrorCode::LimitExceeded, "Google Photos session identity limit exceeded");
        const auto selected = decode(request("https://photospicker.googleapis.com/v1/sessions/" + encode(session), stop).body);
        if (string(selected, "id", true) != session) invalid();
        const auto ready = selected.find("mediaItemsSet");
        if (ready != selected.end() && !ready->is_boolean()) invalid();
        if (ready == selected.end() || !ready->get<bool>())
            throw AlbumError(AlbumErrorCode::SelectionRequired, "Google Photos Picker selection is not complete");
    }
    AlbumSnapshot result;
    result.complete = !picker; result.revision = session;
    std::set<std::string> ids, pageTokens;
    std::string pageToken;
    for (std::size_t page = 0; page < options_.maximumPages; ++page) {
        std::string url = picker ? "https://photospicker.googleapis.com/v1/mediaItems?sessionId=" + encode(session) + "&pageSize=100"
                                 : "https://photoslibrary.googleapis.com/v1/mediaItems?pageSize=100";
        if (!pageToken.empty()) url += "&pageToken=" + encode(pageToken);
        const auto body = decode(request(url, stop).body);
        if (const auto photos = body.find("mediaItems"); photos != body.end()) {
            if (!photos->is_array()) invalid();
            for (const auto &value : *photos) {
                cancelled(stop);
                if (result.photos.size() >= options_.maximumPhotos)
                    throw AlbumError(AlbumErrorCode::LimitExceeded, "Google Photos album photo count limit exceeded");
                auto photo = item(value, options_.mode);
                if (!ids.insert(photo.sourcePhotoId).second) invalid();
                result.photos.push_back(std::move(photo));
            }
        }
        pageToken = string(body, "nextPageToken");
        if (pageToken.empty()) return result;
        if (!pageTokens.insert(pageToken).second) invalid();
    }
    throw AlbumError(AlbumErrorCode::LimitExceeded, "Google Photos page count limit exceeded");
}
}
