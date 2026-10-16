#include <iiPhotoLibrary/PhotoLibraryAlbumSource.h>

namespace iiPhotoLibrary {
namespace {
class LibrarySource final : public AlbumSource {
    std::shared_ptr<PhotoLibrary> library_;
    std::string provider_;
  public:
    LibrarySource(std::shared_ptr<PhotoLibrary> library, std::string provider)
        : library_(std::move(library)), provider_(std::move(provider)) {}
    std::string providerId() const override { return provider_; }
    AlbumCoverage coverage() const override { return AlbumCoverage::SystemPhotoLibrary; }
    AlbumSnapshot read(std::stop_token stop) override {
        if (stop.stop_requested()) throw AlbumError(AlbumErrorCode::Cancelled, "photo album refresh cancelled");
        const auto before = library_->access();
        if (before == Access::Unsupported) throw AlbumError(AlbumErrorCode::Unsupported, "native photo library unavailable");
        if (before == Access::NotDetermined) throw AlbumError(AlbumErrorCode::AuthenticationRequired, "photo library access has not been requested");
        if (before == Access::Denied) throw AlbumError(AlbumErrorCode::PermissionDenied, "photo library access denied");
        std::stop_callback cancelNative(stop, [library = library_] { library->cancel(); });
        if (stop.stop_requested()) throw AlbumError(AlbumErrorCode::Cancelled, "photo album refresh cancelled");
        QString error;
        const auto native = library_->scan(&error);
        if (stop.stop_requested()) throw AlbumError(AlbumErrorCode::Cancelled, "photo album refresh cancelled");
        const auto after = library_->access();
        if (after == Access::Unsupported) throw AlbumError(AlbumErrorCode::Unsupported, "native photo library became unavailable");
        if (after == Access::Denied || after == Access::NotDetermined)
            throw AlbumError(AlbumErrorCode::PermissionDenied, "photo library access was revoked");
        if (!error.isEmpty()) throw AlbumError(AlbumErrorCode::Transport, "native photo album scan failed");
        AlbumSnapshot result;
        result.complete = native.complete && before == Access::Full && after == Access::Full;
        result.revision = native.revision.toStdString();
        result.photos.reserve(static_cast<std::size_t>(native.assets.size()));
        for (const auto &asset : native.assets) {
            if (stop.stop_requested()) throw AlbumError(AlbumErrorCode::Cancelled, "photo album refresh cancelled");
            AlbumPhoto photo;
            photo.sourcePhotoId = asset.identifier.toStdString();
            photo.name = asset.name.toStdString(); photo.mediaType = asset.media.toStdString();
            photo.created = asset.created.toString(Qt::ISODateWithMs).toStdString(); photo.favorite = asset.favorite;
            photo.traits.screenshot = asset.screenshot; photo.traits.imported = asset.imported; photo.traits.hidden = asset.hidden;
            photo.traits.deleted = asset.deleted;
            for (const auto &resource : asset.resources)
                if (!importedPhotoId(resource.name).isEmpty()) photo.traits.imported = true;
            photo.properties["native.stamp"] = asset.stamp.toStdString();
            if (!asset.cloudIdentifier.isEmpty()) photo.properties["native.cloudIdentifier"] = asset.cloudIdentifier.toStdString();
            // Native resource tokens stay inside PhotoLibrary; album references
            // resolve preview/original operations by sourcePhotoId when needed.
            result.photos.push_back(std::move(photo));
        }
        return result;
    }
};
}
std::shared_ptr<AlbumSource> photoLibraryAlbumSource(std::shared_ptr<PhotoLibrary> library, std::string providerId) {
    if (!library || providerId.empty()) throw std::invalid_argument("photo library and provider identity required");
    return std::make_shared<LibrarySource>(std::move(library), std::move(providerId));
}
}
