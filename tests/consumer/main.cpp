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
    qInfo() << "Installed iiPhotoLibrary: native backend, controller and QML import passed";
    return 0;
}
