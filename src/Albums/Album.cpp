#include <iiPhotoLibrary/Album.h>
#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <set>
#include <tuple>

namespace iiPhotoLibrary {
AlbumError::AlbumError(AlbumErrorCode code, const std::string &message) : std::runtime_error(message), code_(code) {}
namespace {
using PhotoIdentity = std::tuple<std::string, std::string, std::string>;
std::string component(const std::string &value) { return std::to_string(value.size()) + ":" + value; }
std::string albumId(const AlbumConnection &connection) {
    return "album:" + component(connection.providerId) + component(connection.accountId) + component(connection.remoteAlbumId);
}
void textSize(const std::string &text, std::size_t maximum) {
    if (text.size() > maximum)
        throw AlbumError(AlbumErrorCode::LimitExceeded, "album metadata text limit exceeded");
}
void validate(const AlbumConnection &connection) {
    if (connection.origin == AlbumOrigin::AllLibrary || connection.origin == AlbumOrigin::Smart ||
        connection.providerId == "all-library" || connection.providerId == "smart-album")
        throw std::invalid_argument("virtual library albums are owned by the album manager");
    if (connection.providerId.empty() || connection.remoteAlbumId.empty() ||
        (connection.origin != AlbumOrigin::Local && connection.accountId.empty()))
        throw std::invalid_argument("album provider, account and remote album identity required");
    for (const auto *value : {&connection.providerId, &connection.accountId, &connection.remoteAlbumId}) textSize(*value, 4096);
    textSize(connection.title, 65536);
    if ((connection.origin == AlbumOrigin::ICloud && connection.providerId != "icloud") ||
        (connection.origin == AlbumOrigin::GooglePhotos && connection.providerId != "googlephotos"))
        throw std::invalid_argument("album origin does not match provider identity");
}
std::string normalizedHash(std::string hash) {
    if (hash.empty()) return hash;
    if (hash.size() != 64) throw AlbumError(AlbumErrorCode::InvalidSnapshot, "content hash must be a SHA-256 hexadecimal digest");
    for (auto &character : hash) {
        if (character >= 'A' && character <= 'F') character += 'a' - 'A';
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
            throw AlbumError(AlbumErrorCode::InvalidSnapshot, "content hash must be a SHA-256 hexadecimal digest");
    }
    return hash;
}
void validateTraits(AlbumPhotoTraits &traits, const std::string &mediaType) {
    traits.contentHash = normalizedHash(std::move(traits.contentHash));
    if (traits.screenshot && mediaType != "photo")
        throw AlbumError(AlbumErrorCode::InvalidSnapshot, "a screenshot must be a photo");
}
void applyTraits(AlbumPhotoTraits &traits, const AlbumPhotoTraitsUpdate &update) {
    if (update.screenshot) traits.screenshot = *update.screenshot;
    if (update.imported) traits.imported = *update.imported;
    if (update.hidden) traits.hidden = *update.hidden;
    if (update.contentHash) traits.contentHash = *update.contentHash;
    if (update.deleted) traits.deleted = *update.deleted;
}
void mergeUpdates(AlbumPhotoTraitsUpdate &target, const AlbumPhotoTraitsUpdate &update) {
    if (update.screenshot) target.screenshot = update.screenshot;
    if (update.imported) target.imported = update.imported;
    if (update.hidden) target.hidden = update.hidden;
    if (update.contentHash) target.contentHash = update.contentHash;
    if (update.favorite) target.favorite = update.favorite;
    if (update.deleted) target.deleted = update.deleted;
}
PhotoIdentity identity(const AlbumConnection &connection, const std::string &sourcePhotoId) {
    return {connection.providerId, connection.accountId, sourcePhotoId};
}
std::string libraryPhotoId(const PhotoIdentity &photo) {
    return "album:all-library" + component(std::get<0>(photo)) + component(std::get<1>(photo)) + component(std::get<2>(photo));
}
void validatePhoto(AlbumPhoto &photo) {
    if (photo.sourcePhotoId.empty() || (photo.mediaType != "photo" && photo.mediaType != "video"))
        throw AlbumError(AlbumErrorCode::InvalidSnapshot, "photo identity and media type required");
    textSize(photo.sourcePhotoId, 4096);
    for (const auto *value : {&photo.name, &photo.mimeType, &photo.created, &photo.contentUrl, &photo.productUrl}) textSize(*value, 65536);
    if (photo.properties.size() > 256)
        throw AlbumError(AlbumErrorCode::LimitExceeded, "photo metadata property limit exceeded");
    for (const auto &[key, value] : photo.properties) { textSize(key, 4096); textSize(value, 65536); }
    validateTraits(photo.traits, photo.mediaType);
}
}
struct Album::Impl {
    mutable std::mutex mutex;
    AlbumData data;
    std::function<std::optional<AlbumData>()> liveSnapshot;
    explicit Impl(AlbumData value) : data(std::move(value)) {}
};
Album::Album(AlbumData data) : impl_(std::make_unique<Impl>(std::move(data))) {}
Album::~Album() = default;
const std::string &Album::id() const noexcept { return impl_->data.id; }
AlbumData Album::snapshot() const {
    if (impl_->liveSnapshot)
        if (auto data = impl_->liveSnapshot()) return std::move(*data);
    std::lock_guard lock(impl_->mutex);
    return impl_->data;
}
struct AlbumManager::Impl {
    struct State;
    std::shared_ptr<State> state;
    explicit Impl(AlbumLimits limits);
};
// Readers hold this state only while taking a snapshot. The public manager
// retains its existing unique pimpl layout, and virtual albums hold weak links.
struct AlbumManager::Impl::State {
    struct Record {
        std::shared_ptr<Album> album;
        std::shared_ptr<AlbumSource> source;
        std::stop_source stop;
        bool busy = false;
    };
    mutable std::mutex mutex;
    std::map<std::string, std::shared_ptr<Record>> records;
    std::shared_ptr<Album> allLibrary;
    std::map<SmartAlbumKind, std::shared_ptr<Album>> smartAlbums;
    std::map<PhotoIdentity, AlbumPhotoTraitsUpdate> traitsOverrides;
    AlbumLimits limits;
    explicit State(AlbumLimits value) : limits(value) {}
    struct LibraryPhotos {
        std::map<PhotoIdentity, AlbumPhoto> photos;
        bool complete = true;
    };
    // Caller holds the manager lock. Hidden/deleted entries remain available here for
    // classification changes and are filtered only when a library view is read.
    LibraryPhotos collectPhotos() const {
        LibraryPhotos result;
        std::set<PhotoIdentity> conflictingHashes;
        for (const auto &[id, record] : records) {
            std::lock_guard stateLock(record->album->impl_->mutex);
            const auto &data = record->album->impl_->data;
            if (data.state != AlbumState::Ready || !data.contents.complete) result.complete = false;
            for (const auto &photo : data.contents.photos) {
                const auto &connection = data.connection;
                const auto key = identity(connection, photo.sourcePhotoId);
                auto [found, inserted] = result.photos.try_emplace(key, photo);
                auto &unique = found->second;
                if (!inserted) {
                    // Positive source classifications accumulate across memberships.
                    unique.traits.screenshot |= photo.traits.screenshot;
                    unique.traits.imported |= photo.traits.imported;
                    unique.traits.hidden |= photo.traits.hidden;
                    unique.traits.deleted |= photo.traits.deleted;
                    unique.favorite |= photo.favorite;
                    if (!photo.traits.contentHash.empty() && !conflictingHashes.contains(key)) {
                        if (unique.traits.contentHash.empty()) unique.traits.contentHash = photo.traits.contentHash;
                        else if (unique.traits.contentHash != photo.traits.contentHash) {
                            unique.traits.contentHash.clear(); conflictingHashes.insert(key);
                        }
                    }
                    continue; // First ordinary album by ID supplies other metadata.
                }
                unique.id = libraryPhotoId(key);
                unique.properties.insert_or_assign("allLibrary.providerId", connection.providerId);
                unique.properties.insert_or_assign("allLibrary.accountId", connection.accountId);
                unique.properties.insert_or_assign("allLibrary.albumId", id);
                unique.properties.insert_or_assign("allLibrary.photoId", photo.id);
            }
        }
        return result;
    }
    AlbumData librarySnapshot(const std::shared_ptr<Album> &view) const {
        // One manager lock keeps membership, classifications and source states consistent.
        std::lock_guard lock(mutex);
        AlbumData result;
        { std::lock_guard stateLock(view->impl_->mutex); result = view->impl_->data; }
        if (result.state == AlbumState::Disconnected) return result;
        auto library = collectPhotos();
        result.contents.complete = library.complete;
        const bool duplicates = result.smartKind == SmartAlbumKind::Duplicates;
        std::map<std::string, std::size_t> hashCounts;
        if (duplicates) {
            for (const auto &[key, photo] : library.photos) {
                (void)key;
                if (photo.traits.hidden || photo.traits.deleted) continue;
                if (photo.traits.contentHash.empty()) result.contents.complete = false;
                else ++hashCounts[photo.traits.contentHash];
            }
        }
        for (auto &[key, photo] : library.photos) {
            (void)key;
            if (result.smartKind == SmartAlbumKind::Deleted) {
                if (!photo.traits.deleted) continue;
            } else if (result.smartKind == SmartAlbumKind::Hidden) {
                if (!photo.traits.hidden || photo.traits.deleted) continue;
            } else {
                if (photo.traits.hidden || photo.traits.deleted) continue;
                if (result.smartKind == SmartAlbumKind::Screenshots && (photo.mediaType != "photo" || !photo.traits.screenshot)) continue;
                if (result.smartKind == SmartAlbumKind::Imported && !photo.traits.imported) continue;
                if (result.smartKind == SmartAlbumKind::Images && photo.mediaType != "photo") continue;
                if (result.smartKind == SmartAlbumKind::Videos && photo.mediaType != "video") continue;
                if (result.smartKind == SmartAlbumKind::Favorite && !photo.favorite) continue;
                if (duplicates && (photo.traits.contentHash.empty() || hashCounts.at(photo.traits.contentHash) < 2)) continue;
            }
            result.contents.photos.push_back(std::move(photo));
        }
        return result;
    }
    bool virtualAlbum(const std::shared_ptr<Album> &album) const {
        if (album == allLibrary) return true;
        for (const auto &[kind, view] : smartAlbums) { (void)kind; if (view == album) return true; }
        return false;
    }
    std::pair<PhotoIdentity, AlbumPhoto> resolvePhoto(const std::string &photoId) const {
        auto library = collectPhotos();
        for (const auto &[key, photo] : library.photos) if (photo.id == photoId) return {key, photo};
        for (const auto &[id, record] : records) {
            (void)id;
            std::lock_guard stateLock(record->album->impl_->mutex);
            const auto &data = record->album->impl_->data;
            for (const auto &photo : data.contents.photos)
                if (photo.id == photoId) {
                    const auto key = identity(data.connection, photo.sourcePhotoId);
                    return {key, library.photos.at(key)};
                }
        }
        throw AlbumError(AlbumErrorCode::NotFound, "photo is not present in this album manager");
    }
    void updateTraits(const std::string &photoId, AlbumPhotoTraitsUpdate update) {
        if (update.contentHash) update.contentHash = normalizedHash(std::move(*update.contentHash));
        const auto [key, currentPhoto] = resolvePhoto(photoId);
        (void)currentPhoto;
        if (!update.screenshot && !update.imported && !update.hidden && !update.contentHash && !update.favorite && !update.deleted) return;
        AlbumPhotoTraitsUpdate combined;
        if (const auto found = traitsOverrides.find(key); found != traitsOverrides.end()) combined = found->second;
        mergeUpdates(combined, update);
        struct Change { std::shared_ptr<Record> record; std::size_t index; AlbumPhotoTraits traits; bool favorite; };
        std::vector<Change> changes;
        for (const auto &[id, record] : records) {
            (void)id;
            std::lock_guard stateLock(record->album->impl_->mutex);
            const auto &data = record->album->impl_->data;
            if (data.connection.providerId != std::get<0>(key) || data.connection.accountId != std::get<1>(key)) continue;
            const auto found = std::lower_bound(data.contents.photos.begin(), data.contents.photos.end(), std::get<2>(key),
                [](const AlbumPhoto &photo, const std::string &sourceId) { return photo.sourcePhotoId < sourceId; });
            if (found == data.contents.photos.end() || found->sourcePhotoId != std::get<2>(key)) continue;
            auto traits = found->traits;
            applyTraits(traits, update); validateTraits(traits, found->mediaType);
            changes.push_back({record, static_cast<std::size_t>(found - data.contents.photos.begin()), std::move(traits),
                update.favorite.value_or(found->favorite)});
        }
        // All validation and allocating copies finish before any visible trait is changed.
        traitsOverrides.insert_or_assign(key, std::move(combined));
        for (auto &change : changes) {
            std::lock_guard stateLock(change.record->album->impl_->mutex);
            change.record->album->impl_->data.contents.photos[change.index].traits = std::move(change.traits);
            change.record->album->impl_->data.contents.photos[change.index].favorite = change.favorite;
        }
    }
    void pruneTraitsOverrides() {
        // Photos are sorted by source ID by apply(); pruning allocates no state after a commit.
        for (auto entry = traitsOverrides.begin(); entry != traitsOverrides.end();) {
            bool present = false;
            for (const auto &[id, record] : records) {
                (void)id;
                std::lock_guard stateLock(record->album->impl_->mutex);
                const auto &data = record->album->impl_->data;
                if (data.connection.providerId != std::get<0>(entry->first) || data.connection.accountId != std::get<1>(entry->first)) continue;
                const auto found = std::lower_bound(data.contents.photos.begin(), data.contents.photos.end(), std::get<2>(entry->first),
                    [](const AlbumPhoto &photo, const std::string &sourceId) { return photo.sourcePhotoId < sourceId; });
                if (found != data.contents.photos.end() && found->sourcePhotoId == std::get<2>(entry->first)) { present = true; break; }
            }
            if (present) ++entry; else entry = traitsOverrides.erase(entry);
        }
    }
    bool current(const std::shared_ptr<Record> &record) const {
        const auto found = records.find(record->album->id());
        return found != records.end() && found->second == record;
    }
    std::shared_ptr<Record> checked(const std::shared_ptr<Album> &album) const {
        if (virtualAlbum(album)) throw AlbumError(AlbumErrorCode::ReadOnly, "virtual library albums are read-only");
        if (album) {
            const auto found = records.find(album->id());
            if (found != records.end() && found->second->album == album) return found->second;
        }
        throw AlbumError(AlbumErrorCode::StaleConnection, "album is not owned by this active connection");
    }
    static void retire(const std::shared_ptr<Album> &album) {
        std::lock_guard lock(album->impl_->mutex);
        auto &data = album->impl_->data;
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
            const auto key = identity(record->album->impl_->data.connection, photo.sourcePhotoId);
            if (const auto found = traitsOverrides.find(key); found != traitsOverrides.end()) {
                applyTraits(photo.traits, found->second); validateTraits(photo.traits, photo.mediaType);
                if (found->second.favorite) photo.favorite = *found->second.favorite;
            }
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
AlbumManager::Impl::Impl(AlbumLimits limits) : state(std::make_shared<State>(limits)) {}
AlbumManager::AlbumManager(AlbumLimits limits) : impl_(std::make_unique<Impl>(limits)) {
    if (limits.maximumAlbums == 0 || limits.maximumPhotos == 0) throw std::invalid_argument("album limits must be positive");
    const auto makeView = [&](AlbumData data) {
        data.state = AlbumState::Ready; data.contents.complete = true;
        auto view = std::shared_ptr<Album>(new Album(std::move(data)));
        view->impl_->liveSnapshot = [manager = std::weak_ptr<Impl::State>(impl_->state),
                                    reference = std::weak_ptr<Album>(view)]() -> std::optional<AlbumData> {
            const auto current = manager.lock();
            const auto album = reference.lock();
            if (current && album) return current->librarySnapshot(album);
            return std::nullopt;
        };
        return view;
    };
    AlbumData data;
    data.id = "album:all-library";
    data.connection = {"all-library", "", "All Library", "library", AlbumOrigin::AllLibrary};
    data.coverage = AlbumCoverage::AllLibrary;
    impl_->state->allLibrary = makeView(std::move(data));
    for (const auto &[kind, slug, title] : std::array{
        std::tuple{SmartAlbumKind::Screenshots, "screenshots", "Screenshots"},
        std::tuple{SmartAlbumKind::Imported, "imported", "Imported Photos"},
        std::tuple{SmartAlbumKind::Duplicates, "duplicates", "Duplicates"},
        std::tuple{SmartAlbumKind::Hidden, "hidden", "Hidden"},
        std::tuple{SmartAlbumKind::Images, "images", "Images"},
        std::tuple{SmartAlbumKind::Videos, "videos", "Videos"},
        std::tuple{SmartAlbumKind::Favorite, "favorite", "Favorite"},
        std::tuple{SmartAlbumKind::Deleted, "deleted", "Deleted"}}) {
        AlbumData smart;
        smart.id = "album:smart:" + std::string(slug);
        smart.connection = {"smart-album", "", title, slug, AlbumOrigin::Smart};
        smart.coverage = AlbumCoverage::SmartLibrary; smart.smartKind = kind;
        impl_->state->smartAlbums.emplace(kind, makeView(std::move(smart)));
    }
}
AlbumManager::~AlbumManager() {
    // Destruction must follow completion of operations on this manager.
    auto &state = *impl_->state;
    decltype(state.records) retired;
    {
        std::lock_guard lock(state.mutex);
        retired.swap(state.records);
        for (const auto &[id, record] : retired) { (void)id; Impl::State::retire(record->album); }
        Impl::State::retire(state.allLibrary);
        for (const auto &[kind, view] : state.smartAlbums) { (void)kind; Impl::State::retire(view); }
        state.traitsOverrides.clear();
    }
    for (const auto &[id, record] : retired) { (void)id; record->stop.request_stop(); }
}
std::shared_ptr<Album> AlbumManager::allLibrary() const { return impl_->state->allLibrary; }
std::shared_ptr<Album> AlbumManager::smartAlbum(SmartAlbumKind kind) const {
    const auto found = impl_->state->smartAlbums.find(kind);
    if (found == impl_->state->smartAlbums.end()) throw std::invalid_argument("unknown smart album kind");
    return found->second;
}
std::vector<std::shared_ptr<Album>> AlbumManager::smartAlbums() const {
    std::vector<std::shared_ptr<Album>> result;
    result.reserve(impl_->state->smartAlbums.size());
    for (const auto &[kind, view] : impl_->state->smartAlbums) { (void)kind; result.push_back(view); }
    return result;
}
AlbumPhotoTraits AlbumManager::photoTraits(const std::string &photoId) const {
    const auto &state = *impl_->state;
    std::lock_guard lock(state.mutex);
    return state.resolvePhoto(photoId).second.traits;
}
bool AlbumManager::isFavorite(const std::string &photoId) const {
    const auto &state = *impl_->state;
    std::lock_guard lock(state.mutex);
    return state.resolvePhoto(photoId).second.favorite;
}
void AlbumManager::setPhotoTraits(const std::string &photoId, AlbumPhotoTraitsUpdate update) {
    auto &state = *impl_->state;
    std::lock_guard lock(state.mutex);
    state.updateTraits(photoId, std::move(update));
}
void AlbumManager::setScreenshot(const std::string &photoId, bool screenshot) { setPhotoTraits(photoId, {.screenshot = screenshot}); }
void AlbumManager::setImported(const std::string &photoId, bool imported) { setPhotoTraits(photoId, {.imported = imported}); }
void AlbumManager::setHidden(const std::string &photoId, bool hidden) { setPhotoTraits(photoId, {.hidden = hidden}); }
void AlbumManager::setContentHash(const std::string &photoId, const std::string &sha256) { setPhotoTraits(photoId, {.contentHash = sha256}); }
void AlbumManager::setFavorite(const std::string &photoId, bool favorite) { setPhotoTraits(photoId, {.favorite = favorite}); }
void AlbumManager::setDeleted(const std::string &photoId, bool deleted) { setPhotoTraits(photoId, {.deleted = deleted}); }
std::vector<DuplicatePhotoGroup> AlbumManager::duplicateGroups() const {
    const auto &state = *impl_->state;
    std::lock_guard lock(state.mutex);
    auto library = state.collectPhotos();
    std::map<std::string, std::vector<AlbumPhoto>> groups;
    for (auto &[key, photo] : library.photos) {
        (void)key;
        if (!photo.traits.hidden && !photo.traits.deleted && !photo.traits.contentHash.empty()) {
            const auto hash = photo.traits.contentHash;
            groups[hash].push_back(std::move(photo));
        }
    }
    std::vector<DuplicatePhotoGroup> result;
    for (auto &[hash, photos] : groups) if (photos.size() > 1) result.push_back({hash, std::move(photos)});
    return result;
}
std::shared_ptr<Album> AlbumManager::createLocal(const std::string &localId, const std::string &title) {
    auto &state = *impl_->state;
    if (localId.empty()) throw std::invalid_argument("local album id required");
    AlbumData data;
    data.connection = {"local", "", title, localId, AlbumOrigin::Local};
    validate(data.connection);
    data.id = albumId(data.connection); data.coverage = AlbumCoverage::Local;
    data.state = AlbumState::Ready; data.contents.complete = true;
    auto record = std::make_shared<Impl::State::Record>();
    record->album = std::shared_ptr<Album>(new Album(std::move(data)));
    std::lock_guard lock(state.mutex);
    if (state.records.contains(record->album->id())) throw std::invalid_argument("local album already exists");
    if (state.records.size() >= state.limits.maximumAlbums)
        throw AlbumError(AlbumErrorCode::LimitExceeded, "album count limit exceeded");
    state.records.emplace(record->album->id(), record);
    return record->album;
}
std::shared_ptr<Album> AlbumManager::connect(AlbumConnection connection, std::shared_ptr<AlbumSource> source, AlbumCoverage receivedCoverage) {
    auto &state = *impl_->state;
    validate(connection);
    if (connection.origin == AlbumOrigin::Local || connection.providerId == "local")
        throw std::invalid_argument("local albums use createLocal");
    if (source && source->providerId() != connection.providerId) throw std::invalid_argument("album source provider does not match connection");
    const auto coverage = source ? source->coverage() : receivedCoverage;
    if (coverage == AlbumCoverage::AllLibrary || coverage == AlbumCoverage::SmartLibrary)
        throw std::invalid_argument("virtual library coverage is reserved for the album manager");
    AlbumData data;
    data.id = albumId(connection); data.connection = std::move(connection);
    data.coverage = coverage;
    auto record = std::make_shared<Impl::State::Record>();
    record->album = std::shared_ptr<Album>(new Album(std::move(data))); record->source = std::move(source);
    std::shared_ptr<Impl::State::Record> previous;
    {
        std::lock_guard lock(state.mutex);
        const auto found = state.records.find(record->album->id());
        if (found != state.records.end()) { previous = found->second; Impl::State::retire(previous->album); }
        else if (state.records.size() >= state.limits.maximumAlbums)
            throw AlbumError(AlbumErrorCode::LimitExceeded, "album count limit exceeded");
        state.records.insert_or_assign(record->album->id(), record);
        state.pruneTraitsOverrides();
    }
    // Stop callbacks may re-enter host code; invoke them outside manager locks.
    if (previous) previous->stop.request_stop();
    return record->album;
}
void AlbumManager::refresh(const std::shared_ptr<Album> &album) {
    auto &state = *impl_->state;
    std::shared_ptr<Impl::State::Record> record;
    {
        std::lock_guard lock(state.mutex); record = state.checked(album);
        if (!record->source) throw AlbumError(AlbumErrorCode::Unsupported, "this album receives snapshots through publish");
        if (record->busy) throw AlbumError(AlbumErrorCode::Busy, "album refresh is already running");
        record->busy = true;
        std::lock_guard stateLock(album->impl_->mutex);
        album->impl_->data.state = AlbumState::Loading; album->impl_->data.error.reset();
    }
    try {
        auto result = record->source->read(record->stop.get_token());
        std::lock_guard lock(state.mutex);
        if (!state.current(record)) throw AlbumError(AlbumErrorCode::StaleConnection, "album connection changed during refresh");
        state.apply(record, std::move(result)); record->busy = false;
        state.pruneTraitsOverrides();
    } catch (...) {
        auto code = AlbumErrorCode::Transport;
        try { throw; } catch (const AlbumError &e) { code = e.code(); } catch (...) {}
        std::lock_guard lock(state.mutex);
        if (!state.current(record)) throw AlbumError(AlbumErrorCode::StaleConnection, "album connection changed during refresh");
        record->busy = false; state.failed(record, code);
        state.pruneTraitsOverrides();
        throw;
    }
}
void AlbumManager::publish(const std::shared_ptr<Album> &album, AlbumSnapshot received) {
    auto &state = *impl_->state;
    std::lock_guard lock(state.mutex);
    auto record = state.checked(album);
    if (record->busy) throw AlbumError(AlbumErrorCode::Busy, "album refresh is already running");
    try { state.apply(record, std::move(received)); state.pruneTraitsOverrides(); }
    catch (const AlbumError &e) { state.failed(record, e.code()); state.pruneTraitsOverrides(); throw; }
}
void AlbumManager::setLocalPhotos(const std::shared_ptr<Album> &album, std::vector<AlbumPhoto> photos) {
    auto &state = *impl_->state;
    std::lock_guard lock(state.mutex);
    auto record = state.checked(album);
    if (album->snapshot().connection.origin != AlbumOrigin::Local) throw AlbumError(AlbumErrorCode::ReadOnly, "external album contents are supplied by its source");
    state.apply(record, {std::move(photos), true, ""});
    state.pruneTraitsOverrides();
}
bool AlbumManager::disconnect(const std::shared_ptr<Album> &album) {
    auto &state = *impl_->state;
    std::shared_ptr<Impl::State::Record> record;
    {
        std::lock_guard lock(state.mutex);
        if (!album) return false;
        const auto found = state.records.find(album->id());
        if (found == state.records.end() || found->second->album != album) return false;
        record = found->second; Impl::State::retire(record->album); state.records.erase(found);
        state.pruneTraitsOverrides();
    }
    record->stop.request_stop(); return true;
}
std::vector<std::shared_ptr<Album>> AlbumManager::albums() const {
    auto &state = *impl_->state;
    std::lock_guard lock(state.mutex); std::vector<std::shared_ptr<Album>> result;
    for (const auto &[id, record] : state.records) { (void)id; result.push_back(record->album); } return result;
}
std::shared_ptr<Album> AlbumManager::album(const std::string &id) const {
    auto &state = *impl_->state;
    std::lock_guard lock(state.mutex);
    if (id == state.allLibrary->id()) return state.allLibrary;
    for (const auto &[kind, view] : state.smartAlbums) { (void)kind; if (id == view->id()) return view; }
    const auto found = state.records.find(id);
    return found == state.records.end() ? nullptr : found->second->album;
}
}
