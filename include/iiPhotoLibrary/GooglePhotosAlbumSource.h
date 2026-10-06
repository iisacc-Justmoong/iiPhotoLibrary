#pragma once
#include "Album.h"
#include <functional>

namespace iiPhotoLibrary {
struct AlbumHttpRequest {
    std::string url;
    std::map<std::string, std::string> headers;
    std::stop_token stop;
};
struct AlbumHttpResponse { int status = 0; std::string body; };
using AlbumHttpGet = std::function<AlbumHttpResponse(const AlbumHttpRequest &)>;
using PhotosAccessToken = std::function<std::string()>;
using PhotosPickerSession = std::function<std::string()>;
enum class GooglePhotosMode { PickerSelection, AppCreatedItems };
struct GooglePhotosAlbumOptions {
    GooglePhotosMode mode = GooglePhotosMode::PickerSelection;
    std::size_t maximumPages = 1000, maximumPhotos = 100000;
};
// OAuth/Picker UI and HTTPS transport are supplied by the host. SDK reads the
// current official API; full Google Photos library enumeration is unavailable.
class GooglePhotosAlbumSource final : public AlbumSource {
  public:
    GooglePhotosAlbumSource(PhotosAccessToken token, AlbumHttpGet get,
                            PhotosPickerSession pickerSession = {}, GooglePhotosAlbumOptions options = {});
    std::string providerId() const override { return "googlephotos"; }
    AlbumCoverage coverage() const override;
    AlbumSnapshot read(std::stop_token stop) override;
  private:
    PhotosAccessToken token_;
    AlbumHttpGet get_;
    PhotosPickerSession session_;
    GooglePhotosAlbumOptions options_;
    AlbumHttpResponse request(const std::string &url, std::stop_token stop);
};
}
