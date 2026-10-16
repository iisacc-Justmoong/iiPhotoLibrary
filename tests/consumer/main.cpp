#include <iiPhotoLibrary/iiPhotoLibrary.h>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QDebug>
#include "backend/runtime/appbootstrap.h"
int main(int argc, char **argv) {
    lvrs::AppBootstrapOptions options;
    options.applicationName = "iiPhotoLibraryConsumer";
    options.quickStyleName = "Basic";
    options.bootstrapGraphicsBackend = false;
    options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false;
    options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv);
    lvrs::postApplicationBootstrap(app, options);
    // Construction does not request permission, scan, import or remove real photos.
    auto library = iiPhotoLibrary::nativePhotoLibrary();
    if (!library || library->name().isEmpty()) return 2;
    iiPhotoLibrary::AlbumManager albums;
    auto localAlbum = albums.createLocal("local", "Local");
    auto systemAlbum = albums.connect({"device", "system-library", "System Photos"},
        iiPhotoLibrary::photoLibraryAlbumSource(library));
    albums.setLocalPhotos(localAlbum, {iiPhotoLibrary::AlbumPhoto{.sourcePhotoId = "fixture"}});
    if (localAlbum == systemAlbum || albums.albums().size() != 2) return 5;
    const auto all = albums.allLibrary()->snapshot();
    if (all.connection.title != "All Library" || all.contents.photos.size() != 1 || all.contents.complete) return 6;
    albums.setImported(all.contents.photos[0].id, true);
    if (albums.smartAlbum(iiPhotoLibrary::SmartAlbumKind::Imported)->snapshot().contents.photos.size() != 1) return 7;
    albums.setHidden(all.contents.photos[0].id, true);
    if (!albums.allLibrary()->snapshot().contents.photos.empty() ||
        albums.smartAlbum(iiPhotoLibrary::SmartAlbumKind::Hidden)->snapshot().contents.photos.size() != 1) return 8;
    albums.setHidden(all.contents.photos[0].id, false);
    if (albums.smartAlbum(iiPhotoLibrary::SmartAlbumKind::Images)->snapshot().contents.photos.size() != 1 ||
        !albums.smartAlbum(iiPhotoLibrary::SmartAlbumKind::Videos)->snapshot().contents.photos.empty()) return 9;
    albums.setFavorite(all.contents.photos[0].id, true);
    if (!albums.isFavorite(all.contents.photos[0].id) ||
        albums.smartAlbum(iiPhotoLibrary::SmartAlbumKind::Favorite)->snapshot().contents.photos.size() != 1) return 10;
    albums.setDeleted(all.contents.photos[0].id, true);
    if (!albums.allLibrary()->snapshot().contents.photos.empty() ||
        albums.smartAlbum(iiPhotoLibrary::SmartAlbumKind::Deleted)->snapshot().contents.photos.size() != 1) return 11;
    albums.setDeleted(all.contents.photos[0].id, false);
    if (albums.smartAlbum(iiPhotoLibrary::SmartAlbumKind::Favorite)->snapshot().contents.photos.size() != 1) return 12;
    const QString id(64, 'a');
    if (iiPhotoLibrary::importedPhotoId(iiPhotoLibrary::importedResourceName(id, 0, "hello.jpg")) != id) return 3;
    iiPhotoLibrary::PhotoController controller({}, nullptr, library);
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(PHOTO_QML_PATH));
    engine.addImportPath(QStringLiteral(LVRS_QML_PATH));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport iiPhotoLibrary 1.0\nPhotosView { width: 402; height: 700 }", QUrl());
    std::unique_ptr<QObject> view(component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
    if (!view) { qCritical() << component.errors(); return 4; }
    qInfo() << "Installed iiPhotoLibrary: album manager/All Library/eight smart albums/bridge, native backend, controller and QML import passed";
    return 0;
}
