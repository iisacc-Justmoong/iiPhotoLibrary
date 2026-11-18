#include <iiPhotoLibrary/PhotoStore.h>
#include <iiPhotoLibrary/PhotoController.h>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QImage>
#include <QPainter>
#include <QLinearGradient>
#include <QJsonDocument>
#include <QTemporaryDir>
#include "CTestSupport.h"
#include <QElapsedTimer>
#include <QSemaphore>
#include <QScopeGuard>
#include <QLockFile>
#include <QCryptographicHash>
#include <QGuiApplication>
#ifdef IIPHOTOLIBRARY_TEST_LEGACY_QML
#include "Legacy/TouchSequence.h"
#include <QPointingDevice>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include "backend/runtime/appbootstrap.h"
#endif
#include <iiSocietyHelper/Sync/Sync.h>
#include <thread>

using namespace iiPhotoLibrary;
namespace {
#ifdef IIPHOTOLIBRARY_TEST_LEGACY_QML
QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name) return parent;
    for (auto *child : parent->childItems()) if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}
#endif
bool tryWrite(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void write(const QString &path, const QByteArray &bytes) {
    CTEST_CHECK(tryWrite(path, bytes));
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
public:
    void finalShutdownDoesNotWaitForABlockedPhotoOperation() {
        Fixture f;
        f.library->add("slow.jpg", "fixture original");
        const auto baselineOwners = f.library.use_count();
        auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
        const auto unblock = qScopeGuard([release] { release->release(); });
        std::jthread watchdog([release](std::stop_token stop) {
            for (int i = 0; i < 300 && !stop.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!stop.stop_requested()) release->release();
        });
        f.library->beforePreview = [entered, release](const QString &) { entered->release(); release->acquire(); };
        auto controller = std::make_unique<PhotoController>(PhotoController::Sender{}, nullptr, f.library);
        controller->configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_VERIFY(!controller->busy(), 10000);
        controller->connectLibrary();
        CTEST_WAIT_VERIFY(entered->available() > 0, 10000);
        QElapsedTimer elapsed; elapsed.start();
        controller->shutdownAsync();
        controller.reset();
        CTEST_CHECK_MESSAGE(elapsed.elapsed() < 500, "Photo shutdown waited for a blocked operation");
        release->release();
        CTEST_WAIT_COMPARE_TIMEOUT(f.library.use_count(), baselineOwners, 10000);
        QCoreApplication::processEvents();
        QFile original(f.library->directory + "/slow.jpg");
        CTEST_CHECK(original.open(QIODevice::ReadOnly)); CTEST_COMPARE(original.readAll(), QByteArray("fixture original"));
    }
    void fixtureRootIsProjectResources() {
        const QFileInfo root(QStringLiteral(IIPHOTOLIBRARY_TEST_DIRECTORY));
        CTEST_COMPARE(root.fileName(), QStringLiteral("resources"));
        CTEST_CHECK(root.isDir());
        QTemporaryDir temporary(root.absoluteFilePath() + "/photos-test-XXXXXX");
        CTEST_CHECK(temporary.isValid());
    }
    void defaultRefreshIgnoresUnselectedNativePhotosAndPreservesContainerFiles() {
        Fixture f; f.library->add("outside.jpg", "native library original");
        const auto root = f.temporary.path() + "/drive.societycontainer";
        for (int attempt = 0; attempt < 3; ++attempt) {
            CTEST_CHECK(f.store->refresh()); CTEST_CHECK(f.store->catalog().isEmpty());
            CTEST_CHECK(!QFileInfo::exists(root + "/.society-photos"));
        }
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(true); controller.configure(root, true);
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_CHECK(controller.entries().isEmpty());
        CTEST_COMPARE(f.library->authorizations, 0); CTEST_COMPARE(f.library->scans.load(), 0);
        CTEST_COMPARE(f.library->exports.load(), 0); CTEST_COMPARE(f.library->previews.load(), 0);
        QImage image(4, 4, QImage::Format_RGB32); image.fill(Qt::green);
        const auto supplied = f.photos() + "/user.png"; CTEST_CHECK(image.save(supplied));
        const auto before = PhotoStore::digestFile(supplied);
        CTEST_CHECK(f.store->refresh()); CTEST_COMPARE(f.store->catalog().size(), 1);
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        CTEST_CHECK(f.store->hasOriginal(id)); CTEST_CHECK(f.store->consume(id));
        CTEST_COMPARE(PhotoStore::digestFile(supplied), before);
        CTEST_CHECK(f.store->trash(id)); CTEST_CHECK(f.store->refresh());
        CTEST_COMPARE(PhotoStore::digestFile(supplied), before);
        CTEST_COMPARE(f.library->scans.load(), 0); CTEST_COMPARE(f.library->imports.load(), 0);
        CTEST_COMPARE(f.library->removals.load(), 0); CTEST_COMPARE(f.library->snapshot.assets.size(), 1);
        QFile external(f.library->directory + "/outside.jpg"); CTEST_CHECK(external.open(QIODevice::ReadOnly));
        CTEST_COMPARE(external.readAll(), QByteArray("native library original"));
    }
    void explicitLibraryImportDoesNotAdoptLaterNativePhotosDuringRefresh() {
        Fixture f; f.library->add("selected.jpg", "selected original");
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.store->catalog().size(), 1);
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto scans = f.library->scans.load(), exports = f.library->exports.load();
        f.library->add("later.jpg", "not selected later original");
        CTEST_CHECK(f.store->refresh()); CTEST_COMPARE(f.store->catalog().size(), 1);
        CTEST_COMPARE(f.library->scans.load(), scans); CTEST_COMPARE(f.library->exports.load(), exports);
        CTEST_CHECK(f.store->prepare(id));
        QFile original(f.store->resourcePath(id, 0)); CTEST_CHECK(original.open(QIODevice::ReadOnly));
        CTEST_COMPARE(original.readAll(), QByteArray("selected original"));
        CTEST_CHECK(f.store->trash(id)); CTEST_CHECK(f.store->importLibrary());
        CTEST_COMPARE(f.library->removals.load(), 0); CTEST_COMPARE(f.library->imports.load(), 0);
        CTEST_COMPARE(f.library->snapshot.assets.size(), 2);
        bool deleted = false;
        for (const auto &value : f.store->catalog())
            if (value.toObject().value("id") == id) deleted = value.toObject().value("deleted").toBool();
        CTEST_CHECK(deleted);
    }
    void permissionGrantedForAnOldContainerDoesNotImportIntoItsReplacement() {
        Fixture first, second; first.library->permission = Access::NotDetermined;
        first.library->add("not-selected.jpg", "native source");
        PhotoController controller({}, nullptr, first.library);
        controller.configure(first.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_DEFAULT(!controller.busy()); controller.connectLibrary();
        CTEST_COMPARE(first.library->authorizations, 1); CTEST_CHECK(controller.busy());
        auto granted = std::exchange(first.library->authorizationFinished, {});
        CTEST_CHECK(bool(granted));
        controller.configure(second.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_DEFAULT(!controller.busy()); first.library->permission = Access::Full;
        granted(); QCoreApplication::processEvents();
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_CHECK(controller.entries().isEmpty());
        CTEST_COMPARE(first.library->scans.load(), 0); CTEST_COMPARE(first.library->exports.load(), 0);
        CTEST_CHECK(!QFileInfo::exists(second.temporary.path() + "/drive.societycontainer/.society-photos"));
    }
    void openingAndReadingAnEmptyStoreCreatesNoPhotoDirectories() {
        Fixture f;
        const auto root = f.temporary.path() + "/drive.societycontainer";
        const auto privateRoot = root + "/.society-photos";
        const auto id = QString(64, 'a');
        const auto before = QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (int attempt = 0; attempt < 3; ++attempt) {
            CTEST_COMPARE(f.store->catalog(), QJsonArray{});
            CTEST_COMPARE(f.store->gallery(), QJsonArray{});
            CTEST_CHECK(!f.store->hasOriginal(id));
            CTEST_CHECK(f.store->resourcePath(id, 0).isEmpty());
            CTEST_COMPARE(f.store->previewPath(id), f.photos() + "/.previews/" + id + ".jpg");
            CTEST_CHECK(!f.store->command({{"id", id}, {"action", "available"}}).value("ok").toBool());
            f.store = std::make_unique<PhotoStore>(root, f.library);
            bool published = false, publicationValid = true;
            CTEST_CHECK(f.store->open([&](const QJsonArray &rows, bool reset) {
                published = true; publicationValid &= rows.isEmpty() && reset;
            }));
            CTEST_CHECK(published); CTEST_CHECK(publicationValid);
            CTEST_CHECK(!QFileInfo::exists(privateRoot));
            CTEST_CHECK(!QFileInfo::exists(f.photos() + "/.previews"));
            CTEST_COMPARE(QDir(root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), before);
        }
        CTEST_COMPARE(f.library->exports.load(), 0); CTEST_COMPARE(f.library->previews.load(), 0);
    }
    void photoDiscoveryAndOriginalPreparationCreateOnlyRequiredParents() {
        Fixture f; const QByteArray original("original bytes");
        f.library->add("first.jpg", original);
        const auto privateRoot = f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId();
        CTEST_CHECK(!QFileInfo::exists(privateRoot));
        CTEST_CHECK_MESSAGE(f.store->importLibrary(), qPrintable(f.store->errorString()));
        CTEST_COMPARE(f.library->scans.load(), 1); // Preflight discovery is reused by the writer.
        const auto record = f.store->catalog().first().toObject();
        const auto id = record.value("id").toString();
        for (const auto &path : {privateRoot + "/gallery", privateRoot + "/references", privateRoot + "/incoming", f.photos() + "/.previews"})
            CTEST_CHECK_MESSAGE(QFileInfo(path).isDir(), qPrintable(path));
        CTEST_CHECK(!QFileInfo::exists(privateRoot + "/originals"));
        CTEST_CHECK(!QFileInfo::exists(privateRoot + "/viewing"));
        // Simulate external removal of a payload-empty staging directory.
        // Provider lease sentinels are persistent control files, not cached originals.
        for (const auto &name : QDir(privateRoot + "/incoming").entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) CTEST_COMPARE(name, QStringLiteral(".iisacc-temporary.lock"));
        CTEST_CHECK(QDir(privateRoot + "/incoming").removeRecursively());
        CTEST_CHECK_MESSAGE(f.store->prepare(id), qPrintable(f.store->errorString()));
        QFile cached(f.store->resourcePath(id, 0)); CTEST_CHECK(cached.open(QIODevice::ReadOnly));
        CTEST_COMPARE(cached.readAll(), original); cached.close();
        CTEST_CHECK(QFileInfo(privateRoot + "/originals").isDir());
        CTEST_CHECK(!QFileInfo::exists(privateRoot + "/viewing"));
        CTEST_CHECK(cached.remove());
        for (const auto &name : QDir(privateRoot + "/originals").entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) CTEST_COMPARE(name, QStringLiteral(".iisacc-temporary.lock"));
        CTEST_CHECK(QDir(privateRoot + "/originals").removeRecursively());
        for (const auto &name : QDir(privateRoot + "/incoming").entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) CTEST_COMPARE(name, QStringLiteral(".iisacc-temporary.lock"));
        CTEST_CHECK(QDir(privateRoot + "/incoming").removeRecursively());
        CTEST_CHECK_MESSAGE(f.store->prepare(id), qPrintable(f.store->errorString()));
        CTEST_COMPARE(PhotoStore::digestFile(f.store->resourcePath(id, 0)), record.value("resources").toArray().first().toObject().value("hash").toString());
    }
    void emptyRefreshAndAutomaticConnectionDoNotPersistPhotoState() {
        for (const auto permission : {Access::NotDetermined, Access::Denied, Access::Unsupported, Access::Full, Access::Limited}) {
            Fixture f; f.library->permission = permission;
            const auto root = f.temporary.path() + "/drive.societycontainer";
            for (int attempt = 0; attempt < 3; ++attempt) {
                bool published = false, publicationValid = true;
                CTEST_CHECK_MESSAGE(f.store->refresh({}, [&](const QJsonArray &rows, bool reset) {
                    published = true; publicationValid &= rows.isEmpty() && reset;
                }), qPrintable(f.store->errorString()));
                CTEST_CHECK(published); CTEST_CHECK(publicationValid);
                CTEST_CHECK(!QFileInfo::exists(root + "/.society-photos"));
                CTEST_CHECK(QDir(f.photos()).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty());
            }
            CTEST_COMPARE(f.library->scans.load(), 0);
            PhotoController controller({}, nullptr, f.library);
            controller.setApplicationActive(false);
            controller.configure(root, true);
            CTEST_WAIT_DEFAULT(!controller.busy());
            CTEST_CHECK(controller.entries().isEmpty());
            CTEST_CHECK(!QFileInfo::exists(root + "/.society-photos"));
            CTEST_CHECK(!QFileInfo::exists(f.photos() + "/.previews"));
            CTEST_COMPARE(f.library->authorizations, 0);
            CTEST_COMPARE(f.library->exports.load(), 0); CTEST_COMPARE(f.library->previews.load(), 0);
        }
    }
    void emptyScaffoldingFromAnOlderSessionDoesNotBecomePersistentPhotoState() {
        for (const auto permission : {Access::NotDetermined, Access::Denied, Access::Unsupported, Access::Full, Access::Limited}) {
            Fixture f; f.library->permission = permission;
            const auto privateRoot = f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId();
            for (const auto &path : {privateRoot + "/gallery", privateRoot + "/references", privateRoot + "/originals", privateRoot + "/incoming", f.photos() + "/.previews"})
                CTEST_CHECK(QDir().mkpath(path));
            for (int attempt = 0; attempt < 3; ++attempt) {
                CTEST_CHECK_MESSAGE(f.store->refresh(), qPrintable(f.store->errorString()));
                QDirIterator files(privateRoot, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                CTEST_CHECK(!files.hasNext());
                CTEST_CHECK(QDir(f.photos() + "/.previews").isEmpty());
            }
            // Unknown data prevents the empty fast path and is never overwritten.
            const auto unknown = privateRoot + "/user-note.txt"; write(unknown, "keep this data");
            CTEST_CHECK_MESSAGE(f.store->refresh(), qPrintable(f.store->errorString()));
            QFile preserved(unknown); CTEST_CHECK(preserved.open(QIODevice::ReadOnly)); CTEST_COMPARE(preserved.readAll(), QByteArray("keep this data"));
            CTEST_CHECK(!QFileInfo::exists(privateRoot + "/index.json"));
        }
    }
    void transferCreatesStagingAndCommitParentsWithoutAnInitialRefresh() {
        Fixture source, target; const QByteArray original("received original");
        source.library->add("clip.mov", original, "video"); CTEST_CHECK(source.store->importLibrary());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        const auto privateRoot = target.temporary.path() + "/drive.societycontainer/.society-photos/" + target.store->containerId();
        QJsonObject request{{"id", id}, {"resource", 0}, {"action", "available"}};
        CTEST_CHECK(target.store->command(request).value("ok").toBool());
        CTEST_CHECK(!QFileInfo::exists(privateRoot));
        request["action"] = "begin";
        CTEST_CHECK_MESSAGE(target.store->command(request).value("ok").toBool(), qPrintable(target.store->errorString()));
        CTEST_CHECK(QFileInfo(privateRoot + "/incoming").isDir());
        for (const auto &name : {"originals", "gallery", "references"}) CTEST_CHECK(!QFileInfo::exists(privateRoot + '/' + name));
        CTEST_CHECK(!QFileInfo::exists(target.photos() + "/.previews"));
        request["action"] = "chunk"; request["offset"] = "0"; request["data"] = QString::fromLatin1(original.toBase64());
        CTEST_CHECK(target.store->command(request).value("ok").toBool());
        request["action"] = "commit"; CTEST_CHECK(target.store->command(request).value("ok").toBool());
        QFile cached(target.store->resourcePath(id, 0)); CTEST_CHECK(cached.open(QIODevice::ReadOnly)); CTEST_COMPARE(cached.readAll(), original);
        CTEST_CHECK(QFileInfo(privateRoot + "/originals").isDir());
        CTEST_CHECK(!QFileInfo::exists(privateRoot + "/incoming/" + record.value("resources").toArray().first().toObject().value("hash").toString()));
        CTEST_COMPARE(target.library->imports.load(), 0);
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
        int published = 0; bool resetValid = true, previewsValid = true;
        CTEST_CHECK(f.store->importLibrary({}, [&](const QJsonArray &rows, bool reset) {
            if (reset) { resetValid &= rows.size() == 8; return; }
            if (f.library->exports.load() != 0) return;
            previewsValid &= !rows.isEmpty() && !rows[0].toObject().value("previewStamp").toString().isEmpty();
            if (++published == 1) f.store->prioritize({key(7)});
        }));
        CTEST_CHECK(resetValid); CTEST_CHECK(previewsValid);
        CTEST_COMPARE(f.library->previewOrder, QStringList({"4.jpg", "7.jpg", "6.jpg", "5.jpg", "3.jpg", "2.jpg", "1.jpg", "0.jpg"}));
        CTEST_COMPARE(published, 8); CTEST_COMPARE(f.library->previews.load(), 8);
        CTEST_COMPARE(f.store->catalog().size(), 8);
    }
    void galleryIndexSurvivesCancellationReopenAndRepairsEditedOrMissingPreviews() {
        Fixture f;
        for (int i = 0; i < 6; ++i) f.library->add(QString::number(i) + ".jpg", QByteArray("original-") + QByteArray::number(i));
        int ready = 0;
        CTEST_CHECK(!f.store->importLibrary({}, [&](const QJsonArray &, bool reset) {
            if (!reset && ++ready == 2) f.store->cancel();
        }));
        CTEST_COMPARE(f.library->exports.load(), 0); CTEST_COMPARE(f.library->previews.load(), 2);
        f.store = std::make_unique<PhotoStore>(f.temporary.path() + "/drive.societycontainer", f.library);
        CTEST_CHECK(f.store->open());
        const auto restored = f.store->gallery(); CTEST_COMPARE(restored.size(), 6);
        int saved = 0;
        for (const auto &row : restored) if (!row.toObject().value("previewStamp").toString().isEmpty()) ++saved;
        CTEST_COMPARE(saved, 2);
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.library->previews.load(), 6);
        const auto exports = f.library->exports.load();
        f.store = std::make_unique<PhotoStore>(f.temporary.path() + "/drive.societycontainer", f.library);
        CTEST_CHECK(f.store->open()); CTEST_CHECK(f.store->importLibrary());
        CTEST_COMPARE(f.library->previews.load(), 6); CTEST_COMPARE(f.library->exports.load(), exports);
        const auto row = f.store->gallery().first().toObject();
        const auto cached = QUrl(row.value("preview").toString()).toLocalFile();
        write(cached, "broken jpeg");
        // The canonical alias preview repairs a corrupt gallery preview without native I/O.
        const auto scans = f.library->scans.load();
        CTEST_CHECK(f.store->refresh()); CTEST_CHECK(QImage(cached).isNull() == false);
        CTEST_COMPARE(f.library->scans.load(), scans);
        CTEST_COMPARE(f.library->previews.load(), 6);
        f.library->snapshot.assets.first().stamp = "edited";
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.library->previews.load(), 7);
    }
    void compactGalleryIsPublishedBeforeReadingIndividualReferences() {
        Fixture f; f.library->add("a.jpg", "first"); f.library->add("b.jpg", "second");
        CTEST_CHECK(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto privateRoot = f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId();
        QFile compact(privateRoot + "/gallery-catalog.json"); CTEST_CHECK(compact.open(QIODevice::ReadOnly));
        const auto bytes = compact.readAll();
        CTEST_CHECK(!bytes.contains(f.temporary.path().toUtf8())); // Relocatable relative preview filenames.
        CTEST_CHECK(!bytes.contains("identifier"));
        PhotoStore reopened(f.temporary.path() + "/drive.societycontainer", f.library);
        bool published = false, publicationValid = true, referenceWritten = false;
        const bool opened = reopened.open([&](const QJsonArray &rows, bool reset) {
            published = true; publicationValid &= reset && rows.size() == 2;
            for (const auto &value : rows) {
                const auto row = value.toObject();
                publicationValid &= !row.value("previewStamp").toString().isEmpty();
                publicationValid &= QFile::exists(QUrl(row.value("preview").toString()).toLocalFile());
            }
            // If the callback runs first, a subsequent reference error does not
            // prevent the cached gallery from being published.
            referenceWritten = tryWrite(privateRoot + "/references/" + id + ".json", "invalid reference");
        });
        CTEST_CHECK(published); CTEST_CHECK(publicationValid); CTEST_CHECK(referenceWritten); CTEST_CHECK(!opened);
    }
    void explicitConnectionRequestsPhotoAccessAndPublishesAfterGrant() {
        Fixture f; f.library->permission = Access::NotDetermined; f.library->add("first.jpg", "first photo");
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(false);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_COMPARE(f.library->authorizations, 0);
        controller.setApplicationActive(true);
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_COMPARE(f.library->authorizations, 0);
        CTEST_COMPARE(f.library->scans.load(), 0); CTEST_CHECK(controller.entries().isEmpty());
        controller.connectLibrary();
        CTEST_WAIT_COMPARE(f.library->authorizations, 1); CTEST_CHECK(controller.busy());
        controller.refresh(); controller.connectLibrary(); controller.setApplicationActive(true);
        CTEST_COMPARE(f.library->authorizations, 1);
        f.library->permission = Access::Full;
        std::exchange(f.library->authorizationFinished, {})();
        CTEST_WAIT_COMPARE(controller.entries().size(), 1); CTEST_WAIT_DEFAULT(!controller.busy());
        CTEST_CHECK(QFile::exists(f.photos() + '/' + controller.entries()[0].toMap()["id"].toString() + ".societyphoto"));
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_COMPARE(f.library->authorizations, 1);
    }
    void automaticPhotoAccessWaitsForAContainerAndNeverOpensSettings() {
        Fixture f; f.library->permission = Access::NotDetermined;
        PhotoController controller({}, nullptr, f.library);
        controller.setApplicationActive(true); controller.refresh(); CTEST_COMPARE(f.library->authorizations, 0);
        f.library->permission = Access::Denied;
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_COMPARE(f.library->authorizations, 0);
        f.library->permission = Access::Limited; f.library->add("allowed.jpg", "selected photo");
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_CHECK(controller.entries().isEmpty());
        CTEST_COMPARE(f.library->authorizations, 0); CTEST_COMPARE(f.library->scans.load(), 0);
        controller.connectLibrary();
        CTEST_WAIT_COMPARE(controller.entries().size(), 1); CTEST_COMPARE(f.library->authorizations, 0);
        controller.setApplicationActive(false); controller.setApplicationActive(true);
        CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_COMPARE(f.library->authorizations, 0);
    }
    void indexedPhotosAppearBeforeASlowOriginalFinishesAndReportRealProgress() {
        Fixture f; f.library->add("first.jpg", "first photo"); f.library->add("slow.mov", "second video", "video");
        QSemaphore gate;
        f.library->beforeExport = [&](const QString &token) { if (token == "slow.mov") gate.acquire(); };
        PhotoController controller({}, nullptr, f.library);
        // Release before the controller destructor joins its worker, even on a failed assertion.
        const auto release = qScopeGuard([&] { gate.release(); });
        photo_test::SignalRecorder progress(&controller, &PhotoController::progress);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_DEFAULT(!controller.busy()); controller.connectLibrary();
        CTEST_WAIT_COMPARE(controller.entries().size(), 2); CTEST_CHECK(controller.busy());
        CTEST_WAIT_DEFAULT(!controller.entries()[0].toMap().value("previewStamp").toString().isEmpty());
        CTEST_WAIT_DEFAULT(!controller.entries()[1].toMap().value("previewStamp").toString().isEmpty());
        CTEST_WAIT_COMPARE(progress.size(), 1); CTEST_COMPARE(progress[0][1].toLongLong(), qint64(11));
        CTEST_COMPARE(progress[0][2].toLongLong(), qint64(11));
        gate.release(); CTEST_WAIT_COMPARE(controller.entries().size(), 2); CTEST_WAIT_DEFAULT(!controller.busy());
        CTEST_COMPARE(progress.size(), 2); CTEST_COMPARE(progress[1][1].toLongLong(), qint64(12));
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
        CTEST_WAIT_DEFAULT(!controller.busy()); controller.connectLibrary();
        CTEST_WAIT_DEFAULT(entered.available());
        controller.configure(f.temporary.path() + "/drive.societycontainer", false);
        controller.waitForDone(); CTEST_CHECK(exited.load());
        QLockFile lock(f.temporary.path() + "/drive.societycontainer/.society-photos/" + f.store->containerId() + "/operation.lock");
        CTEST_CHECK(lock.tryLock());
        QCoreApplication::processEvents(); CTEST_CHECK(!controller.active()); CTEST_CHECK(controller.entries().isEmpty());
    }
    void hashingCancellationStopsBeforePublishingAPartialDigest() {
        Fixture f; const QByteArray bytes(4 * 1024 * 1024, 'x');
        const auto path = f.temporary.path() + "/large.mov"; write(path, bytes);
        int chunks = 0; QString error;
        CTEST_CHECK(PhotoStore::digestFile(path, &error, [&] { return ++chunks == 3; }).isEmpty());
        CTEST_COMPARE(chunks, 3); CTEST_CHECK(!error.isEmpty());
        CTEST_COMPARE(PhotoStore::digestFile(path), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }
    void incrementalPhotoBatchesKeepEveryPreviouslyPublishedRecord() {
        Fixture f; f.library->add("existing.jpg", "existing"); CTEST_CHECK(f.store->importLibrary());
        for (int i = 0; i < 8; ++i) f.library->add(QString::number(i) + ".jpg", QByteArray::number(i));
        f.library->add("last.mov", "last", "video"); QSemaphore gate;
        f.library->beforeExport = [&](const QString &token) {
            if (token == "4.jpg") QThread::msleep(550);
            if (token == "last.mov") gate.acquire();
        };
        PhotoController controller({}, nullptr, f.library);
        const auto release = qScopeGuard([&] { gate.release(); });
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_DEFAULT(!controller.busy()); controller.connectLibrary();
        CTEST_WAIT_DEFAULT(controller.entries().size() >= 6); CTEST_CHECK(controller.busy());
        QSet<QString> names; for (const auto &row : controller.entries()) names.insert(row.toMap().value("name").toString());
        CTEST_CHECK(names.contains("existing.jpg")); CTEST_CHECK(names.contains("0.jpg")); CTEST_CHECK(names.contains("4.jpg"));
        gate.release(); CTEST_WAIT_DEFAULT(!controller.busy()); CTEST_COMPARE(controller.entries().size(), 10);
    }
    void portableNamesKeepTheirExtensionAndPlatformLimits() {
        const auto id = QString(64, 'a');
        const auto name = importedResourceName(id, 0, QString(100, QChar(0xac00)) + ":test?.png");
        CTEST_COMPARE(importedPhotoId(name), id); CTEST_CHECK(name.endsWith(".png")); CTEST_CHECK(name.toUtf8().size() < 255);
        CTEST_CHECK(!name.contains(':')); CTEST_CHECK(!name.contains('?'));
        CTEST_CHECK(importedPhotoId("Society-" + id + "-x-original.png").isEmpty());
    }
    void nativeOriginalsRemainAliasesAndPreviewsPersist() {
        Fixture f; f.library->add("still.jpg", "original-photo"); f.library->add("movie.mp4", "original-video", "video");
        CTEST_CHECK_MESSAGE(f.store->importLibrary(), qPrintable(f.store->errorString()));
        const auto records = f.store->catalog(); CTEST_COMPARE(records.size(), 2);
        CTEST_COMPARE(f.library->imports, 0);
        for (const auto &value : records) {
            const auto record = value.toObject(); const auto id = record.value("id").toString();
            CTEST_CHECK(QFile::exists(f.photos() + '/' + id + ".societyphoto"));
            CTEST_CHECK(QFile::exists(f.store->previewPath(id)));
            CTEST_CHECK(!QFile::exists(f.photos() + '/' + record.value("name").toString()));
            CTEST_CHECK(!QJsonDocument(record).toJson().contains(f.library->directory.toUtf8()));
        }
        QFile original(f.library->directory + "/still.jpg"); CTEST_CHECK(original.open(QIODevice::ReadOnly)); CTEST_COMPARE(original.readAll(), QByteArray("original-photo"));
        const int exported = f.library->exports.load();
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.library->exports, exported); CTEST_COMPARE(f.store->catalog(), records);
        f.store->releaseExports();
        for (const auto &value : records) CTEST_CHECK(!QFile::exists(f.store->resourcePath(value.toObject().value("id").toString(), 0)));
    }
    void duplicatesPreserveAllNativeResourcesAndShareOriginalBytes() {
        Fixture f; f.library->add("live.jpg", "photo-bytes");
        write(f.library->directory + "/live.mov", "motion-bytes");
        f.library->snapshot.assets.first().resources.append({"live.mov", "live.mov", "pairedVideo"});
        CTEST_CHECK(f.store->importLibrary()); const auto original = f.store->catalog().first().toObject();
        const auto id = original.value("id").toString();
        const auto exported = f.store->originalsForSharing(id); CTEST_COMPARE(exported.size(), 2);
        QFile still(exported[0]); CTEST_CHECK(still.open(QIODevice::ReadOnly)); CTEST_COMPARE(still.readAll(), QByteArray("photo-bytes"));
        QFile motion(exported[1]); CTEST_CHECK(motion.open(QIODevice::ReadOnly)); CTEST_COMPARE(motion.readAll(), QByteArray("motion-bytes"));
        CTEST_CHECK_MESSAGE(f.store->duplicate(id), qPrintable(f.store->errorString()));
        CTEST_COMPARE(f.library->imports.load(), 0); CTEST_COMPARE(f.library->snapshot.assets.size(), 1);
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.store->catalog().size(), 2);
        QString duplicate;
        for (const auto &value : f.store->catalog()) if (value.toObject().value("id") != id) {
            duplicate = value.toObject().value("id").toString();
            CTEST_COMPARE(value.toObject().value("resources").toArray().size(), 2);
        }
        CTEST_CHECK(!duplicate.isEmpty()); CTEST_CHECK(f.store->trash(duplicate));
        CTEST_COMPARE(f.library->removals.load(), 0); CTEST_CHECK(QFileInfo::exists(f.library->directory + "/live.jpg"));
    }
    void containerOwnedCopiesAndReceivedRevisionsKeepTheirOriginals() {
        Fixture f; f.library->add("native.jpg", "native original");
        CTEST_CHECK(f.store->importLibrary());
        auto record = f.store->catalog().first().toObject();
        const auto id = record.value("id").toString();
        CTEST_CHECK(f.store->duplicate(id));
        QString duplicate;
        for (const auto &value : f.store->catalog())
            if (value.toObject().value("id") != id) duplicate = value.toObject().value("id").toString();
        CTEST_CHECK(!duplicate.isEmpty());
        f.store->releaseExports(); CTEST_CHECK(f.store->hasOriginal(duplicate));
        const QByteArray replacement("received replacement original");
        auto resource = record.value("resources").toArray().first().toObject();
        resource["hash"] = QString::fromLatin1(QCryptographicHash::hash(replacement, QCryptographicHash::Sha256).toHex());
        resource["size"] = QString::number(replacement.size()); record["resources"] = QJsonArray{resource};
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        QJsonObject packet{{"id", id}, {"resource", 0}, {"action", "begin"}};
        CTEST_CHECK(f.store->command(packet).value("ok").toBool());
        packet["action"] = "chunk"; packet["offset"] = "0"; packet["data"] = QString::fromLatin1(replacement.toBase64());
        CTEST_CHECK(f.store->command(packet).value("ok").toBool());
        packet["action"] = "commit"; CTEST_CHECK(f.store->command(packet).value("ok").toBool());
        CTEST_CHECK(f.store->consume(id));
        QFile received(f.store->resourcePath(id, 0)); CTEST_CHECK(received.open(QIODevice::ReadWrite));
        CTEST_CHECK(received.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600), QFileDevice::FileModificationTime));
        received.close(); f.store->releaseExports(); CTEST_CHECK(f.store->refresh());
        CTEST_CHECK(f.store->hasOriginal(id)); CTEST_CHECK(f.store->hasOriginal(duplicate));
        CTEST_CHECK(received.open(QIODevice::ReadOnly)); CTEST_COMPARE(received.readAll(), replacement);
        QFile native(f.library->directory + "/native.jpg"); CTEST_CHECK(native.open(QIODevice::ReadOnly));
        CTEST_COMPARE(native.readAll(), QByteArray("native original"));
        CTEST_COMPARE(f.library->imports.load(), 0); CTEST_COMPARE(f.library->removals.load(), 0);
    }
    void localAdditionStaysInTheContainerAndPreservesTheUsersFile() {
        Fixture f; QImage image(4, 4, QImage::Format_RGB32); image.fill(Qt::red);
        CTEST_CHECK(image.save(f.photos() + "/new.png"));
        CTEST_CHECK_MESSAGE(f.store->refresh(), qPrintable(f.store->errorString()));
        CTEST_COMPARE(f.library->imports, 0); CTEST_COMPARE(f.store->catalog().size(), 1);
        CTEST_CHECK(QFile::exists(f.photos() + "/new.png"));
        CTEST_CHECK(f.store->refresh()); CTEST_COMPARE(f.library->imports, 0); CTEST_COMPARE(f.store->catalog().size(), 1);
    }
    void transferChecksHashesAndKeepsCommittedOriginalsInsideTheContainer() {
        Fixture source, target;
        source.library->add("clip.mp4", QByteArray(700000, 'v'), "video"); CTEST_CHECK(source.store->importLibrary());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        CTEST_CHECK(target.store->importLibrary()); CTEST_CHECK(!target.store->hasOriginal(id)); CTEST_CHECK(source.store->prepare(id));
        QJsonObject base{{"id", id}, {"resource", 0}};
        auto request = base; request["action"] = "begin";
        CTEST_CHECK(target.store->command(request).value("ok").toBool());
        for (qint64 offset = 0; offset < 700000; offset += PhotoStore::ChunkBytes) {
            request = base; request["action"] = "read"; request["offset"] = QString::number(offset);
            const auto chunk = source.store->command(request); CTEST_CHECK(chunk.value("ok").toBool());
            request["action"] = "chunk"; request["data"] = chunk.value("data");
            CTEST_CHECK(target.store->command(request).value("ok").toBool());
            CTEST_CHECK(target.store->command(request).value("ok").toBool()); // Replayed acknowledgement.
        }
        request = base; request["action"] = "commit"; CTEST_CHECK(target.store->command(request).value("ok").toBool());
        CTEST_CHECK(target.store->consume(id)); CTEST_COMPARE(target.library->imports, 0);
        CTEST_CHECK(target.store->consume(id)); CTEST_CHECK(target.store->refresh()); CTEST_COMPARE(target.library->imports, 0);
        CTEST_COMPARE(target.store->catalog().size(), 1);
        request["id"] = "../../outside"; CTEST_CHECK(!target.store->command(request).value("ok").toBool());
    }
    void legacyLayoutKeepsNativeReferencesAndOriginals()
    {
        Fixture f; f.library->add("original.jpg", "unchanged original"); CTEST_CHECK(f.store->importLibrary());
        const auto before = f.store->catalog(); const auto id = before.first().toObject().value("id").toString();
        const auto identity = f.store->containerId();
        const auto root = f.temporary.path() + "/drive.societycontainer";
        QFile manifest(root + "/.society-drive.json"); CTEST_CHECK(manifest.open(QIODevice::ReadOnly));
        auto data = QJsonDocument::fromJson(manifest.readAll()).object(); manifest.close();
        QJsonArray legacy;
        for (const auto &entry : data.value("sections").toArray())
            if (entry.toObject().value("id") != "photos") legacy.append(entry);
        data["sections"] = legacy;
        CTEST_CHECK(QDir().rename(root + "/Photos", root + "/Files/Photos"));
        write(manifest.fileName(), QJsonDocument(data).toJson());
        f.store = std::make_unique<PhotoStore>(root, f.library);
        CTEST_CHECK_MESSAGE(f.store->open(), qPrintable(f.store->errorString()));
        CTEST_COMPARE(f.store->containerId(), identity); CTEST_COMPARE(f.store->catalog(), before);
        CTEST_CHECK(f.store->hasOriginal(id)); CTEST_CHECK(QFileInfo::exists(f.store->previewPath(id)));
        CTEST_COMPARE(f.store->previewPath(id), root + "/Photos/.previews/" + id + ".jpg");
        const auto exports = f.library->exports.load();
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.library->exports.load(), exports);
        CTEST_COMPARE(f.library->imports.load(), 0);
        CTEST_CHECK(!QFileInfo::exists(root + "/Files/Photos"));
    }

    void deniedAndLimitedAccessNeverErasePhotos() {
        Fixture f; f.library->add("a.jpg", "original"); CTEST_CHECK(f.store->importLibrary());
        const auto records = f.store->catalog(); f.library->snapshot.assets.clear(); f.library->permission = Access::Limited;
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.store->catalog(), records); CTEST_COMPARE(f.library->removals, 0);
        f.library->permission = Access::Denied; CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.store->catalog(), records);
    }
    void corruptTransferAndRedirectedCacheAreRejected() {
        Fixture source, target; source.library->add("a.jpg", "original"); CTEST_CHECK(source.store->importLibrary());
        const auto record = source.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        write(target.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson()); CTEST_CHECK(target.store->importLibrary());
        QJsonObject request{{"id", id}, {"resource", 0}, {"action", "begin"}};
        CTEST_CHECK(target.store->command(request).value("ok").toBool());
        request["action"] = "chunk"; request["offset"] = "0"; request["data"] = QString::fromLatin1(QByteArray("tampered").toBase64());
        CTEST_CHECK(target.store->command(request).value("ok").toBool());
        request["action"] = "commit"; CTEST_CHECK(!target.store->command(request).value("ok").toBool());
        CTEST_COMPARE(target.library->imports, 0);
        const auto outside = target.temporary.path() + "/untouched"; write(outside, "private");
        const auto cache = target.store->resourcePath(id, 0);
        CTEST_CHECK(QDir().mkpath(QFileInfo(cache).absolutePath())); // The fixture deliberately installs a redirected cache.
        CTEST_CHECK(QFile::link(outside, cache));
        request["action"] = "read"; CTEST_CHECK(!target.store->command(request).value("ok").toBool());
        QFile untouched(outside); CTEST_CHECK(untouched.open(QIODevice::ReadOnly)); CTEST_COMPARE(untouched.readAll(), QByteArray("private"));
    }
    void originalEditUpdatesOneObjectAndTrashIsExplicit() {
        Fixture f; f.library->add("a.jpg", "first"); CTEST_CHECK(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        write(f.library->directory + "/a.jpg", "second"); f.library->snapshot.assets[0].stamp = "2";
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.store->catalog().size(), 1);
        CTEST_COMPARE(f.store->catalog().first().toObject().value("id").toString(), id);
        CTEST_CHECK(f.store->trash(id)); CTEST_COMPARE(f.library->removals, 0);
        CTEST_CHECK(f.store->importLibrary()); CTEST_CHECK(f.store->catalog().first().toObject().value("deleted").toBool());
        CTEST_CHECK(!f.store->hasOriginal(id));
    }
    void unavailableAssetDoesNotBlockOtherPhotos() {
        Fixture f; f.library->add("offline.jpg", "offline"); f.library->add("ready.jpg", "ready");
        CTEST_CHECK(QFile::remove(f.library->directory + "/offline.jpg"));
        CTEST_CHECK(!f.store->importLibrary()); CTEST_COMPARE(f.store->catalog().size(), 1);
        CTEST_COMPARE(f.store->catalog().first().toObject().value("name").toString(), QString("ready.jpg"));
    }
    void mediaDatabaseReplacementDoesNotBecomeGlobalDeletion() {
        Fixture f; f.library->snapshot.revision = "first-volume";
        f.library->add("a.jpg", "original"); CTEST_CHECK(f.store->importLibrary());
        const auto records = f.store->catalog(); f.library->snapshot.assets.clear();
        f.library->snapshot.revision = "replacement-volume";
        CTEST_CHECK(f.store->importLibrary()); CTEST_CHECK(f.store->importLibrary());
        CTEST_COMPARE(f.store->catalog(), records); CTEST_COMPARE(f.library->removals, 0);
    }
    void anotherTransferKeepsItsOriginalLease() {
        Fixture f; f.library->add("a.jpg", "original"); CTEST_CHECK(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto a = QUuid::createUuid().toString(), b = QUuid::createUuid().toString();
        auto request = QJsonObject{{"id", id}, {"action", "prepare"}, {"lease", a}};
        CTEST_CHECK(f.store->command(request).value("ok").toBool());
        request["lease"] = b; CTEST_CHECK(f.store->command(request).value("ok").toBool());
        request["action"] = "release"; request["lease"] = a;
        CTEST_CHECK(f.store->command(request).value("ok").toBool());
        CTEST_CHECK(QFile::exists(f.store->resourcePath(id, 0)));
        request["action"] = "read"; request["lease"] = b; request["resource"] = 0; request["offset"] = "0";
        CTEST_CHECK(f.store->command(request).value("ok").toBool());
        request["action"] = "release"; CTEST_CHECK(f.store->command(request).value("ok").toBool());
        CTEST_CHECK(!QFile::exists(f.store->resourcePath(id, 0)));
    }
    void expiredViewingCopiesAreRemovedWithoutTouchingNativeOriginals() {
        Fixture f; f.library->add("a.jpg", "original"); CTEST_CHECK(f.store->importLibrary());
        const auto id = f.store->catalog().first().toObject().value("id").toString();
        const auto path = f.store->originalForViewing(id); CTEST_CHECK(!path.isEmpty());
        QFile copy(path); CTEST_CHECK(copy.open(QIODevice::ReadWrite));
        CTEST_CHECK(copy.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime)); copy.close();
        CTEST_CHECK(f.store->importLibrary()); CTEST_CHECK(!QFile::exists(path));
        QFile original(f.library->directory + "/a.jpg"); CTEST_CHECK(original.open(QIODevice::ReadOnly)); CTEST_COMPARE(original.readAll(), QByteArray("original"));
    }
    void unchangedRefreshDoesNotStartAnotherFileSync() {
        Fixture f; f.library->add("a.jpg", "original");
        PhotoController controller({}, nullptr, f.library);
        photo_test::SignalCounter contents(&controller, &PhotoController::contentsChanged);
        photo_test::SignalCounter entries(&controller, &PhotoController::entriesChanged);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        CTEST_COMPARE(contents.count(), 0); CTEST_CHECK(controller.entries().isEmpty());
        controller.connectLibrary(); CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        CTEST_COMPARE(contents.count(), 1); CTEST_CHECK(entries.count() >= 1);
        const auto initialUpdates = entries.count();
        controller.refresh(); CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        CTEST_COMPARE(contents.count(), 1); CTEST_COMPARE(entries.count(), initialUpdates);
    }
    void partialNativeImportCannotReplaceACompleteAlias() {
        Fixture f; f.library->add("still.jpg", "still"); f.library->add("motion.mov", "motion", "video");
        f.library->snapshot.assets[0].resources.append({"motion.mov", "motion.mov", "pairedVideo"});
        f.library->snapshot.assets.removeLast(); CTEST_CHECK(f.store->importLibrary());
        const auto record = f.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        auto &asset = f.library->snapshot.assets[0]; asset.resources.removeLast(); asset.stamp = "partial";
        asset.resources[0].name = importedResourceName(id, 0, "still.jpg");
        CTEST_CHECK(!f.store->importLibrary()); CTEST_COMPARE(f.store->catalog().first().toObject(), record);
        CTEST_CHECK(!f.store->hasOriginal(id));
    }
    void remoteResourceVersionIsNotOverwrittenByAnUnchangedNativeCopy() {
        Fixture f; f.library->add("a.jpg", "first"); CTEST_CHECK(f.store->importLibrary());
        auto record = f.store->catalog().first().toObject(); const auto id = record.value("id").toString();
        auto resource = record.value("resources").toArray().first().toObject();
        resource["hash"] = QString(64, 'a'); record["resources"] = QJsonArray{resource};
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(record).toJson());
        CTEST_CHECK(f.store->importLibrary()); CTEST_COMPARE(f.store->catalog().first().toObject(), record);
        CTEST_CHECK(!f.store->hasOriginal(id));
    }
    void completedRequestCacheDoesNotStopLongTransfers() {
        Fixture f; f.library->add("a.jpg", "original"); CTEST_CHECK(f.store->importLibrary());
        const auto record = f.store->catalog().first().toObject();
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true, {"peer"});
        CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        for (int i = 0; i < 80; ++i) {
            const QJsonObject request{{"op", "society.photos"}, {"container", f.store->containerId()},
                {"ticket", QUuid::createUuid().toString()}, {"action", "available"}, {"id", record.value("id")}};
            QJsonObject reply;
            CTEST_WAIT_VERIFY(!(reply = controller.handle("peer", request)).value("pending").toBool(), 5000);
            CTEST_CHECK_MESSAGE(reply.value("ok").toBool(), qPrintable(reply.value("error").toString()));
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
        CTEST_WAIT_DEFAULT(!controller.busy()); controller.connectLibrary();
        CTEST_WAIT_DEFAULT(!controller.busy());
        const auto entries = controller.entries(); CTEST_COMPARE(entries.size(), 4);
        CTEST_COMPARE(entries.first().toMap().value("name").toString(), QString("Z-oldest.jpg"));
        CTEST_COMPARE(entries.last().toMap().value("name").toString(), QString("A-newest.mov"));
        CTEST_COMPARE(entries.last().toMap().value("media").toString(), QString("video"));
        CTEST_COMPARE(entries[1].toMap().value("created"), entries[2].toMap().value("created"));
        CTEST_CHECK(entries[1].toMap().value("galleryKey").toString() < entries[2].toMap().value("galleryKey").toString());
    }
