#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <vector>

namespace iiPhotoLibrary {
enum class AlbumOrigin { Local, ICloud, GooglePhotos, External, AllLibrary, Smart };
enum class AlbumCoverage { Local, SystemPhotoLibrary, SelectedItems, AppCreatedItems, External, AllLibrary, SmartLibrary };
enum class SmartAlbumKind { Screenshots, Imported, Duplicates, Hidden, Images, Videos, Favorite, Deleted };
enum class AlbumState { Connected, Loading, Ready, Failed, AuthenticationRequired, Disconnected };
enum class AlbumErrorCode {
    AuthenticationRequired, PermissionDenied, SelectionRequired, RateLimited, Transport,
    InvalidSnapshot, LimitExceeded, Busy, StaleConnection, NotFound, ReadOnly, Cancelled, Unsupported
};
class AlbumError : public std::runtime_error {
  public:
    AlbumError(AlbumErrorCode code, const std::string &message);
    AlbumErrorCode code() const noexcept { return code_; }
  private:
    AlbumErrorCode code_;
};
struct AlbumPhotoTraits {
    bool screenshot = false, imported = false, hidden = false;
    std::string contentHash; // SHA-256 of complete original bytes; empty means unknown.
    bool deleted = false; // SDK logical deletion; the source asset is retained.
    bool operator==(const AlbumPhotoTraits &) const = default;
};
struct AlbumPhotoTraitsUpdate {
    std::optional<bool> screenshot, imported, hidden;
    std::optional<std::string> contentHash; // An explicit empty string clears a known hash.
    std::optional<bool> favorite, deleted;
};
struct AlbumPhoto {
    std::string id; // Album-scoped; All Library uses provider/account/sourcePhotoId identity.
    std::string sourcePhotoId, name, mediaType = "photo", mimeType, created;
    std::string contentUrl, productUrl; // Provider URLs can expire; never an original ownership claim.
    std::uint32_t width = 0, height = 0;
    bool favorite = false;
    std::map<std::string, std::string> properties;
    AlbumPhotoTraits traits;
    bool operator==(const AlbumPhoto &) const = default;
};
struct DuplicatePhotoGroup {
    std::string contentHash;
    std::vector<AlbumPhoto> photos; // At least two distinct visible photo identities.
};
struct AlbumSnapshot {
    std::vector<AlbumPhoto> photos;
    // False: merge visible/selected items; absence is NOT evidence of deletion.
    // True: complete within this album's declared coverage, not necessarily the entire service.
    bool complete = false;
    std::string revision;
};
struct AlbumConnection {
    std::string providerId, accountId, title;
    std::string remoteAlbumId = "library"; // Service root by default, or an explicitly received album.
    AlbumOrigin origin = AlbumOrigin::External;
    bool operator==(const AlbumConnection &) const = default;
};
struct AlbumData {
    std::string id;
    AlbumConnection connection;
    AlbumCoverage coverage = AlbumCoverage::External;
    AlbumState state = AlbumState::Connected;
    std::optional<AlbumErrorCode> error;
    AlbumSnapshot contents;
    std::optional<SmartAlbumKind> smartKind;
};
class AlbumSource {
  public:
    virtual ~AlbumSource() = default;
    virtual std::string providerId() const = 0;
    virtual AlbumCoverage coverage() const = 0;
    virtual AlbumSnapshot read(std::stop_token stop) = 0;
};
// An actual independently owned instance. Refresh preserves the instance;
// reconnect creates another instance and disconnects/clears the previous one.
// All Library and smart albums take live, read-only snapshots of the manager's albums.
class Album final {
  public:
    ~Album();
    Album(const Album &) = delete;
    Album &operator=(const Album &) = delete;
    const std::string &id() const noexcept;
    AlbumData snapshot() const; // Owning, synchronized copy for UI/other readers.
  private:
    struct Impl;
    explicit Album(AlbumData data);
    std::unique_ptr<Impl> impl_;
    friend class AlbumManager;
};
// Limits apply to ordinary albums; virtual library views do not consume album
// slots or truncate their aggregate to a single album's maximumPhotos.
struct AlbumLimits { std::size_t maximumAlbums = 1000, maximumPhotos = 100000; };
class AlbumManager final {
  public:
    explicit AlbumManager(AlbumLimits limits = {});
    ~AlbumManager();
    AlbumManager(const AlbumManager &) = delete;
    AlbumManager &operator=(const AlbumManager &) = delete;
    // Stable, read-only "All Library" Album, also available through album(id).
    // snapshot() unions currently loaded photos, deduplicated by provider/account/sourcePhotoId.
    // No I/O is performed; complete is true only when every ordinary album is Ready and complete.
    std::shared_ptr<Album> allLibrary() const;
    // Eight stable views. Deleted items appear only in Deleted; otherwise hidden
    // items appear only in Hidden. Images/Videos use the existing mediaType.
    std::shared_ptr<Album> smartAlbum(SmartAlbumKind kind) const;
    std::vector<std::shared_ptr<Album>> smartAlbums() const;
    // Accepts an ordinary or virtual AlbumPhoto::id. Changes apply to all album
    // references of that provider/account/source photo and survive refresh.
    AlbumPhotoTraits photoTraits(const std::string &photoId) const;
    bool isFavorite(const std::string &photoId) const;
    void setPhotoTraits(const std::string &photoId, AlbumPhotoTraitsUpdate update);
    void setScreenshot(const std::string &photoId, bool screenshot);
    void setImported(const std::string &photoId, bool imported);
    void setHidden(const std::string &photoId, bool hidden);
    void setContentHash(const std::string &photoId, const std::string &sha256);
    void setFavorite(const std::string &photoId, bool favorite);
    void setDeleted(const std::string &photoId, bool deleted); // False restores the existing classifications.
    std::vector<DuplicatePhotoGroup> duplicateGroups() const;
    std::shared_ptr<Album> createLocal(const std::string &localId, const std::string &title);
    // A service/account/remoteAlbumId tuple owns ONE independently instantiated album.
    // A null source supports externally received snapshots through publish().
    // connect() does not perform I/O; call refresh() on a worker after login/access.
    std::shared_ptr<Album> connect(AlbumConnection connection, std::shared_ptr<AlbumSource> source = {},
                                   AlbumCoverage receivedCoverage = AlbumCoverage::External);
    void refresh(const std::shared_ptr<Album> &album);
    void publish(const std::shared_ptr<Album> &album, AlbumSnapshot received);
    void setLocalPhotos(const std::shared_ptr<Album> &album, std::vector<AlbumPhoto> photos);
    bool disconnect(const std::shared_ptr<Album> &album);
    std::vector<std::shared_ptr<Album>> albums() const; // Ordinary albums, excluding virtual views.
    std::shared_ptr<Album> album(const std::string &id) const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace iiPhotoLibrary
