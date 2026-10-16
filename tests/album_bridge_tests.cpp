#include <iiPhotoLibrary/PhotoLibraryAlbumSource.h>
#include <iostream>
using namespace iiPhotoLibrary;
namespace {
int checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error(#__VA_ARGS__); } while (false)
template<class F> void fails(AlbumErrorCode code, F call) {
    try { call(); } catch (const AlbumError &e) { CHECK(e.code() == code); return; }
    throw std::runtime_error("expected album error");
}
class Library final : public PhotoLibrary {
  public:
    Access permission = Access::Full, after = Access::Full;
    int scans = 0;
    bool wasCancelled = false;
    std::function<void()> onScan;
    QString error;
    Snapshot contents;
    QString name() const override { return "fixture"; }
    Access access() const override { return permission; }
    void authorize(std::function<void()> done) override { done(); }
    void cancel() override { wasCancelled = true; }
    Snapshot scan(QString *message) override {
        ++scans; permission = after; *message = error;
        if (onScan) onScan();
        return contents;
    }
    bool preview(const QString &, const QString &, QString *) override { return false; }
    bool exportResource(const QString &, const QString &, const QString &, QString *) override { return false; }
    QString import(const QJsonObject &, const QStringList &, QString *) override { return {}; }
    bool trash(const QString &, QString *) override { return false; }
};
}
int main() {
    try {
        auto library = std::make_shared<Library>();
        Asset asset; asset.identifier = "native-id"; asset.cloudIdentifier = "cloud-id";
        asset.name = "native.jpg"; asset.media = "photo"; asset.stamp = "stamp"; asset.favorite = true;
        asset.screenshot = true; asset.imported = true; asset.hidden = true;
        asset.created = QDateTime::fromString("2026-10-02T00:00:00Z", Qt::ISODate);
        asset.resources.push_back({"private-token", "original.jpg", "photo"});
        library->contents = {{asset}, true, "native-revision"};
        auto source = photoLibraryAlbumSource(library, "icloud");
        AlbumManager manager;
        auto album = manager.connect({"icloud", "system-library", "iCloud Photos", "library", AlbumOrigin::ICloud}, source);
        manager.refresh(album);
        auto data = album->snapshot();
        CHECK(data.coverage == AlbumCoverage::SystemPhotoLibrary && data.contents.complete);
        CHECK(data.contents.revision == "native-revision" && data.contents.photos.size() == 1);
        const auto photo = data.contents.photos[0];
        CHECK(photo.sourcePhotoId == "native-id" && photo.favorite && photo.name == "native.jpg");
        CHECK(photo.created.starts_with("2026-10-02T00:00:00"));
        CHECK(photo.properties.at("native.cloudIdentifier") == "cloud-id");
        CHECK(photo.properties.size() == 2 && photo.contentUrl.empty());
        CHECK(photo.traits.screenshot && photo.traits.imported && photo.traits.hidden);
        CHECK(manager.allLibrary()->snapshot().contents.photos.empty());
        CHECK(manager.smartAlbum(SmartAlbumKind::Hidden)->snapshot().contents.photos.size() == 1);
        CHECK(manager.smartAlbum(SmartAlbumKind::Favorite)->snapshot().contents.photos.empty());
        auto removed = asset; removed.deleted = true;
        library->contents = {{removed}, true, "deleted"}; manager.refresh(album);
        CHECK(album->snapshot().contents.photos[0].traits.deleted);
        CHECK(manager.smartAlbum(SmartAlbumKind::Deleted)->snapshot().contents.photos.size() == 1 &&
            manager.smartAlbum(SmartAlbumKind::Hidden)->snapshot().contents.photos.empty());
        manager.setDeleted(photo.id, false);
        CHECK(manager.smartAlbum(SmartAlbumKind::Hidden)->snapshot().contents.photos.size() == 1 &&
            manager.smartAlbum(SmartAlbumKind::Deleted)->snapshot().contents.photos.empty());
        manager.setHidden(photo.id, false);
        CHECK(manager.smartAlbum(SmartAlbumKind::Screenshots)->snapshot().contents.photos.size() == 1);
        CHECK(manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.photos.size() == 1);
        CHECK(manager.smartAlbum(SmartAlbumKind::Images)->snapshot().contents.photos.size() == 1 &&
            manager.smartAlbum(SmartAlbumKind::Videos)->snapshot().contents.photos.empty());
        CHECK(manager.smartAlbum(SmartAlbumKind::Favorite)->snapshot().contents.photos.size() == 1 && manager.isFavorite(photo.id));
        manager.setFavorite(photo.id, false);
        CHECK(manager.smartAlbum(SmartAlbumKind::Favorite)->snapshot().contents.photos.empty() && !manager.isFavorite(photo.id));
        auto tagged = asset; tagged.imported = false;
        tagged.resources[0].name = importedResourceName(QString(64, 'a'), 0, "original.jpg");
        library->contents = {{tagged}, true, "tagged"};
        CHECK(source->read({}).photos[0].traits.imported);
        library->contents = {{asset}, true, "native-revision"};
        library->permission = library->after = Access::Limited;
        library->contents.assets.clear();
        manager.refresh(album);
        CHECK(!album->snapshot().contents.complete && album->snapshot().contents.photos.size() == 1);
        library->permission = Access::Full; library->after = Access::Limited;
        manager.refresh(album);
        CHECK(!album->snapshot().contents.complete && album->snapshot().contents.photos.size() == 1);
        library->permission = library->after = Access::Full; library->error = "private backend detail";
        fails(AlbumErrorCode::Transport, [&] { manager.refresh(album); });
        CHECK(album->snapshot().contents.photos.size() == 1);
        library->error.clear(); library->after = Access::Denied;
        fails(AlbumErrorCode::PermissionDenied, [&] { manager.refresh(album); });
        CHECK(album->snapshot().contents.photos.empty() && album->snapshot().state == AlbumState::AuthenticationRequired);
        const auto scans = library->scans;
        fails(AlbumErrorCode::PermissionDenied, [&] { source->read({}); });
        library->permission = Access::NotDetermined;
        fails(AlbumErrorCode::AuthenticationRequired, [&] { source->read({}); });
        library->permission = Access::Unsupported;
        fails(AlbumErrorCode::Unsupported, [&] { source->read({}); });
        std::stop_source stop; stop.request_stop();
        fails(AlbumErrorCode::Cancelled, [&] { source->read(stop.get_token()); });
        CHECK(library->scans == scans);
        auto retiring = std::make_shared<Library>();
        retiring->contents = {{asset}, true, "retired"};
        auto retiredAlbum = manager.connect({"device", "retiring", "System"}, photoLibraryAlbumSource(retiring));
        retiring->onScan = [&] { CHECK(manager.disconnect(retiredAlbum)); };
        fails(AlbumErrorCode::StaleConnection, [&] { manager.refresh(retiredAlbum); });
        CHECK(retiring->wasCancelled && retiredAlbum->snapshot().contents.photos.empty());
        std::cout << "native bridge: " << checks << " checks passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