#ifdef IIPHOTOLIBRARY_TEST_LEGACY_QML
    void chronologicalGalleryOpensAtNewestAndPreservesBrowsing(const QSize &viewport, const char* dataTag) {
        Fixture f;
        const auto epoch = QDateTime::fromString("2026-01-01T00:00:00Z", Qt::ISODate);
        const auto add = [&](int index) {
            const auto name = QString("Photo-%1.%2").arg(99 - index, 2, 10, QChar('0')).arg(index % 2 ? "mov" : "jpg");
            f.library->add(name, QByteArray("timeline-") + QByteArray::number(index), index % 2 ? "video" : "photo");
            f.library->snapshot.assets.last().created = epoch.addDays(index);
        };
        for (int i = 0; i < 40; ++i) add(i);
        CTEST_CHECK(f.store->importLibrary());
        PhotoController controller({}, nullptr, f.library);
        QQmlApplicationEngine engine; engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_QML_IMPORT_PATH)); engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)},
            {"width", viewport.width()}, {"height", viewport.height()}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(IIPHOTOLIBRARY_QML_FILE)));
        CTEST_CHECK_MESSAGE(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); CTEST_CHECK(window);
        auto *view = visualItem(window->contentItem(), "photosView"); CTEST_CHECK(view);
        auto *grid = visualItem(view, "photoGrid"); CTEST_CHECK(grid);
        view->setProperty("touchNavigation", viewport.width() < 600);
        CTEST_COMPARE(grid->property("count").toInt(), 0);

        // Data arrives after the empty view is already laid out.
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_COMPARE(grid->property("count").toInt(), 40);
        auto *importLibrary = visualItem(view, "connectPhotoLibrary"); CTEST_CHECK(importLibrary);
        CTEST_CHECK(importLibrary->isVisible());
        CTEST_CHECK(grid->property("cacheBuffer").toReal() >= grid->height() * 2);
        // atYEnd can still describe the empty layout immediately after count changes.
        CTEST_WAIT_DEFAULT(grid->property("atYEnd").toBool()
            && grid->property("contentY").toReal() > grid->property("originY").toReal());
        const auto settled = [&] {
            const auto pending = view->property("pendingViewState");
            return !controller.busy() && !view->property("initialPositionPending").toBool()
                && (pending.isNull() || pending.value<QJSValue>().isNull());
        };
        // This is a state assertion, not a five-second storage benchmark.
        // External-volume fixture I/O exceeded 5 s while the UI was already ready.
        CTEST_WAIT_VERIFY(settled(), 15000);
        photo_test::processFor(100);
        CTEST_CHECK(window->grabWindow().save(f.temporary.filePath(QStringLiteral("chronological-%1.png")
            .arg(QString::fromLatin1(dataTag)))));

        // Checking unchanged content does not replace rows or move the camera.
        const auto endScroll = grid->property("contentY").toReal();
        photo_test::SignalCounter entriesChanged(&controller, &PhotoController::entriesChanged);
        controller.refresh(); CTEST_WAIT_VERIFY(settled(), 15000);
        CTEST_COMPARE(entriesChanged.size(), 0);
        CTEST_COMPARE(grid->property("contentY").toReal(), endScroll);
        // New arrivals preserve the camera even when it was at the end.
        for (int i = 40; i < 43; ++i) add(i);
        controller.connectLibrary(); CTEST_WAIT_COMPARE(grid->property("count").toInt(), 43); CTEST_WAIT_VERIFY(settled(), 15000);
        CTEST_WAIT_COMPARE(grid->property("contentY").toReal(), endScroll);
        grid->setProperty("contentY", grid->property("originY").toReal() + grid->property("cellHeight").toReal());
        photo_test::processFor(100);
        const auto scroll = grid->property("contentY").toReal();
        CTEST_CHECK(scroll > grid->property("originY").toReal() && !grid->property("atYEnd").toBool());
        const auto selected = controller.entries()[8].toMap().value("id").toString();
        view->setProperty("selectedId", selected);
        for (int i = 43; i < 50; ++i) add(i);
        controller.connectLibrary(); CTEST_WAIT_COMPARE(grid->property("count").toInt(), 50); CTEST_WAIT_VERIFY(settled(), 15000);
        CTEST_COMPARE(grid->property("contentY").toReal(), scroll);
        CTEST_COMPARE(view->property("selectedId").toString(), selected);

        // A changed capture date can reorder the same number of entries.
        const auto movedId = controller.entries().first().toMap().value("id").toString();
        QFile alias(f.photos() + '/' + movedId + ".societyphoto"); CTEST_CHECK(alias.open(QIODevice::ReadOnly));
        auto record = QJsonDocument::fromJson(alias.readAll()).object(); alias.close();
        record["created"] = epoch.addDays(100).toString(Qt::ISODateWithMs);
        write(alias.fileName(), QJsonDocument(record).toJson());
        controller.refresh(); CTEST_WAIT_VERIFY(settled(), 15000);
        CTEST_COMPARE(controller.entries().last().toMap().value("id").toString(), movedId);
        CTEST_COMPARE(grid->property("contentY").toReal(), scroll);
        CTEST_COMPARE(view->property("selectedId").toString(), selected);

        // Storage keeps the view alive: re-entry still starts at the newest.
        view->setVisible(false); view->setVisible(true);
        CTEST_WAIT_VERIFY(settled(), 15000); CTEST_WAIT_DEFAULT(grid->property("atYEnd").toBool());
        view->setVisible(false);
        controller.configure({}, false); CTEST_WAIT_COMPARE(grid->property("count").toInt(), 0);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_COMPARE(grid->property("count").toInt(), 50); CTEST_WAIT_DEFAULT(!controller.busy());
        view->setVisible(true);
        CTEST_WAIT_VERIFY(settled(), 15000); CTEST_WAIT_DEFAULT(grid->property("atYEnd").toBool());
        CTEST_COMPARE(view->property("selectedId").toString(), QString());
        CTEST_CHECK_MESSAGE(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
    void galleryRendersAndLatePreviewKeepsBrowsingPosition() {
        Fixture f;
        for (int i = 0; i < 40; ++i) f.library->add(QString("Photo %1.jpg").arg(i), QByteArray("original-") + QByteArray::number(i));
        CTEST_CHECK(f.store->importLibrary()); const auto records = f.store->catalog();
        const auto id = records.first().toObject().value("id").toString();
        QFile image(f.store->previewPath(id)); CTEST_CHECK(image.open(QIODevice::ReadOnly)); const auto preview = image.readAll(); image.close();
        CTEST_CHECK(image.remove()); f.library->permission = Access::Denied;
        PhotoController controller({}, nullptr, f.library);
        controller.configure(f.temporary.path() + "/drive.societycontainer", true);
        CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        QQmlApplicationEngine engine; engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_QML_IMPORT_PATH)); engine.addImportPath(QStringLiteral(IIPHOTOLIBRARY_LVRS_QML_IMPORT_PATH));
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(IIPHOTOLIBRARY_QML_FILE)));
        CTEST_CHECK_MESSAGE(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); CTEST_CHECK(window);
        auto *view = visualItem(window->contentItem(), "photosView"); CTEST_CHECK(view);
        auto *grid = visualItem(view, "photoGrid"); CTEST_CHECK(grid);
        CTEST_WAIT_COMPARE(grid->property("count").toInt(), 40);
        CTEST_CHECK(grid->property("cacheBuffer").toReal() >= grid->height() * 2);
        CTEST_WAIT_DEFAULT(!view->property("initialPositionPending").toBool());
        CTEST_CHECK(QMetaObject::invokeMethod(grid, "positionViewAtBeginning")); photo_test::processFor(100);
        view->setProperty("selectedId", id);
        photo_test::SignalCounter contentChanges(&controller, &PhotoController::contentsChanged);
        photo_test::SignalCounter resets(controller.galleryModel(), &QAbstractItemModel::modelReset);
        photo_test::SignalCounter updates(controller.galleryModel(), &QAbstractItemModel::dataChanged);
        write(f.store->previewPath(id), preview);
        controller.refresh(); CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        QQuickItem *loaded = nullptr;
        CTEST_WAIT_DEFAULT((loaded = visualItem(grid, "photoPreview-" + id)) != nullptr);
        CTEST_WAIT_COMPARE(loaded->property("status").toInt(), 1); // Image.Ready
        CTEST_CHECK(loaded->property("cache").toBool());
        CTEST_COMPARE(contentChanges.count(), 0);
        CTEST_COMPARE(resets.count(), 0); CTEST_CHECK(updates.count() > 0);
        grid->setProperty("contentY", 250.0); photo_test::processFor(100);
        const auto scroll = grid->property("contentY").toReal();
        controller.refresh(); CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        CTEST_COMPARE(view->property("selectedId").toString(), id);
        CTEST_COMPARE(grid->property("contentY").toReal(), scroll);
        auto *info = view->findChild<QObject *>("galleryInfo"); CTEST_CHECK(info);
        CTEST_CHECK(!info->property("visible").toBool());
        grid->setProperty("contentY", grid->property("originY")); photo_test::processFor(100);
        auto *tile = visualItem(grid, "galleryTile"); CTEST_CHECK(tile);
        CTEST_COMPARE(tile->width(), tile->height());
        CTEST_COMPARE(grid->property("cellWidth").toReal() - tile->width(), 2.0);
        CTEST_COMPARE(tile->property("text").toString(), QString());
        auto *touchDevice = photo_test::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        const auto center = tile->mapToScene(QPointF(tile->width()/2, tile->height()/2)).toPoint();
        photo_test::TouchSequence(window, touchDevice).press(0, center).commit();
        photo_test::TouchSequence(window, touchDevice).release(0, center).commit();
        CTEST_WAIT_DEFAULT(info->property("visible").toBool());
        CTEST_COMPARE(info->property("fileName").toString(), tile->property("name").toString());
        photo_test::processFor(250);
        CTEST_CHECK(window->grabWindow().save(f.temporary.filePath(QStringLiteral("information-desktop.png"))));
        CTEST_CHECK(QMetaObject::invokeMethod(info, "close")); CTEST_WAIT_DEFAULT(!info->property("visible").toBool()); photo_test::processFor(250);
        const auto beforePinch = grid->property("cellWidth").toReal();
        const auto middle = grid->mapToScene(QPointF(grid->width()/2, grid->height()/2)).toPoint();
        photo_test::TouchSequence(window, touchDevice).press(0, middle-QPoint(40,0)).press(1, middle+QPoint(40,0)).commit();
        for (int distance = 50; distance <= 120; distance += 10) {
            photo_test::TouchSequence(window, touchDevice).move(0, middle-QPoint(distance,0)).move(1, middle+QPoint(distance,0)).commit(); photo_test::processFor(20);
        }
        photo_test::TouchSequence(window, touchDevice).release(0, middle-QPoint(120,0)).release(1, middle+QPoint(120,0)).commit();
        CTEST_COMPARE(grid->property("cellWidth").toReal(), beforePinch);
        CTEST_COMPARE(grid->property("cellWidth"), grid->property("cellHeight"));
        CTEST_CHECK(!info->property("visible").toBool());
        const auto enlarged = grid->property("cellWidth").toReal();
        photo_test::TouchSequence(window, touchDevice).press(0, middle-QPoint(120,0)).press(1, middle+QPoint(120,0)).commit();
        for (int distance = 110; distance >= 40; distance -= 10) {
            photo_test::TouchSequence(window, touchDevice).move(0, middle-QPoint(distance,0)).move(1, middle+QPoint(distance,0)).commit(); photo_test::processFor(20);
        }
        photo_test::TouchSequence(window, touchDevice).release(0, middle-QPoint(40,0)).release(1, middle+QPoint(40,0)).commit();
        CTEST_COMPARE(grid->property("cellWidth").toReal(), enlarged);
        CTEST_CHECK(!info->property("visible").toBool());
        auto *zoom = view->findChild<QObject *>("galleryZoom"); CTEST_CHECK(zoom);
        zoom->setProperty("thumbnailSize", 128.0);
        view->setProperty("selectedId", id);
        photo_test::processFor(100); CTEST_CHECK(window->grabWindow().save(f.temporary.filePath(QStringLiteral("gallery-desktop.png"))));
        window->resize(390, 844); photo_test::processFor(100);
        const auto *add = visualItem(view, "addPhotos"); CTEST_CHECK(add);
        CTEST_CHECK(add->mapToItem(view, QPointF(add->width(), 0)).x() <= view->width());
        CTEST_CHECK(window->grabWindow().save(f.temporary.filePath(QStringLiteral("gallery-mobile.png"))));
        auto deleted = records.first().toObject(); deleted["deleted"] = true;
        write(f.photos() + '/' + id + ".societyphoto", QJsonDocument(deleted).toJson());
        controller.refresh(); CTEST_WAIT_VERIFY(!controller.busy(), 5000);
        CTEST_WAIT_COMPARE(grid->property("count").toInt(), 39);
        CTEST_COMPARE(view->property("selectedId").toString(), QString());
        CTEST_CHECK_MESSAGE(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        window->close();
    }
