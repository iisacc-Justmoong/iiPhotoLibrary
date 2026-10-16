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
    auto all = manager.allLibrary();
    const auto data = all->snapshot();
    if (data.connection.title != "All Library" || data.contents.photos.size() != 2 || data.contents.complete ||
        manager.album(all->id()) != all || manager.allLibrary() != all) return 2;
    const auto googleId = google->snapshot().contents.photos[0].id;
    const auto appleId = iCloud->snapshot().contents.photos[0].id;
    manager.setPhotoTraits(googleId, {.screenshot = true, .imported = true, .contentHash = std::string(64, 'a')});
    manager.setContentHash(appleId, std::string(64, 'a'));
    if (manager.smartAlbums().size() != 8 || manager.duplicateGroups().size() != 1 ||
        manager.smartAlbum(SmartAlbumKind::Screenshots)->snapshot().contents.photos.size() != 1 ||
        manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.photos.size() != 1) return 4;
    manager.setHidden(appleId, true);
    const auto hidden = manager.smartAlbum(SmartAlbumKind::Hidden)->snapshot();
    if (hidden.contents.photos.size() != 1 || all->snapshot().contents.photos.size() != 1 ||
        !manager.smartAlbum(SmartAlbumKind::Duplicates)->snapshot().contents.photos.empty()) return 5;
    manager.setHidden(hidden.contents.photos[0].id, false);
    auto images = manager.smartAlbum(SmartAlbumKind::Images);
    auto videos = manager.smartAlbum(SmartAlbumKind::Videos);
    auto favorite = manager.smartAlbum(SmartAlbumKind::Favorite);
    auto deleted = manager.smartAlbum(SmartAlbumKind::Deleted);
    if (images->snapshot().contents.photos.size() != 2 || !videos->snapshot().contents.photos.empty()) return 6;
    manager.setFavorite(googleId, true);
    if (!manager.isFavorite(googleId) || favorite->snapshot().contents.photos.size() != 1) return 7;
    manager.setDeleted(googleId, true);
    if (deleted->snapshot().contents.photos.size() != 1 || images->snapshot().contents.photos.size() != 1 ||
        !favorite->snapshot().contents.photos.empty() || !manager.duplicateGroups().empty()) return 8;
    manager.setDeleted(deleted->snapshot().contents.photos[0].id, false);
    if (favorite->snapshot().contents.photos.size() != 1 || !deleted->snapshot().contents.photos.empty()) return 9;
    manager.disconnect(iCloud);
    if (all->snapshot().contents.photos.size() != 1 || !all->snapshot().contents.complete) return 3;
    std::cout << "Relocated Albums package: received/Google, All Library and eight smart albums passed without Qt\n";
}
