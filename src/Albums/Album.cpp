#include <iiPhotoLibrary/Album.h>
#include <mutex>
#include <set>

namespace iiPhotoLibrary {
AlbumError::AlbumError(AlbumErrorCode code, const std::string &message) : std::runtime_error(message), code_(code) {}
namespace {
std::string component(const std::string &value) { return std::to_string(value.size()) + ":" + value; }
std::string albumId(const AlbumConnection &connection) {
    return "album:" + component(connection.providerId) + component(connection.accountId) + component(connection.remoteAlbumId);
}
void textSize(const std::string &text, std::size_t maximum) {
    if (text.size() > maximum)
        throw AlbumError(AlbumErrorCode::LimitExceeded, "album metadata text limit exceeded");
}
void validate(const AlbumConnection &connection) {
    if (connection.providerId.empty() || connection.remoteAlbumId.empty() ||
        (connection.origin != AlbumOrigin::Local && connection.accountId.empty()))
        throw std::invalid_argument("album provider, account and remote album identity required");
    for (const auto *value : {&connection.providerId, &connection.accountId, &connection.remoteAlbumId}) textSize(*value, 4096);
    textSize(connection.title, 65536);
    if ((connection.origin == AlbumOrigin::ICloud && connection.providerId != "icloud") ||
        (connection.origin == AlbumOrigin::GooglePhotos && connection.providerId != "googlephotos"))
        throw std::invalid_argument("album origin does not match provider identity");
}
void validatePhoto(const AlbumPhoto &photo) {
    if (photo.sourcePhotoId.empty() || (photo.mediaType != "photo" && photo.mediaType != "video"))
        throw AlbumError(AlbumErrorCode::InvalidSnapshot, "photo identity and media type required");
    textSize(photo.sourcePhotoId, 4096);
    for (const auto *value : {&photo.name, &photo.mimeType, &photo.created, &photo.contentUrl, &photo.productUrl}) textSize(*value, 65536);
    if (photo.properties.size() > 256)
        throw AlbumError(AlbumErrorCode::LimitExceeded, "photo metadata property limit exceeded");
    for (const auto &[key, value] : photo.properties) { textSize(key, 4096); textSize(value, 65536); }
}
}
struct Album::Impl {
    mutable std::mutex mutex;
    AlbumData data;
    explicit Impl(AlbumData value) : data(std::move(value)) {}
};
Album::Album(AlbumData data) : impl_(std::make_unique<Impl>(std::move(data))) {}
Album::~Album() = default;
const std::string &Album::id() const noexcept { return impl_->data.id; }
AlbumData Album::snapshot() const { std::lock_guard lock(impl_->mutex); return impl_->data; }
struct AlbumManager::Impl {
    struct Record {
        std::shared_ptr<Album> album;
        std::shared_ptr<AlbumSource> source;
        std::stop_source stop;
        bool busy = false;
    };
    mutable std::mutex mutex;
    std::map<std::string, std::shared_ptr<Record>> records;
    AlbumLimits limits;
    explicit Impl(AlbumLimits value) : limits(value) {}
    bool current(const std::shared_ptr<Record> &record) const {
        const auto found = records.find(record->album->id());
        return found != records.end() && found->second == record;
    }
    std::shared_ptr<Record> checked(const std::shared_ptr<Album> &album) const {
        if (album) {
            const auto found = records.find(album->id());
            if (found != records.end() && found->second->album == album) return found->second;
        }
        throw AlbumError(AlbumErrorCode::StaleConnection, "album is not owned by this active connection");
    }
    static void retire(const std::shared_ptr<Record> &record) {
        std::lock_guard lock(record->album->impl_->mutex);
        auto &data = record->album->impl_->data;
        data.contents = {};
        data.state = AlbumState::Disconnected;
        data.error.reset();
    }
    void apply(const std::shared_ptr<Record> &record, AlbumSnapshot input) {
        if (input.photos.size() > limits.maximumPhotos)
            throw AlbumError(AlbumErrorCode::LimitExceeded, "album photo count limit exceeded");
        textSize(input.revision, 65536);
        std::set<std::string> ids;
        for (auto &photo : input.photos) {
            validatePhoto(photo);
            if (!ids.insert(photo.sourcePhotoId).second)
                throw AlbumError(AlbumErrorCode::InvalidSnapshot, "duplicate photo identity in album snapshot");
            photo.id = record->album->id() + component(photo.sourcePhotoId);
        }
        std::lock_guard lock(record->album->impl_->mutex);
        auto &data = record->album->impl_->data;
        std::map<std::string, AlbumPhoto> merged;
        if (!input.complete)
            for (const auto &photo : data.contents.photos) merged.emplace(photo.sourcePhotoId, photo);
        for (auto &photo : input.photos) merged.insert_or_assign(photo.sourcePhotoId, std::move(photo));
        if (merged.size() > limits.maximumPhotos)
            throw AlbumError(AlbumErrorCode::LimitExceeded, "accumulated album photo count limit exceeded");
        AlbumSnapshot next;
        next.complete = input.complete;
        next.revision = std::move(input.revision);
        next.photos.reserve(merged.size());
        for (auto &[id, photo] : merged) { (void)id; next.photos.push_back(std::move(photo)); }
        // Validation and allocation completed before replacing the observable state.
        data.contents = std::move(next);
        data.state = AlbumState::Ready;
        data.error.reset();
    }
    void failed(const std::shared_ptr<Record> &record, AlbumErrorCode code) {
        std::lock_guard lock(record->album->impl_->mutex);
        auto &data = record->album->impl_->data;
        data.error = code;
        if (code == AlbumErrorCode::AuthenticationRequired || code == AlbumErrorCode::PermissionDenied) {
            data.contents = {};
            data.state = AlbumState::AuthenticationRequired;
        } else data.state = AlbumState::Failed;
    }
};
AlbumManager::AlbumManager(AlbumLimits limits) : impl_(std::make_unique<Impl>(limits)) {
    if (limits.maximumAlbums == 0 || limits.maximumPhotos == 0) throw std::invalid_argument("album limits must be positive");
}
AlbumManager::~AlbumManager() {
    // Destruction must follow completion of operations on this manager.
    for (const auto &[id, record] : impl_->records) { (void)id; Impl::retire(record); record->stop.request_stop(); }
}
std::shared_ptr<Album> AlbumManager::createLocal(const std::string &localId, const std::string &title) {
    if (localId.empty()) throw std::invalid_argument("local album id required");
    AlbumData data;
    data.connection = {"local", "", title, localId, AlbumOrigin::Local};
    validate(data.connection);
    data.id = albumId(data.connection); data.coverage = AlbumCoverage::Local;
    data.state = AlbumState::Ready; data.contents.complete = true;
    auto record = std::make_shared<Impl::Record>();
    record->album = std::shared_ptr<Album>(new Album(std::move(data)));
    std::lock_guard lock(impl_->mutex);
    if (impl_->records.contains(record->album->id())) throw std::invalid_argument("local album already exists");
    if (impl_->records.size() >= impl_->limits.maximumAlbums)
        throw AlbumError(AlbumErrorCode::LimitExceeded, "album count limit exceeded");
    impl_->records.emplace(record->album->id(), record);
    return record->album;
}
std::shared_ptr<Album> AlbumManager::connect(AlbumConnection connection, std::shared_ptr<AlbumSource> source, AlbumCoverage receivedCoverage) {
    validate(connection);
    if (connection.origin == AlbumOrigin::Local || connection.providerId == "local")
        throw std::invalid_argument("local albums use createLocal");
    if (source && source->providerId() != connection.providerId) throw std::invalid_argument("album source provider does not match connection");
    AlbumData data;
    data.id = albumId(connection); data.connection = std::move(connection);
    data.coverage = source ? source->coverage() : receivedCoverage;
    auto record = std::make_shared<Impl::Record>();
    record->album = std::shared_ptr<Album>(new Album(std::move(data))); record->source = std::move(source);
    std::shared_ptr<Impl::Record> previous;
    {
        std::lock_guard lock(impl_->mutex);
        const auto found = impl_->records.find(record->album->id());
        if (found != impl_->records.end()) { previous = found->second; Impl::retire(previous); }
        else if (impl_->records.size() >= impl_->limits.maximumAlbums)
            throw AlbumError(AlbumErrorCode::LimitExceeded, "album count limit exceeded");
        impl_->records.insert_or_assign(record->album->id(), record);
    }
    // Stop callbacks may re-enter host code; invoke them outside manager locks.
    if (previous) previous->stop.request_stop();
    return record->album;
}
void AlbumManager::refresh(const std::shared_ptr<Album> &album) {
    std::shared_ptr<Impl::Record> record;
    {
        std::lock_guard lock(impl_->mutex); record = impl_->checked(album);
        if (!record->source) throw AlbumError(AlbumErrorCode::Unsupported, "this album receives snapshots through publish");
        if (record->busy) throw AlbumError(AlbumErrorCode::Busy, "album refresh is already running");
        record->busy = true;
        std::lock_guard stateLock(album->impl_->mutex);
        album->impl_->data.state = AlbumState::Loading; album->impl_->data.error.reset();
    }
    try {
        auto result = record->source->read(record->stop.get_token());
        std::lock_guard lock(impl_->mutex);
        if (!impl_->current(record)) throw AlbumError(AlbumErrorCode::StaleConnection, "album connection changed during refresh");
        impl_->apply(record, std::move(result)); record->busy = false;
    } catch (...) {
        auto code = AlbumErrorCode::Transport;
        try { throw; } catch (const AlbumError &e) { code = e.code(); } catch (...) {}
        std::lock_guard lock(impl_->mutex);
        if (!impl_->current(record)) throw AlbumError(AlbumErrorCode::StaleConnection, "album connection changed during refresh");
        record->busy = false; impl_->failed(record, code);
        throw;
    }
}
void AlbumManager::publish(const std::shared_ptr<Album> &album, AlbumSnapshot received) {
    std::lock_guard lock(impl_->mutex);
    auto record = impl_->checked(album);
    if (record->busy) throw AlbumError(AlbumErrorCode::Busy, "album refresh is already running");
    try { impl_->apply(record, std::move(received)); }
    catch (const AlbumError &e) { impl_->failed(record, e.code()); throw; }
}
void AlbumManager::setLocalPhotos(const std::shared_ptr<Album> &album, std::vector<AlbumPhoto> photos) {
    std::lock_guard lock(impl_->mutex);
    auto record = impl_->checked(album);
    if (album->snapshot().connection.origin != AlbumOrigin::Local) throw AlbumError(AlbumErrorCode::ReadOnly, "external album contents are supplied by its source");
    impl_->apply(record, {std::move(photos), true, ""});
}
bool AlbumManager::disconnect(const std::shared_ptr<Album> &album) {
    std::shared_ptr<Impl::Record> record;
    {
        std::lock_guard lock(impl_->mutex);
        if (!album) return false;
        const auto found = impl_->records.find(album->id());
        if (found == impl_->records.end() || found->second->album != album) return false;
        record = found->second; Impl::retire(record); impl_->records.erase(found);
    }
    record->stop.request_stop(); return true;
}
std::vector<std::shared_ptr<Album>> AlbumManager::albums() const {
    std::lock_guard lock(impl_->mutex); std::vector<std::shared_ptr<Album>> result;
    for (const auto &[id, record] : impl_->records) { (void)id; result.push_back(record->album); } return result;
}
std::shared_ptr<Album> AlbumManager::album(const std::string &id) const {
    std::lock_guard lock(impl_->mutex); const auto found = impl_->records.find(id);
    return found == impl_->records.end() ? nullptr : found->second->album;
}
}
