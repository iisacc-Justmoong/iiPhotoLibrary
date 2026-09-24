#include <iiPhotoLibrary/PhotoLibrary.h>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace iiPhotoLibrary {
namespace {
using Microsoft::WRL::ComPtr;
struct ComScope {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(status)) CoUninitialize(); }
};
QString pathOf(IShellItem *item) {
    PWSTR value = nullptr;
    if (!item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) return {};
    const auto path = QDir::fromNativeSeparators(QString::fromWCharArray(value)); CoTaskMemFree(value); return path;
}
QStringList libraryFolders(REFKNOWNFOLDERID identifier) {
    QStringList result; ComPtr<IShellLibrary> library;
    if (FAILED(SHLoadLibraryFromKnownFolder(identifier, STGM_READ, IID_PPV_ARGS(&library)))) return result;
    ComPtr<IShellItemArray> folders;
    if (FAILED(library->GetFolders(LFF_FORCEFILESYSTEM, IID_PPV_ARGS(&folders)))) return result;
    DWORD count = 0; folders->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(folders->GetItemAt(i, &folder))) { const auto path = pathOf(folder.Get()); if (!path.isEmpty()) result.append(path); }
    }
    return result;
}
QStringList roots() {
    auto result = libraryFolders(FOLDERID_PicturesLibrary); result += libraryFolders(FOLDERID_VideosLibrary);
    for (const auto &path : {QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)})
        if (!path.isEmpty()) result.append(path);
    result.removeDuplicates(); return result;
}
bool sourceAllowed(const QString &path) {
    const QFileInfo info(path); if (!info.isFile() || info.isSymLink() || info.isJunction()) return false;
    for (const auto &root : roots()) {
        const auto relative = QDir(root).relativeFilePath(info.canonicalFilePath());
        if (!relative.startsWith("../") && relative != ".." && !QDir::isAbsolutePath(relative)) return true;
    }
    return false;
}
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
QString digestFile(const QString &path) {
    QFile file(path); QCryptographicHash digest(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && digest.addData(&file) ? QString::fromLatin1(digest.result().toHex()) : QString();
}
class WindowsLibrary final : public PhotoLibrary {
public:
    QString name() const override { return "Windows photo libraries"; }
    Access access() const override { return Access::Full; }
    void authorize(std::function<void()> finished) override { finished(); }
    Snapshot scan(QString *error) override {
        ComScope com; Snapshot snapshot; snapshot.complete = true; QSet<QString> seen; QMap<QString, Asset> grouped;
        const auto folders = roots();
        for (const auto &root : folders) {
            WCHAR volume[MAX_PATH]{}; DWORD serial = 0;
            if (GetVolumePathNameW(reinterpret_cast<PCWSTR>(root.utf16()), volume, MAX_PATH))
                GetVolumeInformationW(volume, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
            snapshot.revision += root + ':' + QString::number(serial) + ';';
            if (!QFileInfo(root).isReadable() || !QFileInfo(root).isDir()) { snapshot.complete = false; continue; }
            QDirIterator files(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                const auto path = files.next(); const QFileInfo info(path);
                if (info.isSymLink() || info.isJunction() || seen.contains(info.canonicalFilePath())) continue;
                seen.insert(info.canonicalFilePath());
                const auto mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
                const auto media = mime.startsWith("image/") ? QString("photo") : mime.startsWith("video/") ? QString("video") : QString();
                if (media.isEmpty() || !info.isReadable()) continue;
                const auto imported = importedPhotoId(info.fileName()); const auto key = imported.isEmpty() ? info.canonicalFilePath() : imported;
                const int index = imported.isEmpty() ? 0 : info.fileName().mid(73, 1).toInt();
                auto &asset = grouped[key];
                if (asset.identifier.isEmpty() || index == 0) { asset.identifier = info.canonicalFilePath(); asset.name = info.fileName(); asset.media = media; asset.created = info.birthTime(); }
                asset.stamp += QString::number(info.lastModified().toMSecsSinceEpoch()) + ':' + QString::number(info.size()) + ';';
                asset.resources.append({info.canonicalFilePath(), info.fileName(), index == 0 ? media : media == "video" ? QString("pairedVideo") : QString("alternatePhoto")});
            }
        }
        for (auto &asset : grouped) {
            std::sort(asset.resources.begin(), asset.resources.end(), [](const Resource &a, const Resource &b) { return a.name < b.name; });
            snapshot.assets.append(asset);
        }
        if (folders.isEmpty()) { snapshot.complete = false; fail(error, "The Windows Pictures library is unavailable."); }
        return snapshot;
    }
    bool preview(const QString &id, const QString &path, QString *error) override {
        ComScope com; if (!sourceAllowed(id)) return fail(error, "The library photo is unavailable.");
        ComPtr<IShellItemImageFactory> factory;
        if (FAILED(SHCreateItemFromParsingName(reinterpret_cast<PCWSTR>(id.utf16()), nullptr, IID_PPV_ARGS(&factory)))) return fail(error, "The photo thumbnail is unavailable.");
        HBITMAP bitmap = nullptr;
        if (FAILED(factory->GetImage({1024, 1024}, SIIGBF_THUMBNAILONLY, &bitmap))) return fail(error, "Windows is preparing this photo or video preview.");
        BITMAP shape{}; GetObject(bitmap, sizeof(shape), &shape);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = shape.bmWidth;
        info.bmiHeader.biHeight = -shape.bmHeight; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        QImage image(shape.bmWidth, shape.bmHeight, QImage::Format_RGB32); HDC dc = GetDC(nullptr);
        const bool ok = GetDIBits(dc, bitmap, 0, shape.bmHeight, image.bits(), &info, DIB_RGB_COLORS) != 0;
        ReleaseDC(nullptr, dc); DeleteObject(bitmap);
        return ok && image.save(path, "JPEG", 85) ? true : fail(error, "Could not save the photo preview.");
    }
    bool exportResource(const QString &, const QString &token, const QString &path, QString *error) override {
        ComScope com;
        return sourceAllowed(token) && QFile::copy(token, path) ? true : fail(error, "The photo original is unavailable. Check its cloud download status.");
    }
    QString import(const QJsonObject &record, const QStringList &paths, QString *error) override {
        ComScope com; const auto directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Society";
        if (!QDir().mkpath(directory) || QFileInfo(directory).isSymLink() || QFileInfo(directory).isJunction()) { fail(error, "Could not open the Society Pictures folder."); return {}; }
        const auto resources = record.value("resources").toArray(); if (resources.size() != paths.size()) return {};
        QString first;
        for (int i = 0; i < resources.size(); ++i) {
            const auto path = QDir(directory).filePath(importedResourceName(record.value("id").toString(), i, resources[i].toObject().value("name").toString()));
            if (QFileInfo(path).isSymLink() || QFileInfo(path).isJunction()) { fail(error, "The photo destination was redirected."); return {}; }
            const auto expected = resources[i].toObject().value("hash").toString();
            if (QFile::exists(path)) {
                if (digestFile(path) != expected) { fail(error, "This photo has a different native original. Its existing original was preserved."); return {}; }
            } else {
                QFile input(paths[i]); QSaveFile output(path);
                if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) { fail(error, "Could not add the photo to Windows Pictures."); return {}; }
                QCryptographicHash digest(QCryptographicHash::Sha256);
                while (!input.atEnd()) {
                    const auto bytes = input.read(256 * 1024);
                    if (bytes.isEmpty() || output.write(bytes) != bytes.size()) { fail(error, "The photo import was interrupted."); return {}; }
                    digest.addData(bytes);
                }
                if (QString::fromLatin1(digest.result().toHex()) != expected || !output.commit()) { fail(error, "Could not verify the complete photo original."); return {}; }
            }
            if (first.isEmpty()) first = QFileInfo(path).canonicalFilePath();
        }
        return first;
    }
    bool trash(const QString &id, QString *error) override {
        ComScope com; const auto snapshot = scan(error);
        if (error && !error->isEmpty()) return false;
        const auto imported = importedPhotoId(QFileInfo(id).fileName());
        for (const auto &asset : snapshot.assets) {
            if (asset.identifier != id && (imported.isEmpty() || importedPhotoId(asset.name) != imported)) continue;
            for (const auto &resource : asset.resources) {
                if (!sourceAllowed(resource.token)) return fail(error, "The library photo is unavailable.");
                QFile file(resource.token);
                // Qt verifies the per-item Shell result and requires recycling.
                if (!file.moveToTrash()) return fail(error, "Could not move the photo to the Recycle Bin: " + file.errorString());
            }
            return true;
        }
        return !QFile::exists(id) || fail(error, "The library photo is unavailable.");
    }
};
}
std::shared_ptr<PhotoLibrary> nativePhotoLibrary() { return std::make_shared<WindowsLibrary>(); }
}