#endif
    void authenticatedExchangeUploadsLocalAndDownloadsOnlySelectedOriginal() {
        Fixture host, client;
        const auto hostDrive = iiSocietyContainer::SocietyDrive::open(host.temporary.path() + "/drive.societycontainer");
        const auto clientDrive = iiSocietyContainer::SocietyDrive::open(client.temporary.path() + "/drive.societycontainer");
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(clientDrive->rootPath(), clientDrive->identifier(), hostDrive->identifier()));
        CTEST_CHECK(iiSocietyContainer::SocietyDrive::completeReplica(clientDrive->rootPath(), hostDrive->identifier()));
        client.store = std::make_unique<PhotoStore>(clientDrive->rootPath(), client.library); CTEST_CHECK(client.store->open());
        host.library->add("host.jpg", QByteArray(530000, 'h')); client.library->add("phone.mp4", QByteArray(720000, 'p'), "video");
        CTEST_CHECK(host.store->importLibrary()); CTEST_CHECK(client.store->importLibrary());
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
        CTEST_WAIT_VERIFY(!receiver.busy(), 10000);
        const QJsonObject packet{{"op", "society.photos"}, {"container", hostDrive->identifier()},
            {"ticket", QUuid::createUuid().toString()}, {"action", "available"}, {"id", hostRecord.value("id")}};
        CTEST_CHECK(!receiver.handle("stranger", packet).value("ok").toBool());
        int idleTransitions = 0;
        connect(&sender, &PhotoController::changed, this, [&] {
            if (!sender.busy()) ++idleTransitions;
        });
        sender.configure(clientDrive->rootPath(), true, {"host"}, {"host"});
        CTEST_WAIT_VERIFY(QFileInfo::exists(host.store->resourcePath(clientRecord.value("id").toString(), 0)), 20000);
        CTEST_COMPARE(host.library->imports.load(), 0);
        CTEST_WAIT_VERIFY(!sender.busy(), 10000);
        // The local catalog refresh and post-import refresh are part of the
        // same transfer. A background owner must see only the final idle edge.
        CTEST_COMPARE(idleTransitions, 1);
        CTEST_COMPARE(client.library->imports.load(), 0);
        CTEST_CHECK(client.store->originalForViewing(hostRecord.value("id").toString()).isEmpty());
        sender.downloadPhoto(hostRecord.value("id").toString());
        CTEST_WAIT_VERIFY(!sender.busy(), 10000);
        CTEST_CHECK(!client.store->originalForViewing(hostRecord.value("id").toString()).isEmpty());
        CTEST_COMPARE(client.library->imports.load(), 0);
        CTEST_CHECK(client.store->importLibrary());
        CTEST_COMPARE(client.library->imports.load(), 0);
        CTEST_CHECK(requests > 10);
        receiver.configure({}, false); sender.configure({}, false);
    }
    void nativeLibrariesExchangeAliasesPreviewsAndOriginalsOverTls() {
        Fixture desktop, phone;
        desktop.library->add("desktop.jpg", QByteArray(600000, 'd'));
        phone.library->add("phone.mp4", QByteArray(800000, 'p'), "video");
        CTEST_CHECK(desktop.store->importLibrary());
        const auto desktopRecord = desktop.store->catalog().first().toObject();
        iiWebServer::LanPeer host, client;
        iiSocietyHelper::Sync::Controller hosting({}), syncing([&](const auto &peer, const auto &packet) { return client.request(peer, packet); });
        PhotoController hostPhotos({}, nullptr, desktop.library);
        PhotoController phonePhotos([&](const auto &peer, const auto &packet) { return client.request(peer, packet); }, nullptr, phone.library);
        const auto files = iiSocietyHelper::Sync::filesHandler(desktop.temporary.path() + "/drive.societycontainer");
        bool transportValid = true;
        connect(&client, &iiWebServer::LanPeer::completed, this, [&](const auto &id, const auto &result, const auto &transport) {
            transportValid &= transport == QString("local"); syncing.receive(id, result); phonePhotos.receive(id, result);
        });
        CTEST_CHECK(host.startHost("desktop", "Desktop", [&](const auto &peer, const auto &packet) {
            if (packet.value("op") == "society.photos") return hostPhotos.handle(peer, packet);
            return packet.value("op") == "society.sync" ? hosting.handle(peer, packet) : files(peer, packet);
        }, {"127.0.0.1"}, iiWebServer::Address::loopback()));
        CTEST_CHECK(client.join(host.createOffer(), "phone", "Phone")); CTEST_WAIT_MESSAGE_DEFAULT(client.connected(), qPrintable(client.errorString()));
        hosting.open(desktop.temporary.path() + "/drive.societycontainer", QString(64, 'a'));
        syncing.open(phone.temporary.path() + "/drive.societycontainer", QString(64, 'a'));
        CTEST_WAIT_DEFAULT(hosting.available() && syncing.available());
        hosting.setPeers({"phone"}, {});
        photo_test::SignalCounter mirrored(&syncing, &iiSocietyHelper::Sync::Controller::synchronized);
        syncing.setPeers({"desktop"}, {"desktop"});
        CTEST_WAIT_MESSAGE_TIMEOUT(!mirrored.isEmpty(), qPrintable(syncing.errorString()), 15000);
        const auto desktopId = desktopRecord.value("id").toString();
        // The host index publishes metadata in batches. The first successful
        // cycle can contain only its directories, before the alias is indexed.
        CTEST_WAIT_VERIFY(QFile::exists(phone.photos() + '/' + desktopId + ".societyphoto"), 15000);
        phone.store = std::make_unique<PhotoStore>(phone.temporary.path() + "/drive.societycontainer", phone.library); CTEST_CHECK(phone.store->open());
        CTEST_CHECK(phone.store->importLibrary());
        QJsonObject phoneRecord;
        for (const auto &value : phone.store->catalog())
            if (value.toObject().value("id") != desktopId) phoneRecord = value.toObject();
        CTEST_CHECK(!phoneRecord.isEmpty());
        CTEST_WAIT_COMPARE_TIMEOUT(PhotoStore::digestFile(phone.store->previewPath(desktopId)), PhotoStore::digestFile(desktop.store->previewPath(desktopId)), 15000);
        connect(&hostPhotos, &PhotoController::contentsChanged, &hosting, &iiSocietyHelper::Sync::Controller::synchronizeNow);
        connect(&phonePhotos, &PhotoController::contentsChanged, &syncing, &iiSocietyHelper::Sync::Controller::synchronizeNow);
        connect(&syncing, &iiSocietyHelper::Sync::Controller::synchronized, &phonePhotos, &PhotoController::refresh);
        hostPhotos.configure(desktop.temporary.path() + "/drive.societycontainer", true, {"phone"});
        CTEST_WAIT_VERIFY(!hostPhotos.busy(), 5000);
        phonePhotos.configure(phone.temporary.path() + "/drive.societycontainer", true, {"desktop"}, {"desktop"});
        CTEST_WAIT_VERIFY(QFileInfo::exists(desktop.store->resourcePath(phoneRecord.value("id").toString(), 0)), 20000);
        CTEST_COMPARE(desktop.library->imports.load(), 0);
        CTEST_COMPARE(phone.library->imports.load(), 0);
        CTEST_WAIT_VERIFY(!phonePhotos.busy(), 10000);
        phonePhotos.downloadPhoto(desktopId);
        CTEST_WAIT_VERIFY(QFileInfo::exists(phone.temporary.path() + "/drive.societycontainer/.society-photos/"
            + iiSocietyContainer::SocietyDrive::open(phone.temporary.path() + "/drive.societycontainer")->identifier()
            + "/originals/" + desktopRecord.value("resources").toArray().first().toObject().value("hash").toString()), 20000);
        CTEST_WAIT_VERIFY(!phonePhotos.busy(), 10000);
        CTEST_COMPARE(desktop.library->exports.load() > 0, true);
        CTEST_COMPARE(phone.library->exports.load() > 0, true);
        phonePhotos.trashPhoto(desktopId);
        CTEST_COMPARE(phone.library->removals.load(), 0);
        const auto remotelyDeleted = [&] {
            QFile file(desktop.photos() + '/' + desktopId + ".societyphoto");
            return file.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(file.readAll()).object().value("deleted").toBool();
        };
        CTEST_WAIT_VERIFY(remotelyDeleted(), 10000);
        hostPhotos.refresh(); CTEST_WAIT_VERIFY(!hostPhotos.busy(), 5000);
        CTEST_COMPARE(desktop.library->removals.load(), 0);
        CTEST_CHECK(transportValid);
        phonePhotos.configure({}, false); hostPhotos.configure({}, false);
        syncing.closeAndWait(); hosting.closeAndWait(); client.stop(); host.stop();
    }
};
int main(int argc, char **argv) {
#ifdef IIPHOTOLIBRARY_TEST_LEGACY_QML
    lvrs::AppBootstrapOptions options;
    options.applicationName = "iiPhotoLibraryTests"; options.quickStyleName = "Basic";
    options.bootstrapGraphicsBackend = false; options.configureRenderQualityDefaults = false;
    options.logBootstrapDiagnostics = false; options.logGraphicsBackend = false;
    if (!lvrs::preApplicationBootstrap(options).ok) return 1;
#endif
    QGuiApplication app(argc, argv);
#ifdef IIPHOTOLIBRARY_TEST_LEGACY_QML
    lvrs::postApplicationBootstrap(app, options);
#endif
    PhotosTest test;
    photo_test::CTestRunner runner(argc, argv);
    runner.run("finalShutdownDoesNotWaitForABlockedPhotoOperation", [&] { photo_test::ScopedDeferredDeletes cleanup; test.finalShutdownDoesNotWaitForABlockedPhotoOperation(); });
    runner.run("fixtureRootIsProjectResources", [&] { photo_test::ScopedDeferredDeletes cleanup; test.fixtureRootIsProjectResources(); });
    runner.run("defaultRefreshIgnoresUnselectedNativePhotosAndPreservesContainerFiles", [&] { photo_test::ScopedDeferredDeletes cleanup; test.defaultRefreshIgnoresUnselectedNativePhotosAndPreservesContainerFiles(); });
    runner.run("explicitLibraryImportDoesNotAdoptLaterNativePhotosDuringRefresh", [&] { photo_test::ScopedDeferredDeletes cleanup; test.explicitLibraryImportDoesNotAdoptLaterNativePhotosDuringRefresh(); });
    runner.run("permissionGrantedForAnOldContainerDoesNotImportIntoItsReplacement", [&] { photo_test::ScopedDeferredDeletes cleanup; test.permissionGrantedForAnOldContainerDoesNotImportIntoItsReplacement(); });
    runner.run("openingAndReadingAnEmptyStoreCreatesNoPhotoDirectories", [&] { photo_test::ScopedDeferredDeletes cleanup; test.openingAndReadingAnEmptyStoreCreatesNoPhotoDirectories(); });
    runner.run("photoDiscoveryAndOriginalPreparationCreateOnlyRequiredParents", [&] { photo_test::ScopedDeferredDeletes cleanup; test.photoDiscoveryAndOriginalPreparationCreateOnlyRequiredParents(); });
    runner.run("emptyRefreshAndAutomaticConnectionDoNotPersistPhotoState", [&] { photo_test::ScopedDeferredDeletes cleanup; test.emptyRefreshAndAutomaticConnectionDoNotPersistPhotoState(); });
    runner.run("emptyScaffoldingFromAnOlderSessionDoesNotBecomePersistentPhotoState", [&] { photo_test::ScopedDeferredDeletes cleanup; test.emptyScaffoldingFromAnOlderSessionDoesNotBecomePersistentPhotoState(); });
    runner.run("transferCreatesStagingAndCommitParentsWithoutAnInitialRefresh", [&] { photo_test::ScopedDeferredDeletes cleanup; test.transferCreatesStagingAndCommitParentsWithoutAnInitialRefresh(); });
    runner.run("galleryIndexesOutwardAndReprioritizesWithoutExportingOriginals", [&] { photo_test::ScopedDeferredDeletes cleanup; test.galleryIndexesOutwardAndReprioritizesWithoutExportingOriginals(); });
    runner.run("galleryIndexSurvivesCancellationReopenAndRepairsEditedOrMissingPreviews", [&] { photo_test::ScopedDeferredDeletes cleanup; test.galleryIndexSurvivesCancellationReopenAndRepairsEditedOrMissingPreviews(); });
    runner.run("compactGalleryIsPublishedBeforeReadingIndividualReferences", [&] { photo_test::ScopedDeferredDeletes cleanup; test.compactGalleryIsPublishedBeforeReadingIndividualReferences(); });
    runner.run("explicitConnectionRequestsPhotoAccessAndPublishesAfterGrant", [&] { photo_test::ScopedDeferredDeletes cleanup; test.explicitConnectionRequestsPhotoAccessAndPublishesAfterGrant(); });
    runner.run("automaticPhotoAccessWaitsForAContainerAndNeverOpensSettings", [&] { photo_test::ScopedDeferredDeletes cleanup; test.automaticPhotoAccessWaitsForAContainerAndNeverOpensSettings(); });
    runner.run("indexedPhotosAppearBeforeASlowOriginalFinishesAndReportRealProgress", [&] { photo_test::ScopedDeferredDeletes cleanup; test.indexedPhotosAppearBeforeASlowOriginalFinishesAndReportRealProgress(); });
    runner.run("cancelledPhotoWorkReleasesItsFileLockBeforeBackgroundDrainReturns", [&] { photo_test::ScopedDeferredDeletes cleanup; test.cancelledPhotoWorkReleasesItsFileLockBeforeBackgroundDrainReturns(); });
    runner.run("hashingCancellationStopsBeforePublishingAPartialDigest", [&] { photo_test::ScopedDeferredDeletes cleanup; test.hashingCancellationStopsBeforePublishingAPartialDigest(); });
    runner.run("incrementalPhotoBatchesKeepEveryPreviouslyPublishedRecord", [&] { photo_test::ScopedDeferredDeletes cleanup; test.incrementalPhotoBatchesKeepEveryPreviouslyPublishedRecord(); });
    runner.run("portableNamesKeepTheirExtensionAndPlatformLimits", [&] { photo_test::ScopedDeferredDeletes cleanup; test.portableNamesKeepTheirExtensionAndPlatformLimits(); });
    runner.run("nativeOriginalsRemainAliasesAndPreviewsPersist", [&] { photo_test::ScopedDeferredDeletes cleanup; test.nativeOriginalsRemainAliasesAndPreviewsPersist(); });
    runner.run("duplicatesPreserveAllNativeResourcesAndShareOriginalBytes", [&] { photo_test::ScopedDeferredDeletes cleanup; test.duplicatesPreserveAllNativeResourcesAndShareOriginalBytes(); });
    runner.run("containerOwnedCopiesAndReceivedRevisionsKeepTheirOriginals", [&] { photo_test::ScopedDeferredDeletes cleanup; test.containerOwnedCopiesAndReceivedRevisionsKeepTheirOriginals(); });
    runner.run("localAdditionStaysInTheContainerAndPreservesTheUsersFile", [&] { photo_test::ScopedDeferredDeletes cleanup; test.localAdditionStaysInTheContainerAndPreservesTheUsersFile(); });
    runner.run("transferChecksHashesAndKeepsCommittedOriginalsInsideTheContainer", [&] { photo_test::ScopedDeferredDeletes cleanup; test.transferChecksHashesAndKeepsCommittedOriginalsInsideTheContainer(); });
    runner.run("legacyLayoutKeepsNativeReferencesAndOriginals", [&] { photo_test::ScopedDeferredDeletes cleanup; test.legacyLayoutKeepsNativeReferencesAndOriginals(); });
    runner.run("deniedAndLimitedAccessNeverErasePhotos", [&] { photo_test::ScopedDeferredDeletes cleanup; test.deniedAndLimitedAccessNeverErasePhotos(); });
    runner.run("corruptTransferAndRedirectedCacheAreRejected", [&] { photo_test::ScopedDeferredDeletes cleanup; test.corruptTransferAndRedirectedCacheAreRejected(); });
    runner.run("originalEditUpdatesOneObjectAndTrashIsExplicit", [&] { photo_test::ScopedDeferredDeletes cleanup; test.originalEditUpdatesOneObjectAndTrashIsExplicit(); });
    runner.run("unavailableAssetDoesNotBlockOtherPhotos", [&] { photo_test::ScopedDeferredDeletes cleanup; test.unavailableAssetDoesNotBlockOtherPhotos(); });
    runner.run("mediaDatabaseReplacementDoesNotBecomeGlobalDeletion", [&] { photo_test::ScopedDeferredDeletes cleanup; test.mediaDatabaseReplacementDoesNotBecomeGlobalDeletion(); });
    runner.run("anotherTransferKeepsItsOriginalLease", [&] { photo_test::ScopedDeferredDeletes cleanup; test.anotherTransferKeepsItsOriginalLease(); });
    runner.run("expiredViewingCopiesAreRemovedWithoutTouchingNativeOriginals", [&] { photo_test::ScopedDeferredDeletes cleanup; test.expiredViewingCopiesAreRemovedWithoutTouchingNativeOriginals(); });
    runner.run("unchangedRefreshDoesNotStartAnotherFileSync", [&] { photo_test::ScopedDeferredDeletes cleanup; test.unchangedRefreshDoesNotStartAnotherFileSync(); });
    runner.run("partialNativeImportCannotReplaceACompleteAlias", [&] { photo_test::ScopedDeferredDeletes cleanup; test.partialNativeImportCannotReplaceACompleteAlias(); });
    runner.run("remoteResourceVersionIsNotOverwrittenByAnUnchangedNativeCopy", [&] { photo_test::ScopedDeferredDeletes cleanup; test.remoteResourceVersionIsNotOverwrittenByAnUnchangedNativeCopy(); });
    runner.run("completedRequestCacheDoesNotStopLongTransfers", [&] { photo_test::ScopedDeferredDeletes cleanup; test.completedRequestCacheDoesNotStopLongTransfers(); });
    runner.run("galleryOrdersPhotosAndVideosFromOldestToNewest", [&] { photo_test::ScopedDeferredDeletes cleanup; test.galleryOrdersPhotosAndVideosFromOldestToNewest(); });
#ifdef IIPHOTOLIBRARY_TEST_LEGACY_QML
    runner.run("chronologicalGalleryOpensAtNewestAndPreservesBrowsing:mobile", [&] { photo_test::ScopedDeferredDeletes cleanup; test.chronologicalGalleryOpensAtNewestAndPreservesBrowsing(QSize(390, 844), "mobile"); });
    runner.run("chronologicalGalleryOpensAtNewestAndPreservesBrowsing:desktop", [&] { photo_test::ScopedDeferredDeletes cleanup; test.chronologicalGalleryOpensAtNewestAndPreservesBrowsing(QSize(960, 720), "desktop"); });
    runner.run("galleryRendersAndLatePreviewKeepsBrowsingPosition", [&] { photo_test::ScopedDeferredDeletes cleanup; test.galleryRendersAndLatePreviewKeepsBrowsingPosition(); });
#endif
    runner.run("authenticatedExchangeUploadsLocalAndDownloadsOnlySelectedOriginal", [&] { photo_test::ScopedDeferredDeletes cleanup; test.authenticatedExchangeUploadsLocalAndDownloadsOnlySelectedOriginal(); });
    runner.run("nativeLibrariesExchangeAliasesPreviewsAndOriginalsOverTls", [&] { photo_test::ScopedDeferredDeletes cleanup; test.nativeLibrariesExchangeAliasesPreviewsAndOriginalsOverTls(); });
    return runner.result();
}
