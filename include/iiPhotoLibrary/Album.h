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
enum class AlbumOrigin { Local, ICloud, GooglePhotos, External };
enum class AlbumCoverage { Local, SystemPhotoLibrary, SelectedItems, AppCreatedItems, External };
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
struct AlbumPhoto {
    std::string id; // Assigned by AlbumManager, scoped to this album instance's identity.
    std::string sourcePhotoId, name, mediaType = "photo", mimeType, created;
    std::string contentUrl, productUrl; // Provider URLs can expire; never an original ownership claim.
    std::uint32_t width = 0, height = 0;
    bool favorite = false;
    std::map<std::string, std::string> properties;
    bool operator==(const AlbumPhoto &) const = default;
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
struct AlbumLimits { std::size_t maximumAlbums = 1000, maximumPhotos = 100000; };
class AlbumManager final {
  public:
    explicit AlbumManager(AlbumLimits limits = {});
    ~AlbumManager();
    AlbumManager(const AlbumManager &) = delete;
    AlbumManager &operator=(const AlbumManager &) = delete;
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
    std::vector<std::shared_ptr<Album>> albums() const;
    std::shared_ptr<Album> album(const std::string &id) const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace iiPhotoLibrary
