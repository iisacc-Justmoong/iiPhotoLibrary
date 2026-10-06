#include <iiPhotoLibrary/Album.h>
#include <iiPhotoLibrary/GooglePhotosAlbumSource.h>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>

using namespace iiPhotoLibrary;
namespace {
int checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error( \
    std::string(#__VA_ARGS__) + " at " + std::to_string(__LINE__)); } while (false)
template<class F> void fails(AlbumErrorCode code, F call) {
    try { call(); } catch (const AlbumError &e) { CHECK(e.code() == code); return; }
    throw std::runtime_error("expected album error");
}
AlbumPhoto photo(std::string id = "same", std::string name = "photo.jpg") {
    AlbumPhoto result; result.sourcePhotoId = std::move(id); result.name = std::move(name); return result;
}
class Source : public AlbumSource {
  public:
    std::string provider;
    AlbumCoverage scope;
    AlbumSnapshot result{{photo()}, true, "one"};
    std::optional<AlbumErrorCode> error;
    Source(std::string id, AlbumCoverage coverage) : provider(std::move(id)), scope(coverage) {}
    std::string providerId() const override { return provider; }
    AlbumCoverage coverage() const override { return scope; }
    AlbumSnapshot read(std::stop_token stop) override {
        if (stop.stop_requested()) throw AlbumError(AlbumErrorCode::Cancelled, "cancelled fixture");
        if (error) throw AlbumError(*error, "fixture error");
        return result;
    }
};
void manager() {
    AlbumManager manager;
    auto local = manager.createLocal("local", "Local"), secondLocal = manager.createLocal("second", "Local");
    manager.setLocalPhotos(local, {photo()}); manager.setLocalPhotos(secondLocal, {photo()});
    CHECK(local != secondLocal && local->id() != secondLocal->id());
    CHECK(local->snapshot().contents.photos[0].id != secondLocal->snapshot().contents.photos[0].id);
    CHECK(local->snapshot().coverage == AlbumCoverage::Local);
    auto apple = std::make_shared<Source>("icloud", AlbumCoverage::SystemPhotoLibrary);
    auto google = std::make_shared<Source>("googlephotos", AlbumCoverage::SelectedItems);
    auto icloud = manager.connect({"icloud", "apple-account", "iCloud Photos", "library", AlbumOrigin::ICloud}, apple);
    auto photos = manager.connect({"googlephotos", "google-account", "Google Photos", "library", AlbumOrigin::GooglePhotos}, google);
    CHECK(icloud != photos && icloud->snapshot().state == AlbumState::Connected);
    manager.refresh(icloud); manager.refresh(photos);
    CHECK(manager.album(icloud->id()) == icloud && manager.albums().size() == 4);
    CHECK(icloud->snapshot().contents.photos[0].id != photos->snapshot().contents.photos[0].id);
    CHECK(photos->snapshot().coverage == AlbumCoverage::SelectedItems);
    const auto first = icloud->snapshot().contents.photos[0].id;
    apple->result = {{photo("same", "changed.jpg"), photo("new")}, false, "partial"};
    manager.refresh(icloud);
    CHECK(manager.album(icloud->id()) == icloud);
    CHECK(icloud->snapshot().contents.photos.size() == 2 && !icloud->snapshot().contents.complete);
    CHECK(icloud->snapshot().contents.photos.back().id == first);
    apple->result = {{photo("same", "newer.jpg")}, false, "partial-2"};
    manager.refresh(icloud);
    CHECK(icloud->snapshot().contents.photos.size() == 2);
    auto before = icloud->snapshot().contents;
    apple->error = AlbumErrorCode::Transport;
    fails(*apple->error, [&] { manager.refresh(icloud); });
    CHECK(icloud->snapshot().contents.photos == before.photos && icloud->snapshot().state == AlbumState::Failed);
    apple->error.reset();
    apple->result = {{photo("x"), photo("x")}, true, "invalid"};
    fails(AlbumErrorCode::InvalidSnapshot, [&] { manager.refresh(icloud); });
    CHECK(icloud->snapshot().contents.photos == before.photos);
    apple->result = {{}, true, "empty"}; manager.refresh(icloud);
    CHECK(icloud->snapshot().contents.photos.empty());
    google->error = AlbumErrorCode::PermissionDenied;
    fails(*google->error, [&] { manager.refresh(photos); });
    CHECK(photos->snapshot().contents.photos.empty() && photos->snapshot().state == AlbumState::AuthenticationRequired);
    CHECK(local->snapshot().contents.photos.size() == 1);
    auto otherAccount = manager.connect({"googlephotos", "second-google-account", "Google Photos", "library", AlbumOrigin::GooglePhotos});
    manager.publish(otherAccount, {{photo()}, true, "second-account"});
    CHECK(otherAccount != photos && otherAccount->id() != photos->id());
    CHECK(otherAccount->snapshot().contents.photos.size() == 1 && photos->snapshot().contents.photos.empty());
    auto colonOne = manager.connect({"remote:a", "b", "Same title", "c"});
    auto colonTwo = manager.connect({"remote", "a:b", "Same title", "c"});
    CHECK(colonOne->id() != colonTwo->id());
    fails(AlbumErrorCode::ReadOnly, [&] { manager.setLocalPhotos(icloud, {}); });
    auto received = manager.connect({"remote", "peer", "Received", "album-1"});
    manager.publish(received, {{photo()}, true, "received"});
    auto another = manager.connect({"remote", "peer", "Received", "album-2"});
    manager.publish(another, {{photo()}, true, "received"});
    CHECK(received != another && received->snapshot().contents.photos[0].id != another->snapshot().contents.photos[0].id);
    fails(AlbumErrorCode::Unsupported, [&] { manager.refresh(received); });
    AlbumManager foreign;
    fails(AlbumErrorCode::StaleConnection, [&] { foreign.publish(received, {}); });
    CHECK(manager.disconnect(received) && !manager.disconnect(received));
    CHECK(received->snapshot().state == AlbumState::Disconnected && received->snapshot().contents.photos.empty());
    fails(AlbumErrorCode::StaleConnection, [&] { manager.publish(received, {}); });
    auto reconnected = manager.connect({"icloud", "apple-account", "iCloud Photos", "library", AlbumOrigin::ICloud}, apple);
    CHECK(reconnected != icloud && reconnected->id() == icloud->id());
    CHECK(icloud->snapshot().state == AlbumState::Disconnected);
    CHECK(!manager.disconnect(icloud) && manager.album(reconnected->id()) == reconnected);
    AlbumManager limited({1, 1});
    auto only = limited.connect({"remote", "account", "Limited"});
    limited.publish(only, {{photo()}, true, "first"});
    fails(AlbumErrorCode::LimitExceeded, [&] { limited.publish(only, {{photo("new")}, false, "overflow"}); });
    CHECK(only->snapshot().contents.photos[0].sourcePhotoId == "same");
    fails(AlbumErrorCode::LimitExceeded, [&] { limited.createLocal("another", "Another"); });
    limited.publish(only, {{photo("same", "updated")}, false, "same"});
    CHECK(only->snapshot().contents.photos[0].name == "updated");
    std::shared_ptr<Album> surviving;
    { AlbumManager transient; surviving = transient.createLocal("survive", "Survive"); transient.setLocalPhotos(surviving, {photo()}); }
    CHECK(surviving->snapshot().state == AlbumState::Disconnected && surviving->snapshot().contents.photos.empty());
    class Blocking final : public Source {
      public:
        Blocking() : Source("remote", AlbumCoverage::External) {}
        std::mutex mutex; std::condition_variable cv; bool entered = false, release = false; std::stop_token token;
        AlbumSnapshot read(std::stop_token stop) override {
            std::unique_lock lock(mutex); token = stop; entered = true; cv.notify_all();
            cv.wait(lock, [&] { return release; }); return result;
        }
    };
    auto slow = std::make_shared<Blocking>(); auto handle = manager.connect({"remote", "slow", "Slow"}, slow);
    auto pending = std::async(std::launch::async, [&] {
        try { manager.refresh(handle); } catch (const AlbumError &e) { return e.code(); }
        return AlbumErrorCode::Unsupported;
    });
    { std::unique_lock lock(slow->mutex); slow->cv.wait(lock, [&] { return slow->entered; }); }
    fails(AlbumErrorCode::Busy, [&] { manager.publish(handle, {}); });
    auto newHandle = manager.connect({"remote", "slow", "Slow"});
    CHECK(slow->token.stop_requested());
    { std::lock_guard lock(slow->mutex); slow->release = true; slow->cv.notify_all(); }
    CHECK(pending.get() == AlbumErrorCode::StaleConnection);
    CHECK(handle->snapshot().state == AlbumState::Disconnected && newHandle->snapshot().contents.photos.empty());
}
void google() {
    std::vector<AlbumHttpRequest> requests;
    std::vector<AlbumHttpResponse> responses; std::size_t cursor = 0, tokens = 0;
    auto get = [&](const AlbumHttpRequest &request) { requests.push_back(request); return responses.at(cursor++); };
    GooglePhotosAlbumSource source([&] { ++tokens; return "fixture-token"; }, get, [] { return "session/+"; });
    responses = {{200, R"({"id":"session/+","mediaItemsSet":true})"},
        {200, R"({"mediaItems":[{"id":"same","type":"PHOTO","createTime":"2026-10-02T00:00:00Z","mediaFile":{"filename":"photo.jpg","mimeType":"image/jpeg","baseUrl":"https://lh3.googleusercontent.com/fixture","mediaFileMetadata":{"width":640,"height":480}}}],"nextPageToken":"next/+"})"},
        {200, R"({"mediaItems":[{"id":"video","type":"VIDEO","mediaFile":{"filename":"video.mp4","mimeType":"video/mp4","baseUrl":"https://lh3.googleusercontent.com/video"}}]})"}};
    auto result = source.read({});
    CHECK(result.photos.size() == 2 && !result.complete);
    CHECK(source.coverage() == AlbumCoverage::SelectedItems);
    CHECK(result.photos[0].sourcePhotoId == "same" && result.photos[0].name == "photo.jpg");
    CHECK(result.photos[0].width == 640 && result.photos[0].height == 480);
    CHECK(result.photos[1].mediaType == "video");
    CHECK(tokens == 3 && requests[0].headers.at("Authorization") == "Bearer fixture-token");
    CHECK(requests[0].url.find("sessions/session%2F%2B") != std::string::npos);
    CHECK(requests[2].url.find("pageToken=next%2F%2B") != std::string::npos);
    CHECK(requests[1].url.find("sessionId=session%2F%2B") != std::string::npos);
    auto failure = [&](AlbumHttpResponse response, AlbumErrorCode code) {
        cursor = 0; responses = {std::move(response)}; fails(code, [&] { source.read({}); });
    };
    failure({200, R"({"id":"session/+","mediaItemsSet":false})"}, AlbumErrorCode::SelectionRequired);
    failure({200, R"({"id":"wrong","mediaItemsSet":true})"}, AlbumErrorCode::InvalidSnapshot);
    failure({401, "secret in upstream response"}, AlbumErrorCode::AuthenticationRequired);
    failure({403, "denied"}, AlbumErrorCode::PermissionDenied);
    failure({429, "quota"}, AlbumErrorCode::RateLimited);
    failure({302, "redirect"}, AlbumErrorCode::Transport);
    failure({200, "invalid JSON"}, AlbumErrorCode::InvalidSnapshot);
    failure({200, std::string(16 * 1024 * 1024 + 1, 'x')}, AlbumErrorCode::LimitExceeded);
    GooglePhotosAlbumSource appCreated([] { return "token"; }, get, {}, {GooglePhotosMode::AppCreatedItems, 2, 2});
    cursor = 0; requests.clear();
    responses = {{200, R"({"mediaItems":[{"id":"same","filename":"created.jpg","mimeType":"image/jpeg","baseUrl":"https://lh3.googleusercontent.com/created","mediaMetadata":{"width":"800","height":"600","creationTime":"2026-10-02T00:00:00Z","photo":{}}}]})"}};
    result = appCreated.read({});
    CHECK(result.complete && appCreated.coverage() == AlbumCoverage::AppCreatedItems);
    CHECK(result.photos[0].width == 800 && result.photos[0].created == "2026-10-02T00:00:00Z");
    CHECK(requests[0].url.starts_with("https://photoslibrary.googleapis.com/v1/mediaItems"));
    cursor = 0; responses = {{200, R"({"mediaItems":[],"nextPageToken":"repeat"})"}, {200, R"({"mediaItems":[],"nextPageToken":"repeat"})"}};
    fails(AlbumErrorCode::InvalidSnapshot, [&] { appCreated.read({}); });
    cursor = 0; responses = {{200, R"({"mediaItems":[{"id":"x"},{"id":"x"}]})"}};
    fails(AlbumErrorCode::InvalidSnapshot, [&] { appCreated.read({}); });
    std::stop_source cancelled; cancelled.request_stop();
    const auto calls = requests.size();
    fails(AlbumErrorCode::Cancelled, [&] { appCreated.read(cancelled.get_token()); });
    CHECK(requests.size() == calls);
    GooglePhotosAlbumSource invalid([] { return "token\r\nInjected: value"; }, get, [] { return "session"; });
    fails(AlbumErrorCode::AuthenticationRequired, [&] { invalid.read({}); });
    GooglePhotosAlbumSource missing([] { return "token"; }, get, [] { return std::string{}; });
    fails(AlbumErrorCode::SelectionRequired, [&] { missing.read({}); });
    AlbumManager manager;
    cursor = 0; responses = {{200, R"({"mediaItems":[{"id":"first","filename":"created.jpg","mimeType":"image/jpeg"}]})"}};
    auto connected = manager.connect({"googlephotos", "one", "Google Photos", "library", AlbumOrigin::GooglePhotos},
        std::make_shared<GooglePhotosAlbumSource>([] { return "token"; }, get, PhotosPickerSession{}, GooglePhotosAlbumOptions{GooglePhotosMode::AppCreatedItems, 2, 2}));
    manager.refresh(connected);
    cursor = 0; responses = {{429, "quota"}};
    fails(AlbumErrorCode::RateLimited, [&] { manager.refresh(connected); });
    CHECK(connected->snapshot().contents.photos.size() == 1);
    cursor = 0; responses = {{401, "private"}};
    fails(AlbumErrorCode::AuthenticationRequired, [&] { manager.refresh(connected); });
    CHECK(connected->snapshot().contents.photos.empty());
    auto invalidPage = [&](const std::string &body, AlbumErrorCode code = AlbumErrorCode::InvalidSnapshot) {
        cursor = 0; responses = {{200, body}};
        fails(code, [&] { appCreated.read({}); });
    };
    invalidPage(R"({"mediaItems":[{"id":"x","mediaMetadata":{"width":"-1"}}]})");
    invalidPage(R"({"mediaItems":[{"id":"x","baseUrl":"http://example.com/photo"}]})");
    invalidPage(R"({"mediaItems":{}})");
    invalidPage(R"({"mediaItems":[{"filename":"missing-id"}]})");
    invalidPage(R"({"mediaItems":[{"id":"1"},{"id":"2"},{"id":"3"}]})", AlbumErrorCode::LimitExceeded);
    invalidPage("{\"nested\":" + std::string(34, '[') + "0" + std::string(34, ']') + "}", AlbumErrorCode::LimitExceeded);
    cursor = 0; responses = {{200, R"({"nextPageToken":"first"})"}, {200, R"({"nextPageToken":"second"})"}};
    fails(AlbumErrorCode::LimitExceeded, [&] { appCreated.read({}); });
    GooglePhotosAlbumSource transportError([] { return "token"; }, [](const AlbumHttpRequest &) -> AlbumHttpResponse {
        throw std::runtime_error("private token detail");
    }, {}, {GooglePhotosMode::AppCreatedItems});
    bool rejected = false;
    try { transportError.read({}); }
    catch (const AlbumError &e) {
        rejected = true;
        CHECK(e.code() == AlbumErrorCode::Transport && std::string(e.what()).find("private") == std::string::npos);
    }
    CHECK(rejected);
}
}
int main(int argc, char **argv) {
    try {
        const std::string suite = argc > 1 ? argv[1] : "";
        if (suite == "manager") manager(); else if (suite == "google") google();
        else throw std::runtime_error("unknown suite");
        std::cout << suite << ": " << checks << " checks passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
