#include <iiPhotoLibrary/Album.h>
#include <iiPhotoLibrary/GooglePhotosAlbumSource.h>
#include <iostream>
int main() {
    using namespace iiPhotoLibrary;
    AlbumManager manager;
    auto iCloud = manager.connect({"icloud", "apple", "iCloud Photos", "library", AlbumOrigin::ICloud});
    auto google = manager.connect({"googlephotos", "google", "Google Photos", "library", AlbumOrigin::GooglePhotos},
        std::make_shared<GooglePhotosAlbumSource>([] { return "fixture"; }, [](const AlbumHttpRequest &) {
            return AlbumHttpResponse{200, R"({"mediaItems":[{"id":"one","filename":"created.jpg"}]})"};
        }, PhotosPickerSession{}, GooglePhotosAlbumOptions{GooglePhotosMode::AppCreatedItems}));
    manager.publish(iCloud, {{AlbumPhoto{.sourcePhotoId = "one"}}, false, "received"});
    manager.refresh(google);
    if (manager.albums().size() != 2 || iCloud == google ||
        iCloud->snapshot().contents.photos[0].id == google->snapshot().contents.photos[0].id) return 1;
    std::cout << "Relocated Albums package: separate received/Google instances passed without Qt\n";
}
