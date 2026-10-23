#include <iiPhotoLibrary/PhotoStore.h>
#include <iiPhotoLibrary/PhotoController.h>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QImage>
#include <QPointingDevice>
#include <QPainter>
#include <QLinearGradient>
#include <QJsonDocument>
#include <QJSValue>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QSemaphore>
#include <QScopeGuard>
#include <QLockFile>
#include <QCryptographicHash>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include "backend/runtime/appbootstrap.h"
#include <iiSocietyHelper/Sync/Sync.h>

using namespace iiPhotoLibrary;
namespace {
QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems()) if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size());
}
class Library final : public PhotoLibrary {
public:
    QString directory;
    Snapshot snapshot;
    std::atomic_int imports{0}, exports{0}, removals{0}, previews{0}, scans{0};
    QStringList previewOrder;
    std::function<void(const QString &)> beforePreview;
    std::atomic_bool cancelled{false};
    Access permission = Access::Full;
    int authorizations = 0;
    std::function<void()> authorizationFinished;
    std::function<void(const QString &)> beforeExport;
    explicit Library(const QString &path) : directory(path) { snapshot.complete = true; }
    QString name() const override { return "Test Photos"; }
    Access access() const override { return permission; }
    void authorize(std::function<void()> done) override { ++authorizations; authorizationFinished = std::move(done); }
    void cancel() override { cancelled = true; }
    Snapshot scan(QString *) override { ++scans; auto result = snapshot; result.complete = permission == Access::Full; return result; }
    bool preview(const QString &id, const QString &path, QString *) override {
        ++previews; previewOrder.append(id);
        if (beforePreview) beforePreview(id);
        // Non-square fixtures make unintended letterboxing visible in gallery QA.
        QByteArray contents;
        for (const auto &asset : snapshot.assets) if (asset.identifier == id && !asset.resources.isEmpty()) {
            QFile original(directory + '/' + asset.resources.first().token);
            if (original.open(QIODevice::ReadOnly)) contents = original.readAll();
        }
        const int hue = int(qChecksum(contents) % 360);
        QImage image(hue % 2 ? 180 : 320, hue % 2 ? 320 : 180, QImage::Format_RGB32);
        QPainter painter(&image);
        QLinearGradient gradient(0, 0, image.width(), image.height());
        gradient.setColorAt(0, QColor::fromHsv(hue, 180, 220));
        gradient.setColorAt(1, QColor::fromHsv((hue + 70) % 360, 120, 90));
        painter.fillRect(image.rect(), gradient);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(255, 255, 255, 70));
        painter.drawEllipse(QPointF(image.width()*0.55, image.height()*0.45), 50, 50);
        painter.end(); return image.save(path, "JPEG");
    }
    bool exportResource(const QString &, const QString &token, const QString &path, QString *) override {
        if (beforeExport) beforeExport(token);
        ++exports; return QFile::copy(directory + '/' + token, path);
    }
    QString import(const QJsonObject &record, const QStringList &paths, QString *) override {
        const auto id = record.value("id").toString();
        for (const auto &asset : snapshot.assets) if (asset.identifier == id) return id;
        Asset asset; asset.identifier = id; asset.name = record.value("name").toString();
        asset.media = record.value("media").toString(); asset.stamp = "imported";
        const auto resources = record.value("resources").toArray();
        for (int i = 0; i < paths.size(); ++i) {
            const auto name = importedResourceName(id, i, resources[i].toObject().value("name").toString());
            if (!QFile::copy(paths[i], directory + '/' + name)) return {};
            asset.resources.append({name, name, resources[i].toObject().value("role").toString()});
        }
        snapshot.assets.append(asset); ++imports; return id;
    }
    bool trash(const QString &id, QString *) override {
        for (qsizetype i = 0; i < snapshot.assets.size(); ++i) if (snapshot.assets[i].identifier == id) {
            snapshot.assets.removeAt(i); ++removals; break;
        }
        return true;
    }
    void add(const QString &name, const QByteArray &bytes, const QString &media = "photo") {
        write(directory + '/' + name, bytes);
        Asset asset; asset.identifier = name; asset.name = name; asset.stamp = "1";
        asset.created = QDateTime::fromString("2026-09-14T09:00:00Z", Qt::ISODate);
        asset.media = media; asset.resources = {{name, name, media}}; snapshot.assets.append(asset);
    }
};
struct Fixture {
    QTemporaryDir temporary{QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos-XXXXXX")};
    std::shared_ptr<Library> library;
    std::unique_ptr<PhotoStore> store;
    Fixture() {
        QDir().mkpath(temporary.path() + "/native");
        QDir().mkpath(temporary.path() + "/drive.societycontainer");
        library = std::make_shared<Library>(temporary.path() + "/native");
        const auto drive = iiSocietyContainer::SocietyDrive::create(temporary.path() + "/drive.societycontainer");
        if (!drive) qFatal("Could not create the photo test container");
        store = std::make_unique<PhotoStore>(drive->rootPath(), library);
        if (!store->open()) qFatal("Could not open the photo test store: %s", qPrintable(store->errorString()));
    }
    QString photos() const { return temporary.path() + "/drive.societycontainer/Photos"; }
};
}
class PhotosTest : public QObject {
    Q_OBJECT
private slots:
    void defaultRefreshIgnoresUnselectedNativePhotosAndPreservesContainerFiles() {
        Fixture f; f.library->add("outside.jpg", "native library original");
        const auto root = f.temporary.path() + "/drive.societycontainer";
        for (int attempt = 0; attempt < 3; ++attempt) {
            QVERIFY(f.store->refresh()); QVERIFY(f.store->catalog().isEmpty());
            QVERIFY(!QFileInfo::exists(root + "/.society-photos"));
        }
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(true); controller.configure(root, true);
        QTRY_VERIFY(!controller.busy()); QVERIFY(controller.entries().isEmpty());
        QCOMPARE(f.library->authorizations, 0); QCOMPARE(f.library->scans.load(), 0);
        QCOMPARE(f.library->exports.load(), 0); QCOMPARE(f.library->previews.load(), 0);
        QImage image(4, 4, QImage::Format_RGB32); image.fill(Qt::green);
        const auto supplied = f.photos() + "/user.png"; QVERIFY(image.save(supplied));
        const auto before = PhotoStore::digestFile(supplied);
        QVERIFY(f.store->refresh()); QCOMPARE(f.store->catalog().size(), 1);
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        QVERIFY(f.store->hasOriginal(id)); QVERIFY(f.store->consume(id));
        QCOMPARE(PhotoStore::digestFile(supplied), before);
        QVERIFY(f.store->trash(id)); QVERIFY(f.store->refresh());
        QCOMPARE(PhotoStore::digestFile(supplied), before);
        QCOMPARE(f.library->scans.load(), 0); QCOMPARE(f.library->imports.load(), 0);
        QCOMPARE(f.library->removals.load(), 0); QCOMPARE(f.library->snapshot.assets.size(), 1);
        QFile external(f.library->directory + "/outside.jpg"); QVERIFY(external.open(QIODevice::ReadOnly));
        QCOMPARE(external.readAll(), QByteArray("native library original"));
    }
    void explicitLibraryImportDoesNotAdoptLaterNativePhotosDuringRefresh() {
        Fixture f; f.library->add("selected.jpg", "selected original");
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.store->catalog().size(), 1);
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto scans = f.library->scans.load(), exports = f.library->exports.load();
        f.library->add("later.jpg", "not selected later original");
        QVERIFY(f.store->refresh()); QCOMPARE(f.store->catalog().size(), 1);
        QCOMPARE(f.library->scans.load(), scans); QCOMPARE(f.library->exports.load(), exports);
        QVERIFY(f.store->prepare(id));
        QFile original(f.store->resourcePath(id, 0)); QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("selected original"));
        QVERIFY(f.store->trash(id)); QVERIFY(f.store->importLibrary());
        QCOMPARE(f.library->removals.load(), 0); QCOMPARE(f.library->imports.load(), 0);
        QCOMPARE(f.library->snapshot.assets.size(), 2);
        bool deleted = false;
        for (const auto &value : f.store->catalog())
            if (value.toObject().value("id") == id) deleted = value.toObject().value("deleted").toBool();
        QVERIFY(deleted);
    }
    void permissionGrantedForAnOldContainerDoesNotImportIntoItsReplacement() {
        Fixture first, second; first.library->permission = Access::NotDetermined;
        first.library->add("not-selected.jpg", "native source");
        PhotoController controller({}, nullptr, first.library);
        controller.configure(first.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); controller.connectLibrary();
        QCOMPARE(first.library->authorizations, 1); QVERIFY(controller.busy());
        auto granted = std::exchange(first.library->authorizationFinished, {});
        QVERIFY(bool(granted));
        controller.configure(second.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); first.library->permission = Access::Full;
        granted(); QCoreApplication::processEvents();
        QTRY_VERIFY(!controller.busy()); QVERIFY(controller.entries().isEmpty());
        QCOMPARE(first.library->scans.load(), 0); QCOMPARE(first.library->exports.load(), 0);
        QVERIFY(!QFileInfo::exists(second.temporary.path() + "/drive.societycontainer/.society-photos"));
    }
    void openingAndReadingAnEmptyStoreCreatesNoPhotoDirectories() {
        Fixture f;
        const auto root = f.temporary.path() + "/drive.societycontainer";
        const auto privateRoot = root + "/.society-photos";
        const auto id = QString(64, 'a');
        const auto before = QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (int attempt = 0; attempt < 3; ++attempt) {
            QCOMPARE(f.store->catalog(), QJsonArray{});
            QCOMPARE(f.store->gallery(), QJsonArray{});
            QVERIFY(!f.store->hasOriginal(id));
            QVERIFY(f.store->resourcePath(id, 0).isEmpty());
            QCOMPARE(f.store->previewPath(id), f.photos() + "/.previews/" + id + ".jpg");
            QVERIFY(!f.store->command({{"id", id}, {"action", "available"}}).value("ok").toBool());
            f.store = std::make_unique<PhotoStore>(root, f.library);
            bool published = false;
            QVERIFY(f.store->open([&](const QJsonArray &rows, bool reset) {
                QCOMPARE(rows, QJsonArray{}); QVERIFY(reset); published = true;
            }));
            QVERIFY(published);
            QVERIFY(!QFileInfo::exists(privateRoot));
            QVERIFY(!QFileInfo::exists(f.photos() + "/.previews"));
            QCOMPARE(QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), before);
        }
        QCOMPARE(f.library->exports.load(), 0); QCOMPARE(f.library->previews.load(), 0);
    }
    void photoDiscoveryAndOriginalPreparationCreateOnlyRequiredParents() {
        Fixture f; const QByteArray original("original bytes");
        f.library->add("first.jpg", original);
        const auto privateRoot = f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId();
        QVERIFY(!QFileInfo::exists(privateRoot));
        QVERIFY2(f.store->importLibrary(), qPrintable(f.store->errorString()));
        QCOMPARE(f.library->scans.load(), 1); // Preflight discovery is reused by the writer.
        const auto record = f.store->catalog().first().toObject();
        const auto id = record.value("id").toString();
        for (const auto &path : {privateRoot + "/gallery", privateRoot + "/references", privateRoot + "/incoming", f.photos() + "/.previews"})
            QVERIFY2(QFileInfo(path).isDir(), qPrintable(path));
        QVERIFY(!QFileInfo::exists(privateRoot + "/originals"));
        QVERIFY(!QFileInfo::exists(privateRoot + "/viewing"));
        // Simulate external removal of a payload-empty staging directory.
        // Provider lease sentinels are persistent control files, not cached originals.
        for (const auto &name : QDir(privateRoot + "/incoming").entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) QCOMPARE(name, QStringLiteral(".iisacc-temporary.lock"));
        QVERIFY(QDir(privateRoot + "/incoming").removeRecursively());
        QVERIFY2(f.store->prepare(id), qPrintable(f.store->errorString()));
        QFile cached(f.store->resourcePath(id, 0)); QVERIFY(cached.open(QIODevice::ReadOnly));
        QCOMPARE(cached.readAll(), original); cached.close();
        QVERIFY(QFileInfo(privateRoot + "/originals").isDir());
        QVERIFY(!QFileInfo::exists(privateRoot + "/viewing"));
        QVERIFY(cached.remove());
        for (const auto &name : QDir(privateRoot + "/originals").entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) QCOMPARE(name, QStringLiteral(".iisacc-temporary.lock"));
        QVERIFY(QDir(privateRoot + "/originals").removeRecursively());
        for (const auto &name : QDir(privateRoot + "/incoming").entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) QCOMPARE(name, QStringLiteral(".iisacc-temporary.lock"));
        QVERIFY(QDir(privateRoot + "/incoming").removeRecursively());
        QVERIFY2(f.store->prepare(id), qPrintable(f.store->errorString()));
        QCOMPARE(PhotoStore::digestFile(f.store->resourcePath(id, 0)), record.value("resources").toArray().first().toObject().value("hash").toString());
    }
    void emptyRefreshAndAutomaticConnectionDoNotPersistPhotoState() {
        for (const auto permission : {Access::NotDetermined, Access::Denied, Access::Unsupported, Access::Full, Access::Limited}) {
            Fixture f; f.library->permission = permission;
            const auto root = f.temporary.path() + "/drive.societycontainer";
            for (int attempt = 0; attempt < 3; ++attempt) {
                bool published = false;
                QVERIFY2(f.store->refresh({}, [&](const QJsonArray &rows, bool reset) {
                    QCOMPARE(rows, QJsonArray{}); QVERIFY(reset); published = true;
                }), qPrintable(f.store->errorString()));
                QVERIFY(published);
                QVERIFY(!QFileInfo::exists(root + "/.society-photos"));
                QVERIFY(QDir(f.photos()).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty());
            }
            QCOMPARE(f.library->scans.load(), 0);
            PhotoController controller({}, nullptr, f.library);
            controller.setApplicationActive(false);
            controller.configure(root, true);
            QTRY_VERIFY(!controller.busy());
            QVERIFY(controller.entries().isEmpty());
            QVERIFY(!QFileInfo::exists(root + "/.society-photos"));
            QVERIFY(!QFileInfo::exists(f.photos() + "/.previews"));
            QCOMPARE(f.library->authorizations, 0);
            QCOMPARE(f.library->exports.load(), 0); QCOMPARE(f.library->previews.load(), 0);
        }
    }
    void emptyScaffoldingFromAnOlderSessionDoesNotBecomePersistentPhotoState() {
        for (const auto permission : {Access::NotDetermined, Access::Denied, Access::Unsupported, Access::Full, Access::Limited}) {
            Fixture f; f.library->permission = permission;
            const auto privateRoot = f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId();
            for (const auto &path : {privateRoot + "/gallery", privateRoot + "/references", privateRoot + "/originals", privateRoot + "/incoming", f.photos() + "/.previews"})
                QVERIFY(QDir().mkpath(path));
            for (int attempt = 0; attempt < 3; ++attempt) {
                QVERIFY2(f.store->refresh(), qPrintable(f.store->errorString()));
                QDirIterator files(privateRoot, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                QVERIFY(!files.hasNext());
                QVERIFY(QDir(f.photos() + "/.previews").isEmpty());
            }
            // Unknown data prevents the empty fast path and is never overwritten.
            const auto unknown = privateRoot + "/user-note.txt"; write(unknown, "keep this data");
            QVERIFY2(f.store->refresh(), qPrintable(f.store->errorString()));
            QFile preserved(unknown); QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(), QByteArray("keep this data"));
            QVERIFY(!QFileInfo::exists(privateRoot + "/index.json"));
        }
    }
    void transferCreatesStagingAndCommitParentsWithoutAnInitialRefresh() {
        Fixture source, target; const QByteArray original("received original");
        source.library->add("clip.mov", original, "video"); QVERIFY(source.store->importLibrary());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        const auto privateRoot = target.temporary.path() + "/drive.societycontainer/.society-photos/" + target.store->containerId();
        QJsonObject request{{"id", id}, {"resource", 0}, {"action", "available"}};
        QVERIFY(target.store->command(request).value("ok").toBool());
        QVERIFY(!QFileInfo::exists(privateRoot));
        request["action"] = "begin";
        QVERIFY2(target.store->command(request).value("ok").toBool(), qPrintable(target.store->errorString()));
        QVERIFY(QFileInfo(privateRoot + "/incoming").isDir());
        for (const auto &name : {"originals", "gallery", "references"}) QVERIFY(!QFileInfo::exists(privateRoot + '/' + name));
        QVERIFY(!QFileInfo::exists(target.photos() + "/.previews"));
        request["action"] = "chunk"; request["offset"] = "0"; request["data"] = QString::fromLatin1(original.toBase64());
        QVERIFY(target.store->command(request).value("ok").toBool());
        request["action"] = "commit"; QVERIFY(target.store->command(request).value("ok").toBool());
        QFile cached(target.store->resourcePath(id, 0)); QVERIFY(cached.open(QIODevice::ReadOnly)); QCOMPARE(cached.readAll(), original);
        QVERIFY(QFileInfo(privateRoot + "/originals").isDir());
        QVERIFY(!QFileInfo::exists(privateRoot + "/incoming/" + record.value("resources").toArray().first().toObject().value("hash").toString()));
        QCOMPARE(target.library->imports.load(), 0);
    }
    void galleryIndexesOutwardAndReprioritizesWithoutExportingOriginals() {
        Fixture f;
        const auto epoch = QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
        for (int i = 0; i < 8; ++i) {
            f.library->add(QString::number(i) + ".jpg", QByteArray("original-") + QByteArray::number(i));
            f.library->snapshot.assets.last().created = epoch.addDays(i);
        }
        const auto key = [](int i) { return QString::fromLatin1(QCryptographicHash::hash((QString::number(i) + ".jpg").toUtf8(), QCryptographicHash::Sha256).toHex()); };
        f.store->prioritize({key(4)});
        int published = 0;
        QVERIFY(f.store->importLibrary({}, [&](const QJsonArray &rows, bool reset) {
            if (reset) { QCOMPARE(rows.size(), 8); return; }
            if (f.library->exports.load() != 0) return;
            QVERIFY(!rows[0].toObject().value("previewStamp").toString().isEmpty());
            if (++published == 1) f.store->prioritize({key(7)});
        }));
        QCOMPARE(f.library->previewOrder, QStringList({"4.jpg", "7.jpg", "6.jpg", "5.jpg", "3.jpg", "2.jpg", "1.jpg", "0.jpg"}));
        QCOMPARE(published, 8); QCOMPARE(f.library->previews.load(), 8);
        QCOMPARE(f.store->catalog().size(), 8);
    }
    void galleryIndexSurvivesCancellationReopenAndRepairsEditedOrMissingPreviews() {
        Fixture f;
        for (int i = 0; i < 6; ++i) f.library->add(QString::number(i) + ".jpg", QByteArray("original-") + QByteArray::number(i));
        int ready = 0;
        QVERIFY(!f.store->importLibrary({}, [&](const QJsonArray &, bool reset) {
            if (!reset && ++ready == 2) f.store->cancel();
        }));
        QCOMPARE(f.library->exports.load(), 0); QCOMPARE(f.library->previews.load(), 2);
        f.store = std::make_unique<PhotoStore>(f.temporary.path() + "/drive.societycontainer", f.library);
        QVERIFY(f.store->open());
        const auto restored = f.store->gallery(); QCOMPARE(restored.size(), 6);
        int saved = 0;
        for (const auto &row : restored) if (!row.toObject().value("previewStamp").toString().isEmpty()) ++saved;
        QCOMPARE(saved, 2);
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.library->previews.load(), 6);
        const auto exports = f.library->exports.load();
        f.store = std::make_unique<PhotoStore>(f.temporary.path() + "/drive.societycontainer", f.library);
        QVERIFY(f.store->open()); QVERIFY(f.store->importLibrary());
        QCOMPARE(f.library->previews.load(), 6); QCOMPARE(f.library->exports.load(), exports);
        const auto row = f.store->gallery().first().toObject();
        const auto cached = QUrl(row.value("preview").toString()).toLocalFile();
        write(cached, "broken jpeg");
        // The canonical alias preview repairs a corrupt gallery preview without native I/O.
        const auto scans = f.library->scans.load();
        QVERIFY(f.store->refresh()); QVERIFY(QImage(cached).isNull() == false);
        QCOMPARE(f.library->scans.load(), scans);
        QCOMPARE(f.library->previews.load(), 6);
        f.library->snapshot.assets.first().stamp = "edited";
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.library->previews.load(), 7);
    }
    void compactGalleryIsPublishedBeforeReadingIndividualReferences() {
        Fixture f; f.library->add("a.jpg", "first"); f.library->add("b.jpg", "second");
        QVERIFY(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto privateRoot = f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId();
        QFile compact(privateRoot + "/gallery-catalog.json"); QVERIFY(compact.open(QIODevice::ReadOnly));
        const auto bytes = compact.readAll();
        QVERIFY(!bytes.contains(f.temporary.path().toUtf8())); // Relocatable relative preview filenames.
        QVERIFY(!bytes.contains("identifier"));
        PhotoStore reopened(f.temporary.path() + "/drive.societycontainer", f.library);
        bool published = false;
        const bool opened = reopened.open([&](const QJsonArray &rows, bool reset) {
            QVERIFY(reset); QCOMPARE(rows.size(), 2); published = true;
            for (const auto &value : rows) {
                const auto row = value.toObject();
                QVERIFY(!row.value("previewStamp").toString().isEmpty());
                QVERIFY(QFile::exists(QUrl(row.value("preview").toString()).toLocalFile()));
            }
            // If the callback runs first, a subsequent reference error does not
            // prevent the cached gallery from being published.
            write(privateRoot + "/references/" + id + ".json", "invalid reference");
        });
        QVERIFY(published); QVERIFY(!opened);
    }
    void explicitConnectionRequestsPhotoAccessAndPublishesAfterGrant() {
        Fixture f; f.library->permission = Access::NotDetermined; f.library->add("first.jpg", "first photo");
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(false);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 0);
        controller.setApplicationActive(true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 0);
        QCOMPARE(f.library->scans.load(), 0); QVERIFY(controller.entries().isEmpty());
        controller.connectLibrary();
        QTRY_COMPARE(f.library->authorizations, 1); QVERIFY(controller.busy());
        controller.refresh(); controller.connectLibrary(); controller.setApplicationActive(true);
        QCOMPARE(f.library->authorizations, 1);
        f.library->permission = Access::Full;
        std::exchange(f.library->authorizationFinished, {})();
        QTRY_COMPARE(controller.entries().size(), 1); QTRY_VERIFY(!controller.busy());
        QVERIFY(QFile::exists(f.photos() + '/' + controller.entries()[0].toMap()["id"].toString() + ".societyphoto"));
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 1);
    }
    void automaticPhotoAccessWaitsForAContainerAndNeverOpensSettings() {
        Fixture f; f.library->permission = Access::NotDetermined;
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(true); controller.refresh(); QCOMPARE(f.library->authorizations, 0);
        f.library->permission = Access::Denied;
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 0);
        f.library->permission = Access::Limited; f.library->add("allowed.jpg", "selected photo");
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        QTRY_VERIFY(!controller.busy()); QVERIFY(controller.entries().isEmpty());
        QCOMPARE(f.library->authorizations, 0); QCOMPARE(f.library->scans.load(), 0);
        controller.connectLibrary();
        QTRY_COMPARE(controller.entries().size(), 1); QCOMPARE(f.library->authorizations, 0);
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        QTRY_VERIFY(!controller.busy()); QCOMPARE(f.library->authorizations, 0);
    }
    void indexedPhotosAppearBeforeASlowOriginalFinishesAndReportRealProgress() {
        Fixture f; f.library->add("first.jpg", "first photo"); f.library->add("slow.mov", "second video", "video");
        QSemaphore gate;
        f.library->beforeExport = [&](const QString &token) { if (token == "slow.mov") gate.acquire(); };
        PhotoController controller({}, nullptr, f.library);
        // Release before the controller destructor joins its worker, even on a failed assertion.
        const auto release = qScopeGuard([&] { gate.release(); });
        QSignalSpy progress(&controller, &PhotoController::progress);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); controller.connectLibrary();
        QTRY_COMPARE(controller.entries().size(), 2); QVERIFY(controller.busy());
        QTRY_VERIFY(!controller.entries()[0].toMap().value("previewStamp").toString().isEmpty());
        QTRY_VERIFY(!controller.entries()[1].toMap().value("previewStamp").toString().isEmpty());
        QTRY_COMPARE(progress.size(), 1); QCOMPARE(progress[0][1].toLongLong(), qint64(11));
        QCOMPARE(progress[0][2].toLongLong(), qint64(11));
        gate.release(); QTRY_COMPARE(controller.entries().size(), 2); QTRY_VERIFY(!controller.busy());
        QCOMPARE(progress.size(), 2); QCOMPARE(progress[1][1].toLongLong(), qint64(12));
    }
    void cancelledPhotoWorkReleasesItsFileLockBeforeBackgroundDrainReturns() {
        Fixture f; f.library->add("slow.mov", "video", "video"); QSemaphore entered;
        std::atomic_bool exited{false};
        f.library->beforeExport = [&](const QString &) {
            entered.release();
            while (!f.library->cancelled) QThread::msleep(1);
            exited = true;
        };
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); controller.connectLibrary();
        QTRY_VERIFY(entered.available());
        controller.configure(f.temporary.path() + "/drive.societycontainer", false);
        controller.waitForDone(); QVERIFY(exited.load());
        QLockFile lock(f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId() + "/operation.lock");
        QVERIFY(lock.tryLock());
        QCoreApplication::processEvents(); QVERIFY(!controller.active()); QVERIFY(controller.entries().isEmpty());
    }
    void hashingCancellationStopsBeforePublishingAPartialDigest() {
        Fixture f; const QByteArray bytes(4 * 1024 * 1024, 'x');
        const auto path = f.temporary.path() + "/large.mov"; write(path, bytes);
        int chunks = 0; QString error;
        QVERIFY(PhotoStore::digestFile(path, &error, [&] { return ++chunks == 3; }).isEmpty());
        QCOMPARE(chunks, 3); QVERIFY(!error.isEmpty());
        QCOMPARE(PhotoStore::digestFile(path), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }
    void incrementalPhotoBatchesKeepEveryPreviouslyPublishedRecord() {
        Fixture f; f.library->add("existing.jpg", "existing"); QVERIFY(f.store->importLibrary());
        for (int i = 0; i < 8; ++i) f.library->add(QString::number(i) + ".jpg", QByteArray::number(i));
        f.library->add("last.mov", "last", "video"); QSemaphore gate;
        f.library->beforeExport = [&](const QString &token) {
            if (token == "4.jpg") QThread::msleep(550);
            if (token == "last.mov") gate.acquire();
        };
        PhotoController controller({}, nullptr, f.library);
        const auto release = qScopeGuard([&] { gate.release(); });
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); controller.connectLibrary();
        QTRY_VERIFY(controller.entries().size() >= 6); QVERIFY(controller.busy());
        QSet<QString> names; for (const auto &row : controller.entries()) names.insert(row.toMap().value("name").toString());
        QVERIFY(names.contains("existing.jpg")); QVERIFY(names.contains("0.jpg")); QVERIFY(names.contains("4.jpg"));
        gate.release(); QTRY_VERIFY(!controller.busy()); QCOMPARE(controller.entries().size(), 10);
    }
    void portableNamesKeepTheirExtensionAndPlatformLimits() {
        const auto id = QString(64, 'a');
        const auto name = importedResourceName(id, 0, QString(100, QChar(0xac00)) + ":test?.png");
        QCOMPARE(importedPhotoId(name), id); QVERIFY(name.endsWith(".png")); QVERIFY(name.toUtf8().size() < 255);
        QVERIFY(!name.contains(':')); QVERIFY(!name.contains('?'));
        QVERIFY(importedPhotoId("Society-" + id + "-x-original.png").isEmpty());
    }
    void nativeOriginalsRemainAliasesAndPreviewsPersist() {
        Fixture f; f.library->add("still.jpg", "original-photo"); f.library->add("movie.mp4", "original-video", "video");
        QVERIFY2(f.store->importLibrary(), qPrintable(f.store->errorString()));
        const auto records = f.store->catalog(); QCOMPARE(records.size(), 2);
        QCOMPARE(f.library->imports, 0);
        for (const auto &value : records) {
            const auto record = value.toObject(); const auto id = record.value("id").toString();
            QVERIFY(QFile::exists(f.photos() + '/' + id + ".societyphoto"));
            QVERIFY(QFile::exists(f.store->previewPath(id)));
            QVERIFY(!QFile::exists(f.photos() + '/' + record.value("name").toString()));
            QVERIFY(!QJsonDocument(record).toJson().contains(f.library->directory.toUtf8()));
        }
        QFile original(f.library->directory + "/still.jpg"); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original-photo"));
        const int exported = f.library->exports.load();
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.library->exports, exported); QCOMPARE(f.store->catalog(), records);
        f.store->releaseExports();
        for (const auto &value : records) QVERIFY(!QFile::exists(f.store->resourcePath(value.toObject().value("id").toString(), 0)));
    }
    void duplicatesPreserveAllNativeResourcesAndShareOriginalBytes() {
        Fixture f; f.library->add("live.jpg", "photo-bytes");
        write(f.library->directory + "/live.mov", "motion-bytes");
        f.library->snapshot.assets.first().resources.append({"live.mov", "live.mov", "pairedVideo"});
        QVERIFY(f.store->importLibrary()); const auto original = f.store->catalog().first().toObject();
        const auto id = original.value("id").toString();
        const auto exported = f.store->originalsForSharing(id); QCOMPARE(exported.size(), 2);
        QFile still(exported[0]); QVERIFY(still.open(QIODevice::ReadOnly)); QCOMPARE(still.readAll(), QByteArray("photo-bytes"));
        QFile motion(exported[1]); QVERIFY(motion.open(QIODevice::ReadOnly)); QCOMPARE(motion.readAll(), QByteArray("motion-bytes"));
        QVERIFY2(f.store->duplicate(id), qPrintable(f.store->errorString()));
        QCOMPARE(f.library->imports.load(), 0); QCOMPARE(f.library->snapshot.assets.size(), 1);
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.store->catalog().size(), 2);
        QString duplicate;
        for (const auto &value : f.store->catalog()) if (value.toObject().value("id") != id) {
            duplicate = value.toObject().value("id").toString();
            QCOMPARE(value.toObject().value("resources").toArray().size(), 2);
        }
        QVERIFY(!duplicate.isEmpty()); QVERIFY(f.store->trash(duplicate));
        QCOMPARE(f.library->removals.load(), 0); QVERIFY(QFileInfo::exists(f.library->directory + "/live.jpg"));
    }
    void containerOwnedCopiesAndReceivedRevisionsKeepTheirOriginals() {
        Fixture f; f.library->add("native.jpg", "native original");
        QVERIFY(f.store->importLibrary());
        auto record = f.store->catalog().first().toObject();
        const auto id = record.value("id").toString();
        QVERIFY(f.store->duplicate(id));
        QString duplicate;
        for (const auto &value : f.store->catalog())
            if (value.toObject().value("id") != id) duplicate = value.toObject().value("id").toString();
        QVERIFY(!duplicate.isEmpty());
        f.store->releaseExports(); QVERIFY(f.store->hasOriginal(duplicate));
        const QByteArray replacement("received replacement original");
        auto resource = record.value("resources").toArray().first().toObject();
        resource["hash"] = QString::fromLatin1(QCryptographicHash::hash(replacement, QCryptographicHash::Sha256).toHex());
        resource["size"] = QString::number(replacement.size()); record["resources"] = QJsonArray{resource};
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        QJsonObject packet{{"id", id}, {"resource", 0}, {"action", "begin"}};
        QVERIFY(f.store->command(packet).value("ok").toBool());
        packet["action"] = "chunk"; packet["offset"] = "0"; packet["data"] = QString::fromLatin1(replacement.toBase64());
        QVERIFY(f.store->command(packet).value("ok").toBool());
        packet["action"] = "commit"; QVERIFY(f.store->command(packet).value("ok").toBool());
        QVERIFY(f.store->consume(id));
        QFile received(f.store->resourcePath(id, 0)); QVERIFY(received.open(QIODevice::ReadWrite));
        QVERIFY(received.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600), QFileDevice::FileModificationTime));
        received.close(); f.store->releaseExports(); QVERIFY(f.store->refresh());
        QVERIFY(f.store->hasOriginal(id)); QVERIFY(f.store->hasOriginal(duplicate));
        QVERIFY(received.open(QIODevice::ReadOnly)); QCOMPARE(received.readAll(), replacement);
        QFile native(f.library->directory + "/native.jpg"); QVERIFY(native.open(QIODevice::ReadOnly));
        QCOMPARE(native.readAll(), QByteArray("native original"));
        QCOMPARE(f.library->imports.load(), 0); QCOMPARE(f.library->removals.load(), 0);
    }
    void localAdditionStaysInTheContainerAndPreservesTheUsersFile() {
        Fixture f; QImage image(4, 4, QImage::Format_RGB32); image.fill(Qt::red);
        QVERIFY(image.save(f.photos() + "/new.png"));
        QVERIFY2(f.store->refresh(), qPrintable(f.store->errorString()));
        QCOMPARE(f.library->imports, 0); QCOMPARE(f.store->catalog().size(), 1);
        QVERIFY(QFile::exists(f.photos() + "/new.png"));
        QVERIFY(f.store->refresh()); QCOMPARE(f.library->imports, 0); QCOMPARE(f.store->catalog().size(), 1);
    }
    void transferChecksHashesAndKeepsCommittedOriginalsInsideTheContainer() {
        Fixture source, target;
        source.library->add("clip.mp4", QByteArray(700000, 'v'), "video"); QVERIFY(source.store->importLibrary());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        QVERIFY(target.store->importLibrary()); QVERIFY(!target.store->hasOriginal(id)); QVERIFY(source.store->prepare(id));
        QJsonObject base{{"id", id}, {"resource", 0}};
        auto request = base; request["action"] = "begin";
        QVERIFY(target.store->command(request).value("ok").toBool());
        for (qint64 offset = 0; offset < 700000; offset += PhotoStore::ChunkBytes) {
            request = base; request["action"] = "read"; request["offset"] = QString::number(offset);
            const auto chunk = source.store->command(request); QVERIFY(chunk.value("ok").toBool());
            request["action"] = "chunk"; request["data"] = chunk.value("data");
            QVERIFY(target.store->command(request).value("ok").toBool());
            QVERIFY(target.store->command(request).value("ok").toBool()); // Replayed acknowledgement.
        }
        request = base; request["action"] = "commit"; QVERIFY(target.store->command(request).value("ok").toBool());
        QVERIFY(target.store->consume(id)); QCOMPARE(target.library->imports, 0);
        QVERIFY(target.store->consume(id)); QVERIFY(target.store->refresh()); QCOMPARE(target.library->imports, 0);
        QCOMPARE(target.store->catalog().size(), 1);
        request["id"] = "../../outside"; QVERIFY(!target.store->command(request).value("ok").toBool());
    }
    void legacyLayoutKeepsNativeReferencesAndOriginals()
    {
        Fixture f; f.library->add("original.jpg", "unchanged original"); QVERIFY(f.store->importLibrary());
        const auto before = f.store->catalog(); const auto id = before.first().toObject().value("id").toString();
        const auto identity = f.store->containerId();
        const auto root = f.temporary.path() + "/drive.societycontainer";
        QFile manifest(root + "/.society-drive.json"); QVERIFY(manifest.open(QIODevice::ReadOnly));
        auto data = QJsonDocument::fromJson(manifest.readAll()).object(); manifest.close();
        QJsonArray legacy;
        for (const auto &entry : data.value("sections").toArray())
            if (entry.toObject().value("id") != "photos") legacy.append(entry);
        data["sections"] = legacy;
        QVERIFY(QDir().rename(root + "/Photos", root + "/Files/Photos"));
        write(manifest.fileName(), QJsonDocument(data).toJson());
        f.store = std::make_unique<PhotoStore>(root, f.library);
        QVERIFY2(f.store->open(), qPrintable(f.store->errorString()));
        QCOMPARE(f.store->containerId(), identity); QCOMPARE(f.store->catalog(), before);
        QVERIFY(f.store->hasOriginal(id)); QVERIFY(QFileInfo::exists(f.store->previewPath(id)));
        QCOMPARE(f.store->previewPath(id), root + "/Photos/.previews/" + id + ".jpg");
        const auto exports = f.library->exports.load();
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.library->exports.load(), exports);
        QCOMPARE(f.library->imports.load(), 0);
        QVERIFY(!QFileInfo::exists(root + "/Files/Photos"));
    }

    void deniedAndLimitedAccessNeverErasePhotos() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->importLibrary());
        const auto records = f.store->catalog(); f.library->snapshot.assets.clear(); f.library->permission = Access::Limited;
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.store->catalog(), records); QCOMPARE(f.library->removals, 0);
        f.library->permission = Access::Denied; QVERIFY(f.store->importLibrary()); QCOMPARE(f.store->catalog(), records);
    }
    void corruptTransferAndRedirectedCacheAreRejected() {
        Fixture source, target; source.library->add("a.jpg", "original"); QVERIFY(source.store->importLibrary());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson()); QVERIFY(target.store->importLibrary());
        QJsonObject request{{"id", id}, {"resource", 0}, {"action", "begin"}};
        QVERIFY(target.store->command(request).value("ok").toBool());
        request["action"] = "chunk"; request["offset"] = "0"; request["data"] = QString::fromLatin1(QByteArray("tampered").toBase64());
        QVERIFY(target.store->command(request).value("ok").toBool());
        request["action"] = "commit"; QVERIFY(!target.store->command(request).value("ok").toBool());
        QCOMPARE(target.library->imports, 0);
        const auto outside = target.temporary.path() + "/untouched"; write(outside, "private");
        const auto cache = target.store->resourcePath(id, 0);
        QVERIFY(QDir().mkpath(QFileInfo(cache).absolutePath())); // The fixture deliberately installs a redirected cache.
        QVERIFY(QFile::link(outside, cache));
        request["action"] = "read"; QVERIFY(!target.store->command(request).value("ok").toBool());
        QFile untouched(outside); QVERIFY(untouched.open(QIODevice::ReadOnly)); QCOMPARE(untouched.readAll(), QByteArray("private"));
    }
    void originalEditUpdatesOneObjectAndTrashIsExplicit() {
        Fixture f; f.library->add("a.jpg", "first"); QVERIFY(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        write(f.library->directory + "/a.jpg", "second"); f.library->snapshot.assets[0].stamp = "2";
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.store->catalog().size(), 1);
        QCOMPARE(f.store->catalog().first().toObject().value("id").toString(), id);
        QVERIFY(f.store->trash(id)); QCOMPARE(f.library->removals, 0);
        QVERIFY(f.store->importLibrary()); QVERIFY(f.store->catalog().first().toObject().value("deleted").toBool());
        QVERIFY(!f.store->hasOriginal(id));
    }
    void unavailableAssetDoesNotBlockOtherPhotos() {
        Fixture f; f.library->add("offline.jpg", "offline"); f.library->add("ready.jpg", "ready");
        QVERIFY(QFile::remove(f.library->directory + "/offline.jpg"));
        QVERIFY(!f.store->importLibrary()); QCOMPARE(f.store->catalog().size(), 1);
        QCOMPARE(f.store->catalog().first().toObject().value("name").toString(), QString("ready.jpg"));
    }
    void mediaDatabaseReplacementDoesNotBecomeGlobalDeletion() {
        Fixture f; f.library->snapshot.revision = "first-volume";
        f.library->add("a.jpg", "original"); QVERIFY(f.store->importLibrary());
        const auto records = f.store->catalog(); f.library->snapshot.assets.clear();
        f.library->snapshot.revision = "replacement-volume";
        QVERIFY(f.store->importLibrary()); QVERIFY(f.store->importLibrary());
        QCOMPARE(f.store->catalog(), records); QCOMPARE(f.library->removals, 0);
    }
    void anotherTransferKeepsItsOriginalLease() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto a = QUuid::createUuid().toString(), b = QUuid::createUuid().toString();
        auto request = QJsonObject{{"id", id}, {"action", "prepare"}, {"lease", a}};
        QVERIFY(f.store->command(request).value("ok").toBool());
        request["lease"] = b; QVERIFY(f.store->command(request).value("ok").toBool());
        request["action"] = "release"; request["lease"] = a;
        QVERIFY(f.store->command(request).value("ok").toBool());
        QVERIFY(QFile::exists(f.store->resourcePath(id, 0)));
        request["action"] = "read"; request["lease"] = b; request["resource"] = 0; request["offset"] = "0";
        QVERIFY(f.store->command(request).value("ok").toBool());
        request["action"] = "release"; QVERIFY(f.store->command(request).value("ok").toBool());
        QVERIFY(!QFile::exists(f.store->resourcePath(id, 0)));
    }
    void expiredViewingCopiesAreRemovedWithoutTouchingNativeOriginals() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto path = f.store->originalForViewing(id); QVERIFY(!path.isEmpty());
        QFile copy(path); QVERIFY(copy.open(QIODevice::ReadWrite));
        QVERIFY(copy.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime)); copy.close();
        QVERIFY(f.store->importLibrary()); QVERIFY(!QFile::exists(path));
        QFile original(f.library->directory + "/a.jpg"); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("original"));
    }
    void unchangedRefreshDoesNotStartAnotherFileSync() {
        Fixture f; f.library->add("a.jpg", "original");
        PhotoController controller({}, nullptr, f.library);
        QSignalSpy contents(&controller, &PhotoController::contentsChanged);
        QSignalSpy entries(&controller, &PhotoController::entriesChanged);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(contents.count(), 0); QVERIFY(controller.entries().isEmpty());
        controller.connectLibrary(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(contents.count(), 1); QVERIFY(entries.count() >= 1);
        const auto initialUpdates = entries.count();
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(contents.count(), 1); QCOMPARE(entries.count(), initialUpdates);
    }
    void partialNativeImportCannotReplaceACompleteAlias() {
        Fixture f; f.library->add("still.jpg", "still"); f.library->add("motion.mov", "motion", "video");
        f.library->snapshot.assets[0].resources.append({"motion.mov", "motion.mov", "pairedVideo"});
        f.library->snapshot.assets.removeLast(); QVERIFY(f.store->importLibrary());
        const auto record = f.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        auto &asset = f.library->snapshot.assets[0]; asset.resources.removeLast(); asset.stamp = "partial";
        asset.resources[0].name = importedResourceName(id, 0, "still.jpg");
        QVERIFY(!f.store->importLibrary()); QCOMPARE(f.store->catalog().first().toObject(), record);
        QVERIFY(!f.store->hasOriginal(id));
    }
    void remoteResourceVersionIsNotOverwrittenByAnUnchangedNativeCopy() {
        Fixture f; f.library->add("a.jpg", "first"); QVERIFY(f.store->importLibrary());
        auto record = f.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        auto resource = record.value("resources").toArray().first().toObject();
        resource["hash"] = QString(64, 'a'); record["resources"] = QJsonArray{resource};
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        QVERIFY(f.store->importLibrary()); QCOMPARE(f.store->catalog().first().toObject(), record);
        QVERIFY(!f.store->hasOriginal(id));
    }
    void completedRequestCacheDoesNotStopLongTransfers() {
        Fixture f; f.library->add("a.jpg", "original"); QVERIFY(f.store->importLibrary());
        const auto record = f.store->catalog().first().toObject();
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true, {"peer"});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        for (int i = 0; i < 80; ++i) {
            const QJsonObject request{{"op", "society.photos"}, {"container", f.store->containerId()},
                {"ticket", QUuid::createUuid().toString()}, {"action", "available"}, {"id", record.value("id")}};
            QJsonObject reply;
            QTRY_VERIFY_WITH_TIMEOUT(!(reply = controller.handle("peer", request)).value("pending").toBool(), 5000);
            QVERIFY2(reply.value("ok").toBool(), qPrintable(reply.value("error").toString()));
        }
    }
    void galleryOrdersPhotosAndVideosFromOldestToNewest() {
        Fixture f;
        const auto epoch = QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
        f.library->add("A-newest.mov", "newest video", "video");
        f.library->snapshot.assets.last().created = epoch.addDays(3);
        f.library->add("Z-oldest.jpg", "oldest photo");
        f.library->snapshot.assets.last().created = epoch;
        f.library->add("same-time-photo.jpg", "same time photo");
        f.library->snapshot.assets.last().created = epoch.addDays(1);
        f.library->add("same-time-video.mp4", "same time video", "video");
        f.library->snapshot.assets.last().created = epoch.addDays(1);
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY(!controller.busy()); controller.connectLibrary();
        QTRY_VERIFY(!controller.busy());
        const auto entries = controller.entries(); QCOMPARE(entries.size(), 4);
        QCOMPARE(entries.first().toMap().value("name").toString(), QString("Z-oldest.jpg"));
        QCOMPARE(entries.last().toMap().value("name").toString(), QString("A-newest.mov"));
        QCOMPARE(entries.last().toMap().value("media").toString(), QString("video"));
        QCOMPARE(entries[1].toMap().value("created"), entries[2].toMap().value("created"));
        QVERIFY(entries[1].toMap().value("galleryKey").toString() < entries[2].toMap().value("galleryKey").toString());
    }
    void chronologicalGalleryOpensAtNewestAndPreservesBrowsing_data() {
        QTest::addColumn<QSize>("viewport");
        QTest::newRow("mobile") << QSize(390, 844);
        QTest::newRow("desktop") << QSize(960, 720);
    }
    void chronologicalGalleryOpensAtNewestAndPreservesBrowsing() {
        QFETCH(QSize, viewport);
        Fixture f;
        const auto epoch = QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
        const auto add = [&](int index) {
            const auto name = QString("Photo-%1.%2").arg(99 - index, 2, 10, QChar('0')).arg(index % 2 ? "mov" : "jpg");
            f.library->add(name, QByteArray("timeline-") + QByteArray::number(index), index % 2 ? "video" : "photo");
            f.library->snapshot.assets.last().created = epoch.addDays(index);
        };
        for (int i = 0; i < 40; ++i) add(i);
        QVERIFY(f.store->importLibrary());
        PhotoController controller({}, nullptr, f.library);
        QQmlApplicationEngine engine; engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_QML_IMPORT_PATH)); engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)},
            {"width", viewport.width()}, {"height", viewport.height()}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(IIPHOTOLIBRARY_QML_FILE)));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *view = visualItem(window->contentItem(), "photosView"); QVERIFY(view);
        auto *grid = visualItem(view, "photoGrid"); QVERIFY(grid);
        view->setProperty("touchNavigation", viewport.width() < 600);
        QCOMPARE(grid->property("count").toInt(), 0);

        // Data arrives after the empty view is already laid out.
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_COMPARE(grid->property("count").toInt(), 40);
        auto *importLibrary = visualItem(view, "connectPhotoLibrary"); QVERIFY(importLibrary);
        QVERIFY(importLibrary->isVisible());
        QVERIFY(grid->property("cacheBuffer").toReal() >= grid->height() * 2);
        QTRY_VERIFY(grid->property("atYEnd").toBool());
        QVERIFY(grid->property("contentY").toReal() > grid->property("originY").toReal());
        const auto settled = [&] {
            const auto pending = view->property("pendingViewState");
            return !controller.busy() && !view->property("initialPositionPending").toBool()
                && (pending.isNull() || pending.value<QJSValue>().isNull());
        };
        // This is a state assertion, not a five-second storage benchmark.
        // External-volume fixture I/O exceeded 5 s while the UI was already ready.
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 15000);
        QVERIFY(QDir().mkpath(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos")));
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos/chronological-%1.png")
            .arg(QString::fromLatin1(QTest::currentDataTag()))));

        // Checking unchanged content does not replace rows or move the camera.
        const auto endScroll = grid->property("contentY").toReal();
        QSignalSpy entriesChanged(&controller, &PhotoController::entriesChanged);
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(settled(), 15000);
        QCOMPARE(entriesChanged.size(), 0);
        QCOMPARE(grid->property("contentY").toReal(), endScroll);
        // New arrivals preserve the camera even when it was at the end.
        for (int i = 40; i < 43; ++i) add(i);
        controller.connectLibrary(); QTRY_COMPARE(grid->property("count").toInt(), 43); QTRY_VERIFY_WITH_TIMEOUT(settled(), 15000);
        QTRY_COMPARE(grid->property("contentY").toReal(), endScroll);
        grid->setProperty("contentY", grid->property("originY").toReal() + grid->property("cellHeight").toReal());
        QTest::qWait(100);
        const auto scroll = grid->property("contentY").toReal();
        QVERIFY(scroll > grid->property("originY").toReal() && !grid->property("atYEnd").toBool());
        const auto selected = controller.entries()[8].toMap().value("id").toString();
        view->setProperty("selectedId", selected);
        for (int i = 43; i < 50; ++i) add(i);
        controller.connectLibrary(); QTRY_COMPARE(grid->property("count").toInt(), 50); QTRY_VERIFY_WITH_TIMEOUT(settled(), 15000);
        QCOMPARE(grid->property("contentY").toReal(), scroll);
        QCOMPARE(view->property("selectedId").toString(), selected);

        // A changed capture date can reorder the same number of entries.
        const auto movedId = controller.entries().first().toMap().value("id").toString();
        QFile alias(f.photos() + '/' + movedId + ".societyphoto"); QVERIFY(alias.open(QIODevice::ReadOnly));
        auto record = QJsonDocument::fromJson(alias.readAll()).object(); alias.close();
        record["created"] = epoch.addDays(100).toString(Qt::ISODateWithMs);
        write(alias.fileName(), QJsonDocument(record).toJson());
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(settled(), 15000);
        QCOMPARE(controller.entries().last().toMap().value("id").toString(), movedId);
        QCOMPARE(grid->property("contentY").toReal(), scroll);
        QCOMPARE(view->property("selectedId").toString(), selected);

        // Storage keeps the view alive: re-entry still starts at the newest.
        view->setVisible(false); view->setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 15000); QTRY_VERIFY(grid->property("atYEnd").toBool());
        view->setVisible(false);
        controller.configure({}, false); QTRY_COMPARE(grid->property("count").toInt(), 0);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_COMPARE(grid->property("count").toInt(), 50); QTRY_VERIFY(!controller.busy());
        view->setVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 15000); QTRY_VERIFY(grid->property("atYEnd").toBool());
        QCOMPARE(view->property("selectedId").toString(), QString());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void galleryRendersAndLatePreviewKeepsBrowsingPosition() {
        Fixture f;
        for (int i = 0; i < 40; ++i) f.library->add(QString("Photo %1.jpg").arg(i), QByteArray("original-") + QByteArray::number(i));
        QVERIFY(f.store->importLibrary()); const auto records = f.store->catalog();
        const auto id = records.first().toObject().value("id").toString();
        QFile image(f.store->previewPath(id)); QVERIFY(image.open(QIODevice::ReadOnly)); const auto preview = image.readAll(); image.close();
        QVERIFY(image.remove()); f.library->permission = Access::Denied;
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QQmlApplicationEngine engine; engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_QML_IMPORT_PATH)); engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(IIPHOTOLIBRARY_QML_FILE)));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *view = visualItem(window->contentItem(), "photosView"); QVERIFY(view);
        auto *grid = visualItem(view, "photoGrid"); QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 40);
        QVERIFY(grid->property("cacheBuffer").toReal() >= grid->height() * 2);
        QTRY_VERIFY(!view->property("initialPositionPending").toBool());
        QVERIFY(QMetaObject::invokeMethod(grid, "positionViewAtBeginning")); QTest::qWait(100);
        view->setProperty("selectedId", id);
        QSignalSpy contentChanges(&controller, &PhotoController::contentsChanged);
        QSignalSpy resets(controller.galleryModel(), &QAbstractItemModel::modelReset);
        QSignalSpy updates(controller.galleryModel(), &QAbstractItemModel::dataChanged);
        write(f.store->previewPath(id), preview);
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QQuickItem *loaded = nullptr;
        QTRY_VERIFY((loaded = visualItem(grid, "photoPreview-" + id)) != nullptr);
        QTRY_COMPARE(loaded->property("status").toInt(), 1); // Image.Ready
        QVERIFY(loaded->property("cache").toBool());
        QCOMPARE(contentChanges.count(), 0);
        QCOMPARE(resets.count(), 0); QVERIFY(updates.count() > 0);
        grid->setProperty("contentY", 250.0); QTest::qWait(100);
        const auto scroll = grid->property("contentY").toReal();
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(view->property("selectedId").toString(), id);
        QCOMPARE(grid->property("contentY").toReal(), scroll);
        auto *info = view->findChild<QObject *>("galleryInfo"); QVERIFY(info);
        QVERIFY(!info->property("visible").toBool());
        grid->setProperty("contentY", grid->property("originY")); QTest::qWait(100);
        auto *tile = visualItem(grid, "galleryTile"); QVERIFY(tile);
        QCOMPARE(tile->width(), tile->height());
        QCOMPARE(grid->property("cellWidth").toReal() - tile->width(), 2.0);
        QCOMPARE(tile->property("text").toString(), QString());
        auto *touchDevice = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        const auto center = tile->mapToScene(QPointF(tile->width()/2, tile->height()/2)).toPoint();
        QTest::touchEvent(window, touchDevice).press(0, center).commit();
        QTest::touchEvent(window, touchDevice).release(0, center).commit();
        QTRY_VERIFY(info->property("visible").toBool());
        QCOMPARE(info->property("fileName").toString(), tile->property("name").toString());
        QVERIFY(QDir().mkpath(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos")));
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos/information-desktop.png")));
        QVERIFY(QMetaObject::invokeMethod(info, "close")); QTRY_VERIFY(!info->property("visible").toBool()); QTest::qWait(250);
        const auto beforePinch = grid->property("cellWidth").toReal();
        const auto middle = grid->mapToScene(QPointF(grid->width()/2, grid->height()/2)).toPoint();
        QTest::touchEvent(window, touchDevice).press(0, middle-QPoint(40,0)).press(1, middle+QPoint(40,0)).commit();
        for (int distance = 50; distance <= 120; distance += 10) {
            QTest::touchEvent(window, touchDevice).move(0, middle-QPoint(distance,0)).move(1, middle+QPoint(distance,0)).commit(); QTest::qWait(20);
        }
        QTest::touchEvent(window, touchDevice).release(0, middle-QPoint(120,0)).release(1, middle+QPoint(120,0)).commit();
        QCOMPARE(grid->property("cellWidth").toReal(), beforePinch);
        QCOMPARE(grid->property("cellWidth"), grid->property("cellHeight"));
        QVERIFY(!info->property("visible").toBool());
        const auto enlarged = grid->property("cellWidth").toReal();
        QTest::touchEvent(window, touchDevice).press(0, middle-QPoint(120,0)).press(1, middle+QPoint(120,0)).commit();
        for (int distance = 110; distance >= 40; distance -= 10) {
            QTest::touchEvent(window, touchDevice).move(0, middle-QPoint(distance,0)).move(1, middle+QPoint(distance,0)).commit(); QTest::qWait(20);
        }
        QTest::touchEvent(window, touchDevice).release(0, middle-QPoint(40,0)).release(1, middle+QPoint(40,0)).commit();
        QCOMPARE(grid->property("cellWidth").toReal(), enlarged);
        QVERIFY(!info->property("visible").toBool());
        auto *zoom = view->findChild<QObject *>("galleryZoom"); QVERIFY(zoom);
        zoom->setProperty("thumbnailSize", 128.0);
        view->setProperty("selectedId", id);
        QVERIFY(QDir().mkpath(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos")));
        QTest::qWait(100); QVERIFY(window->grabWindow().save(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos/gallery-desktop.png")));
        window->resize(390, 844); QTest::qWait(100);
        const auto *add = visualItem(view, "addPhotos"); QVERIFY(add);
        QVERIFY(add->mapToItem(view, QPointF(add->width(), 0)).x() <= view->width());
        QVERIFY(window->grabWindow().save(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY "/photos/gallery-mobile.png")));
        auto deleted = records.first().toObject(); deleted["deleted"] = true;
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(deleted).toJson());
        controller.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QTRY_COMPARE(grid->property("count").toInt(), 39);
        QCOMPARE(view->property("selectedId").toString(), QString());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void authenticatedExchangeUploadsLocalAndDownloadsOnlySelectedOriginal() {
        Fixture host, client;
        const auto hostDrive = iiSocietyContainer::SocietyDrive::open(host.temporary.path() + "/drive.societycontainer");
        const auto clientDrive = iiSocietyContainer::SocietyDrive::open(client.temporary.path() + "/drive.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(clientDrive->rootPath(), clientDrive->identifier(), hostDrive->identifier()));
        QVERIFY(iiSocietyContainer::SocietyDrive::completeReplica(clientDrive->rootPath(), hostDrive->identifier()));
        client.store = std::make_unique<PhotoStore>(clientDrive->rootPath(), client.library); QVERIFY(client.store->open());
        host.library->add("host.jpg", QByteArray(530000, 'h')); client.library->add("phone.mp4", QByteArray(720000, 'p'), "video");
        QVERIFY(host.store->importLibrary()); QVERIFY(client.store->importLibrary());
        const auto hostRecord = host.store->catalog().first().toObject(), clientRecord = client.store->catalog().first().toObject();
        // Portable aliases use the ordinary container sync path. Originals use
        // the authenticated, resumable photo channel tested below.
        write(client.photos() + '/' + hostRecord.value("id").toString() + ".societyphoto", QJsonDocument(hostRecord).toJson());
        write(host.photos() + '/' + clientRecord.value("id").toString() + ".societyphoto", QJsonDocument(clientRecord).toJson());
        host.store->releaseExports(); client.store->releaseExports();
        PhotoController receiver([](const auto &, const auto &) { return QString(); }, nullptr, host.library);
        PhotoController *clientController = nullptr;
        int requests = 0;
        PhotoController sender([&](const QString &peer, const QJsonObject &packet) {
            ++requests; const auto ticket = QString::number(requests);
            const auto reply = peer == "host" ? receiver.handle("client", packet) : QJsonObject{};
            QTimer::singleShot(0, &receiver, [&, ticket, reply] { clientController->receive(ticket, reply); });
            return ticket;
        }, nullptr, client.library);
        clientController = &sender;
        receiver.configure(hostDrive->rootPath(), true, {"client"});
        QTRY_VERIFY_WITH_TIMEOUT(!receiver.busy(), 10000);
        const QJsonObject packet{{"op", "society.photos"}, {"container", hostDrive->identifier()},
            {"ticket", QUuid::createUuid().toString()}, {"action", "available"}, {"id", hostRecord.value("id")}};
        QVERIFY(!receiver.handle("stranger", packet).value("ok").toBool());
        int idleTransitions = 0;
        connect(&sender, &PhotoController::changed, this, [&] {
            if (!sender.busy()) ++idleTransitions;
        });
        sender.configure(clientDrive->rootPath(), true, {"host"}, {"host"});
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(host.store->resourcePath(clientRecord.value("id").toString(), 0)), 20000);
        QCOMPARE(host.library->imports.load(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!sender.busy(), 10000);
        // The local catalog refresh and post-import refresh are part of the
        // same transfer. A background owner must see only the final idle edge.
        QCOMPARE(idleTransitions, 1);
        QCOMPARE(client.library->imports.load(), 0);
        QVERIFY(client.store->originalForViewing(hostRecord.value("id").toString()).isEmpty());
        sender.downloadPhoto(hostRecord.value("id").toString());
        QTRY_VERIFY_WITH_TIMEOUT(!sender.busy(), 10000);
        QVERIFY(!client.store->originalForViewing(hostRecord.value("id").toString()).isEmpty());
        QCOMPARE(client.library->imports.load(), 0);
        QVERIFY(client.store->importLibrary());
        QCOMPARE(client.library->imports.load(), 0);
        QVERIFY(requests > 10);
        receiver.configure({}, false); sender.configure({}, false);
    }
    void nativeLibrariesExchangeAliasesPreviewsAndOriginalsOverTls() {
        Fixture desktop, phone;
        desktop.library->add("desktop.jpg", QByteArray(600000, 'd'));
        phone.library->add("phone.mp4", QByteArray(800000, 'p'), "video");
        QVERIFY(desktop.store->importLibrary());
        const auto desktopRecord = desktop.store->catalog().first().toObject();
        iiWebServer::LanPeer host, client;
        iiSocietyHelper::Sync::Controller hosting({}), syncing([&](const auto &peer, const auto &packet) { return client.request(peer, packet); });
        PhotoController hostPhotos({}, nullptr, desktop.library);
        PhotoController phonePhotos([&](const auto &peer, const auto &packet) { return client.request(peer, packet); }, nullptr, phone.library);
        const auto files = iiSocietyHelper::Sync::filesHandler(desktop.temporary.path() + "/drive.societycontainer");
        connect(&client, &iiWebServer::LanPeer::completed, this, [&](const auto &id, const auto &result, const auto &transport) {
            QCOMPARE(transport, QString("local")); syncing.receive(id, result); phonePhotos.receive(id, result);
        });
        QVERIFY(host.startHost("desktop", "Desktop", [&](const auto &peer, const auto &packet) {
            if (packet.value("op") == "society.photos") return hostPhotos.handle(peer, packet);
            return packet.value("op") == "society.sync" ? hosting.handle(peer, packet) : files(peer, packet);
        }, {"127.0.0.1"}, iiWebServer::Address::loopback()));
        QVERIFY(client.join(host.createOffer(), "phone", "Phone")); QTRY_VERIFY2(client.connected(), qPrintable(client.errorString()));
        hosting.open(desktop.temporary.path() + "/drive.societycontainer", QString(64, 'a'));
        syncing.open(phone.temporary.path() + "/drive.societycontainer", QString(64, 'a'));
        QTRY_VERIFY(hosting.available() && syncing.available());
        hosting.setPeers({"phone"}, {});
        QSignalSpy mirrored(&syncing, &iiSocietyHelper::Sync::Controller::synchronized);
        syncing.setPeers({"desktop"}, {"desktop"});
        QTRY_VERIFY2_WITH_TIMEOUT(!mirrored.isEmpty(), qPrintable(syncing.errorString()), 15000);
        const auto desktopId = desktopRecord.value("id").toString();
        // The host index publishes metadata in batches. The first successful
        // cycle can contain only its directories, before the alias is indexed.
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(phone.photos() + '/' + desktopId + ".societyphoto"), 15000);
        phone.store = std::make_unique<PhotoStore>(phone.temporary.path() + "/drive.societycontainer", phone.library); QVERIFY(phone.store->open());
        QVERIFY(phone.store->importLibrary());
        QJsonObject phoneRecord;
        for (const auto &value : phone.store->catalog())
            if (value.toObject().value("id") != desktopId) phoneRecord = value.toObject();
        QVERIFY(!phoneRecord.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(PhotoStore::digestFile(phone.store->previewPath(desktopId)), PhotoStore::digestFile(desktop.store->previewPath(desktopId)), 15000);
        connect(&hostPhotos, &PhotoController::contentsChanged, &hosting, &iiSocietyHelper::Sync::Controller::synchronizeNow);
        connect(&phonePhotos, &PhotoController::contentsChanged, &syncing, &iiSocietyHelper::Sync::Controller::synchronizeNow);
        connect(&syncing, &iiSocietyHelper::Sync::Controller::synchronized, &phonePhotos, &PhotoController::refresh);
        hostPhotos.configure(desktop.temporary.path() + "/drive.societycontainer", true, {"phone"});
        QTRY_VERIFY_WITH_TIMEOUT(!hostPhotos.busy(), 5000);
        phonePhotos.configure(phone.temporary.path() + "/drive.societycontainer", true, {"desktop"}, {"desktop"});
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(desktop.store->resourcePath(phoneRecord.value("id").toString(), 0)), 20000);
        QCOMPARE(desktop.library->imports.load(), 0);
        QCOMPARE(phone.library->imports.load(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!phonePhotos.busy(), 10000);
        phonePhotos.downloadPhoto(desktopId);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(phone.temporary.path() + "/drive.societycontainer/.society-photos/"
            + iiSocietyContainer::SocietyDrive::open(phone.temporary.path() + "/drive.societycontainer")->identifier()
            + "/originals/" + desktopRecord.value("resources").toArray().first().toObject().value("hash").toString()), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!phonePhotos.busy(), 10000);
        QCOMPARE(desktop.library->exports.load() > 0, true);
        QCOMPARE(phone.library->exports.load() > 0, true);
        phonePhotos.trashPhoto(desktopId);
        QCOMPARE(phone.library->removals.load(), 0);
        const auto remotelyDeleted = [&] {
            QFile file(desktop.photos() + '/' + desktopId + ".societyphoto");
            return file.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(file.readAll()).object().value("deleted").toBool();
        };
        QTRY_VERIFY_WITH_TIMEOUT(remotelyDeleted(), 10000);
        hostPhotos.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!hostPhotos.busy(), 5000);
        QCOMPARE(desktop.library->removals.load(), 0);
        phonePhotos.configure({}, false); hostPhotos.configure({}, false);
        syncing.closeAndWait(); hosting.closeAndWait(); client.stop(); host.stop();
    }
};
int main(int argc, char **argv) {
    lvrs::AppBootstrapOptions options;
    options.applicationName = "iiPhotoLibraryTests"; options.quickStyleName = "Basic";
    options.bootstrapGraphicsBackend = false; options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false; options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
    QGuiApplication app(argc, argv); lvrs::postApplicationBootstrap(app, options);
    PhotosTest test; return QTest::qExec(&test, argc, argv);
}
#include "tst_photos.moc"
