#include <iiFileProviderDevice.h>
#include <iiPhotoLibrary/PhotoStore.h>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUuid>
#include <QUrl>
#include <QElapsedTimer>
#include <limits>
#include <algorithm>

namespace iiPhotoLibrary {
namespace {
bool hex(const QString &s) { static const QRegularExpression rx("\\A[a-f0-9]{64}\\z"); return rx.match(s).hasMatch(); }
QString hash(const QByteArray &data) { return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()); }
QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
bool ordinary(const QString &path, bool directory = false) {
    const QFileInfo info(path); return !info.isSymLink() && !info.isJunction() && (directory ? info.isDir() : info.isFile());
}
bool confined(const QString &root, const QString &path) {
    const auto relative = iiFileProvider::Directory(root).relativeFilePath(path);
    if (relative == ".." || relative.startsWith("../") || iiFileProvider::Directory::isAbsolutePath(relative)) return false;
    QString current = root;
    if (!ordinary(root, true)) return false;
    for (const auto &part : relative.split('/')) {
        if (part == ".") continue;
        current += '/' + part; const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return false;
    }
    return true;
}
bool prepareParent(const QString &root, const QString &path) {
    return confined(root, path) && iiFileProvider::Directory().mkpath(QFileInfo(path).absolutePath()) && confined(root, path);
}
bool hasNoFiles(const QString &root, const QString &directory) {
    if (!QFileInfo::exists(directory)) return true;
    if (!ordinary(directory, true)) return false;
    QDirIterator entries(directory, iiFileProvider::Directory::AllEntries | iiFileProvider::Directory::Hidden | iiFileProvider::Directory::System | iiFileProvider::Directory::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        const auto path = entries.next();
        if (!confined(root, path) || !ordinary(path, true)) return false;
    }
    return true;
}
bool writeJson(const QString &root, const QString &path, const QJsonObject &value) {
    if (!prepareParent(root, path)) return false;
    const auto bytes = json(value); iiFileProvider::SaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject readJson(const QString &path) {
    iiFileProvider::FileDevice file(path); if (!ordinary(path) || file.size() > 256 * 1024 || !file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString contentId(const QJsonArray &resources) {
    QByteArray bytes("society-photo-v1\n");
    for (const auto &value : resources) {
        const auto r = value.toObject(); bytes += r.value("role").toString().toUtf8() + ':' + r.value("hash").toString().toLatin1() + '\n';
    }
    return hash(bytes);
}
QString safeName(const QString &name) {
    auto result = name;
    for (auto &character : result)
        if (character.unicode() < 32 || QStringLiteral("/\\:<>\"|?*").contains(character)) character = '_';
    if (result.toUtf8().size() > 160) {
        const auto extension = QFileInfo(result).suffix();
        const auto suffix = !extension.isEmpty() && extension.toUtf8().size() <= 24 ? '.' + extension : QString();
        if (!suffix.isEmpty()) result.chop(suffix.size());
        while (result.toUtf8().size() + suffix.toUtf8().size() > 160) {
            if (result.back().isLowSurrogate() && result.size() > 1) result.chop(2); else result.chop(1);
        }
        result += suffix;
    }
    return result.isEmpty() ? QStringLiteral("photo") : result;
}
}
QString importedResourceName(const QString &id, int index, const QString &name) {
    return "Society-" + id + '-' + QString::number(index) + '-' + safeName(name);
}
QString importedPhotoId(const QString &name) {
    if (!name.startsWith("Society-") || name.size() < 76 || name[72] != '-'
        || name[73] < '0' || name[73] > '7' || name[74] != '-') return {};
    const auto id = name.mid(8, 64); return hex(id) ? id : QString();
}
QString accessName(Access access) {
    switch (access) {
    case Access::Full: return "full"; case Access::Limited: return "limited";
    case Access::NotDetermined: return "notDetermined"; case Access::Denied: return "denied";
    default: return "unsupported";
    }
}
PhotoStore::PhotoStore(QString root, std::shared_ptr<PhotoLibrary> library)
    : m_root(std::move(root)), m_library(std::move(library)) {}
bool PhotoStore::fail(const QString &error) { m_error = error; return false; }
QString PhotoStore::containerId() const { return m_drive ? m_drive->identifier() : QString(); }
bool PhotoStore::open(const GalleryProgress &published) {
    m_drive = iiSocietyContainer::SocietyDrive::open(m_root, &m_error);
    if (!m_drive || !m_drive->isReady()) return fail("The Society container is not ready.");
    m_photos = m_drive->sectionPath(iiSocietyContainer::StoreSection::Photos);
    m_private = iiFileProvider::Directory(m_root).filePath(".society-photos/" + m_drive->identifier());
    if (!confined(m_root, m_private) || !confined(m_root, m_photos)) return fail("The photo storage location was redirected.");
    if (!intact()) return false;
    if (published) published(loadGalleryCatalog(), true);
    return load();
}
bool PhotoStore::intact() {
    if (m_cancelled || !m_drive || !m_drive->isValid() || !m_drive->isReady()) return fail("The photo session ended or the container changed.");
    for (const auto &path : {m_private, m_private + "/index.json", m_private + "/gallery-catalog.json", m_private + "/operation.lock", m_private + "/gallery", m_private + "/references", m_private + "/originals", m_private + "/incoming", m_photos, m_photos + "/.previews"})
        if (!confined(m_root, path)) return fail("The photo storage location was redirected.");
    return true;
}
bool PhotoStore::load() {
    if (!intact()) return false;
    const auto path = m_private + "/index.json";
    if (iiFileProvider::FileDevice::exists(path)) {
        m_local = readJson(path);
        if (m_local.value("schema") != 1 || m_local.value("container") != containerId()) return fail("The local photo index is invalid.");
    } else m_local = {{"schema", 1}, {"container", containerId()}};
    m_references.clear();
    QDirIterator references(m_private + "/references", {"*.json"}, iiFileProvider::Directory::Files | iiFileProvider::Directory::NoDotAndDotDot);
    while (references.hasNext()) {
        if (m_cancelled) return fail("The photo session ended.");
        const auto file = references.next(), id = QFileInfo(file).completeBaseName();
        if (!hex(id) || !confined(m_root, file)) return fail("A local photo reference is invalid.");
        const auto reference = readJson(file);
        if (reference.value("identifier").toString().isEmpty()) return fail("A local photo reference could not be read.");
        m_references[id] = reference;
    }
    // Migrate early single-file indexes without discarding the original index
    // until every native reference has been committed individually.
    const auto legacy = m_local.value("assets").toObject();
    for (auto i = legacy.begin(); i != legacy.end(); ++i)
        if (!m_references.contains(i.key()) && !saveReference(i.key(), i.value().toObject())) return false;
    if (!legacy.isEmpty()) { m_local.remove("assets"); if (!save()) return false; }
    m_gallery.clear();
    QDirIterator galleryFiles(m_private + "/gallery", {"*.json"}, iiFileProvider::Directory::Files);
    while (galleryFiles.hasNext()) {
        if (m_cancelled) return false;
        const auto file = galleryFiles.next(), key = QFileInfo(file).completeBaseName();
        if (!hex(key) || !confined(m_root, file)) return fail("The gallery index was redirected.");
        const auto entry = readJson(file);
        if (entry.value("schema") != 1 || entry.value("container") != containerId()
            || hash(entry.value("identifier").toString().toUtf8()) != key) continue;
        m_gallery[key] = entry;
    }
    m_galleryAliases.clear();
    for (auto it = m_references.begin(); it != m_references.end(); ++it)
        m_galleryAliases[hash(it.value().value("identifier").toString().toUtf8())] = it.key();
    catalog(); return m_error.isEmpty();
}
bool PhotoStore::save() {
    return intact() && (readJson(m_private + "/index.json") == m_local
        || writeJson(m_root, m_private + "/index.json", m_local) || fail("Could not save the local photo index."));
}
bool PhotoStore::saveReference(const QString &id, const QJsonObject &reference) {
    if (!intact() || !hex(id)) return false;
    const auto path = m_private + "/references/" + id + ".json";
    if (!confined(m_root, path)) return fail("The local photo reference was redirected.");
    if (m_references.value(id) != reference && !writeJson(m_root, path, reference)) return fail("Could not save the local photo reference.");
    m_references[id] = reference; return true;
}
QString PhotoStore::digestFile(const QString &path, QString *error, const std::function<bool()> &cancelled) {
    iiFileProvider::FileDevice file(path); if (!ordinary(path) || !file.open(QIODevice::ReadOnly)) { if (error) *error = "Could not read the original resource."; return {}; }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray chunk(1024 * 1024, Qt::Uninitialized);
    while (!file.atEnd()) {
        if (cancelled && cancelled()) { if (error) *error = "The photo session ended."; return {}; }
        const auto bytes = file.read(chunk.data(), chunk.size());
        if (bytes <= 0) { if (error) *error = "Could not hash the original resource."; return {}; }
        digest.addData(QByteArrayView(chunk.constData(), bytes));
    }
    return QString::fromLatin1(digest.result().toHex());
}
QString PhotoStore::hashResource(const QString &path, QString *error) const {
    return digestFile(path, error, [this] { return m_cancelled.load(); });
}
bool PhotoStore::validRecord(const QJsonObject &record) {
    if (record.value("schema") != 1 || !hex(record.value("id").toString())
        || record.value("name").toString().isEmpty() || record.value("name").toString().size() > 256
        || (record.value("media") != "photo" && record.value("media") != "video")
        || record.contains("identifier") || record.contains("path") || record.contains("token")
        || json(record).size() > 32 * 1024) return false;
    const auto resources = record.value("resources").toArray(); if (resources.isEmpty() || resources.size() > 8) return false;
    for (const auto &value : resources) {
        const auto r = value.toObject(); bool ok; const auto size = r.value("size").toString().toLongLong(&ok);
        const auto role = r.value("role").toString(), name = r.value("name").toString();
        if (!ok || size <= 0 || size > (1LL << 40) || !hex(r.value("hash").toString())
            || name.isEmpty() || name.size() > 256 || name.contains('/') || name.contains('\\') || name.contains(QChar::Null)
            || (role != "photo" && role != "video" && role != "pairedVideo" && role != "alternatePhoto")) return false;
    }
    return true;
}
QJsonArray PhotoStore::catalog() {
    QJsonArray result; m_paths.clear();
    if (!intact()) return result;
    QDirIterator it(m_photos, iiFileProvider::Directory::Files | iiFileProvider::Directory::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (m_cancelled) { fail("The photo session ended."); return {}; }
        const auto path = it.next(); if (!path.endsWith(".societyphoto")) continue;
        if (!confined(m_root, path)) { fail("A photo alias was redirected."); break; }
        const auto record = readJson(path); if (!validRecord(record)) continue;
        const auto id = record.value("id").toString();
        if (m_paths.contains(id)) continue;
        m_paths.insert(id, path); result.append(record);
        if (result.size() >= 100000) { fail("The photo catalog limit was reached."); break; }
    }
    QList<QJsonObject> sorted; for (const auto &value : result) sorted.append(value.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.value("id").toString() < b.value("id").toString(); });
    result = {}; for (const auto &record : sorted) result.append(record);
    return result;
}
QJsonObject PhotoStore::readRecord(const QString &id) const {
    if (m_cancelled) return {};
    const auto path = m_paths.value(id, m_photos + '/' + id + ".societyphoto"); if (!hex(id) || !confined(m_root, path)) return {};
    const auto record = readJson(path); return validRecord(record) && record.value("id") == id ? record : QJsonObject();
}
bool PhotoStore::writeRecord(const QJsonObject &record, const QString &location) {
    if (!intact() || !validRecord(record)) return fail("The photo object is invalid.");
    const auto id = record.value("id").toString();
    const auto path = location.isEmpty() ? m_paths.value(id, m_photos + '/' + id + ".societyphoto") : location;
    if (!confined(m_photos, path)) return fail("The photo object is outside Photos.");
    if (iiFileProvider::FileDevice::exists(path)) {
        const auto existing = readJson(path);
        if (!validRecord(existing) || existing.value("id") != id) return fail("A different file already occupies this photo alias name.");
    }
    if (readJson(path) != record && !writeJson(m_root, path, record)) return fail("Could not save the photo alias.");
    m_paths[id] = path; return true;
}
QString PhotoStore::cachePath(const QString &digest) const {
    if (!hex(digest)) return {};
    const auto path = m_private + "/originals/" + digest;
    return confined(m_root, path) ? path : QString();
}
QString PhotoStore::resourcePath(const QString &id, int index) const {
    const auto resources = readRecord(id).value("resources").toArray();
    return index >= 0 && index < resources.size() ? cachePath(resources[index].toObject().value("hash").toString()) : QString();
}
QString PhotoStore::previewPath(const QString &id) const {
    const auto path = m_photos + "/.previews/" + id + ".jpg";
    return hex(id) && confined(m_root, path) ? path : QString();
}
bool PhotoStore::cacheValid(const QJsonObject &resource) const {
    const auto path = cachePath(resource.value("hash").toString());
    return !path.isEmpty() && ordinary(path) && QFileInfo(path).size() == resource.value("size").toString().toLongLong()
        && hashResource(path) == resource.value("hash").toString();
}
bool PhotoStore::hasOriginal(const QString &id) {
    if (!intact()) return false;
    const auto record = readRecord(id); if (record.isEmpty() || record.value("deleted").toBool()) return false;
    const auto local = m_references.value(id);
    const auto access = m_library->access();
    if ((access == Access::Full || access == Access::Limited) && !local.value("unavailable").toBool()
        && !local.value("identifier").toString().isEmpty() && local.value("content") == contentId(record.value("resources").toArray())) return true;
    for (const auto &resource : record.value("resources").toArray()) if (!cacheValid(resource.toObject())) return false;
    return true;
}
bool PhotoStore::remember(const Asset &asset, const QString &id, const QJsonObject &record) {
    QJsonArray tokens;
    for (const auto &resource : asset.resources) tokens.append(resource.token);
    const QJsonObject reference{{"identifier", asset.identifier}, {"stamp", asset.stamp}, {"cloud", asset.cloudIdentifier},
        {"tokens", tokens}, {"content", contentId(record.value("resources").toArray())}, {"unavailable", false}};
    if (!saveReference(id, reference)) return false;
    m_galleryAliases[hash(asset.identifier.toUtf8())] = id;
    return true;
}
QJsonArray PhotoStore::loadGalleryCatalog() {
    const auto path = m_private + "/gallery-catalog.json";
    if (!confined(m_root, path)) return {};
    iiFileProvider::FileDevice file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) return {};
    const auto cached = QJsonDocument::fromJson(file.readAll()).object();
    if (cached.value("schema") != 1 || cached.value("container") != containerId()) return {};
    QJsonArray rows;
    for (const auto &value : cached.value("rows").toArray()) {
        auto row = value.toObject();
        const auto id = row.value("id").toString(), key = row.value("galleryKey").toString();
        if (!hex(id) || !hex(key)) return {};
        const auto fileName = row.take("previewFile").toString();
        if (!fileName.isEmpty() && fileName != id + ".jpg" && fileName != "gallery-" + key + ".jpg") return {};
        const auto preview = m_photos + "/.previews/" + fileName;
        row["preview"] = fileName.isEmpty() || !confined(m_root, preview) ? QString() : QUrl::fromLocalFile(preview).toString();
        rows.append(row);
        if (rows.size() > 100000) return {};
    }
    return rows;
}
bool PhotoStore::saveGalleryCatalog(const QJsonArray &rows) {
    if (!intact()) return false;
    QJsonArray compact;
    for (const auto &value : rows) {
        auto row = value.toObject();
        row["previewFile"] = QFileInfo(QUrl(row.take("preview").toString()).toLocalFile()).fileName();
        compact.append(row);
    }
    return writeJson(m_root, m_private + "/gallery-catalog.json", {{"schema", 1}, {"container", containerId()}, {"rows", compact}})
        || fail("Could not save the gallery startup catalog.");
}
void PhotoStore::prioritize(const QStringList &keys) {
    std::lock_guard lock(m_priorityMutex);
    m_visibleKeys = keys;
}
QString PhotoStore::galleryPreview(const QString &key) const {
    const auto path = m_photos + "/.previews/gallery-" + key + ".jpg";
    return hex(key) && confined(m_root, path) ? path : QString();
}
bool PhotoStore::saveGallery(const QString &key, const QJsonObject &entry) {
    const auto path = m_private + "/gallery/" + key + ".json";
    if (!intact() || !hex(key) || !confined(m_root, path)) return fail("The gallery index was redirected.");
    if (m_gallery.value(key) != entry && !writeJson(m_root, path, entry)) return fail("Could not save the gallery index.");
    m_gallery[key] = entry;
    return true;
}
QJsonObject PhotoStore::galleryRow(const QString &key) {
    const auto entry = m_gallery.value(key);
    const auto alias = m_galleryAliases.value(key);
    const auto record = readRecord(alias);
    if (record.value("deleted").toBool()) return {};
    auto row = record;
    if (row.isEmpty()) row = {{"id", key}, {"name", entry.value("name")}, {"media", entry.value("media")},
        {"created", entry.value("created")}, {"resources", QJsonArray{}}, {"indexing", true}};
    auto preview = galleryPreview(key);
    // Keep the previous preview visible during a native edit. Replace atomically.
    if (!ordinary(preview)) preview = previewPath(alias);
    const QFileInfo info(preview);
    row["galleryKey"] = key;
    row["preview"] = info.exists() ? QUrl::fromLocalFile(preview).toString() : QString();
    row["previewStamp"] = info.exists() ? QString::number(info.lastModified().toMSecsSinceEpoch()) + ':' + QString::number(info.size()) : QString();
    return row;
}
QJsonArray PhotoStore::gallery() {
    QJsonArray result;
    for (auto it = m_gallery.begin(); it != m_gallery.end(); ++it) {
        if (m_cancelled) return {};
        const auto row = galleryRow(it.key());
        if (!row.isEmpty()) result.append(row);
    }
    return result;
}
bool PhotoStore::indexGallery(const Snapshot &snapshot, const GalleryProgress &progress) {
    // Persist lightweight discovery separately from portable objects, whose
    // resource hashes still require complete originals. Never send this local
    // index or native identifiers to peers.
    QSet<QString> seen;
    for (const auto &asset : snapshot.assets) {
        if (!intact()) return false;
        const auto key = hash(asset.identifier.toUtf8()); seen.insert(key);
        auto entry = m_gallery.value(key);
        entry["schema"] = 1; entry["container"] = containerId();
        entry["identifier"] = asset.identifier; entry["stamp"] = asset.stamp;
        entry["name"] = safeName(asset.name); entry["media"] = asset.media;
        entry["created"] = asset.created.toUTC().toString(Qt::ISODateWithMs);
        if (!saveGallery(key, entry)) return false;
    }
    if (snapshot.complete) {
        for (const auto &key : m_gallery.keys()) if (!seen.contains(key)) {
            const auto path = m_private + "/gallery/" + key + ".json";
            if (!confined(m_root, path) || !iiFileProvider::FileDevice::remove(path)) return fail("Could not update the gallery index.");
            m_gallery.remove(key);
        }
    }
    auto rows = gallery();
    QHash<QString, qsizetype> rowPositions;
    for (qsizetype i = 0; i < rows.size(); ++i) rowPositions[rows[i].toObject().value("galleryKey").toString()] = i;
    if (!saveGalleryCatalog(rows)) return false;
    if (progress) progress(rows, true);
    QElapsedTimer checkpoint; checkpoint.start();
    int pendingCheckpoint = 0;
    QList<Asset> assets = snapshot.assets;
    std::sort(assets.begin(), assets.end(), [](const Asset &a, const Asset &b) {
        return a.created == b.created ? hash(a.identifier.toUtf8()) < hash(b.identifier.toUtf8()) : a.created < b.created;
    });
    QHash<QString, int> positions;
    QList<int> pending;
    for (int i = 0; i < assets.size(); ++i) { positions[hash(assets[i].identifier.toUtf8())] = i; pending.append(i); }
    QStringList appliedKeys; bool first = true;
    while (!pending.isEmpty()) {
        if (!intact()) return false;
        QStringList keys;
        { std::lock_guard lock(m_priorityMutex); keys = m_visibleKeys; }
        if (first || keys != appliedKeys) {
            first = false; appliedKeys = keys;
            int begin = assets.size(), end = -1;
            for (const auto &key : keys) if (positions.contains(key)) {
                begin = std::min(begin, positions[key]); end = std::max(end, positions[key]);
            }
            if (end < 0) { end = assets.size() - 1; begin = std::max(0, end - 23); }
            const auto distance = [begin, end](int index) { return index < begin ? begin - index : std::max(0, index - end); };
            std::sort(pending.begin(), pending.end(), [&](int a, int b) {
                return distance(a) == distance(b) ? a < b : distance(a) < distance(b);
            });
        }
        const auto asset = assets[pending.takeFirst()];
        const auto key = hash(asset.identifier.toUtf8()); auto entry = m_gallery.value(key);
        const auto path = galleryPreview(key);
        if (entry.value("previewStamp") == asset.stamp && ordinary(path) && QImageReader(path).canRead()) continue;
        const auto alias = m_galleryAliases.value(key); const auto local = m_references.value(alias);
        if (readRecord(alias).value("deleted").toBool()) continue;
        const auto canonical = previewPath(alias);
        const auto temporary = m_private + "/incoming/gallery-" + key + ".jpg";
        if (path.isEmpty() || !confined(m_root, temporary)) return fail("The gallery preview was redirected.");
        if (!prepareParent(m_root, temporary)) return fail("Could not prepare the gallery preview.");
        iiFileProvider::FileDevice::remove(temporary);
        QString error;
        const bool reuse = local.value("stamp") == asset.stamp && ordinary(canonical) && QImageReader(canonical).canRead();
        const bool ready = reuse ? iiFileProvider::FileDevice::copy(canonical, temporary) : m_library->preview(asset.identifier, temporary, &error);
        if (!ready || !QImageReader(temporary).canRead()) { iiFileProvider::FileDevice::remove(temporary); continue; }
        iiFileProvider::FileDevice input(temporary); iiFileProvider::SaveFile output(path);
        if (!input.open(QIODevice::ReadOnly) || input.size() > 16 * 1024 * 1024
            || !prepareParent(m_root, path) || !output.open(QIODevice::WriteOnly))
            return fail("Could not save the gallery preview.");
        const auto bytes = input.readAll();
        if (output.write(bytes) != bytes.size() || !output.commit()) return fail("Could not commit the gallery preview.");
        input.close(); iiFileProvider::FileDevice::remove(temporary);
        entry["previewStamp"] = asset.stamp;
        if (!saveGallery(key, entry)) return false;
        const auto row = galleryRow(key);
        if (rowPositions.contains(key)) rows[rowPositions[key]] = row;
        if (progress) progress({row}, false);
        if (++pendingCheckpoint >= 32 || checkpoint.elapsed() >= 1000) {
            if (!saveGalleryCatalog(rows)) return false;
            pendingCheckpoint = 0; checkpoint.restart();
        }
    }
    return saveGalleryCatalog(rows);
}

bool PhotoStore::refresh(const Progress &progress, const GalleryProgress &galleryProgress) {
    return refreshContents(false, progress, galleryProgress);
}
bool PhotoStore::importLibrary(const Progress &progress, const GalleryProgress &galleryProgress) {
    return refreshContents(true, progress, galleryProgress);
}
bool PhotoStore::refreshContents(bool includeLibrary, const Progress &progress, const GalleryProgress &galleryProgress) {
    m_error.clear(); if (!intact()) return false;
    std::optional<Snapshot> initialSnapshot;
    const auto initiallyEmpty = [this] {
        return hasNoFiles(m_root, m_private) && hasNoFiles(m_root, m_photos);
    };
    if (initiallyEmpty()) {
        const auto access = m_library->access();
        if (includeLibrary && (access == Access::Full || access == Access::Limited)) {
            initialSnapshot = m_library->scan(&m_error);
            if (!m_error.isEmpty() || !intact()) return false;
        }
        // An empty first connection has no deletion history or payload to
        // persist. Recheck after scanning before skipping the process lock.
        if ((!initialSnapshot || initialSnapshot->assets.isEmpty()) && initiallyEmpty()) {
            if (galleryProgress) galleryProgress({}, true);
            return true;
        }
    }
    if (!prepareParent(m_root, m_private + "/operation.lock")) return fail("Could not prepare the photo operation.");
    iiFileProvider::LockFile lock(m_private + "/operation.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock()) return fail("Another Society photo operation is in progress.");
    if (!load()) return false;
    // A different process may have committed photo history after preflight.
    // Its persisted state must be compared with a scan taken under our lock.
    if (initialSnapshot && (!m_references.isEmpty() || !m_gallery.isEmpty() || !m_paths.isEmpty()
        || QFileInfo::exists(m_private + "/index.json"))) initialSnapshot.reset();
    pruneExports();
    if (includeLibrary) return refreshImpl(progress, galleryProgress, std::move(initialSnapshot));
    if (!importLooseFiles()) return false;
    for (auto entry = m_gallery.begin(); entry != m_gallery.end(); ++entry) {
        const auto preview = galleryPreview(entry.key());
        const auto canonical = previewPath(m_galleryAliases.value(entry.key()));
        if (ordinary(preview) && QImageReader(preview).canRead()) continue;
        if (!ordinary(canonical) || !QImageReader(canonical).canRead()) continue;
        iiFileProvider::FileDevice input(canonical); iiFileProvider::SaveFile output(preview);
        if (!input.open(QIODevice::ReadOnly) || input.size() > 16 * 1024 * 1024
            || !prepareParent(m_root, preview) || !output.open(QIODevice::WriteOnly))
            return fail("Could not repair the stored gallery preview.");
        const auto bytes = input.readAll();
        if (output.write(bytes) != bytes.size() || !output.commit())
            return fail("Could not repair the stored gallery preview.");
    }
    if (!saveGalleryCatalog(gallery())) return false;
    if (galleryProgress) galleryProgress(gallery(), true);
    return true;
}
bool PhotoStore::refreshImpl(const Progress &progress, const GalleryProgress &galleryProgress, std::optional<Snapshot> initialSnapshot) {
    const auto access = m_library->access();
    if (access != Access::Full && access != Access::Limited) { m_local["complete"] = false; return save() && importLooseFiles(); }
    auto snapshot = initialSnapshot ? std::move(*initialSnapshot) : m_library->scan(&m_error);
    if (!m_error.isEmpty()) { m_local["complete"] = false; save(); return false; }
    if (!intact()) return false;
    if (!snapshot.revision.isEmpty() && m_local.value("libraryRevision").toString() != snapshot.revision)
        m_local["complete"] = false;
    if (!indexGallery(snapshot, galleryProgress)) return false;
    if (!m_library->resolveIdentifiers(snapshot, &m_error)) return false;
    QSet<QString> seen; QHash<QString, QString> nativeIds, cloudIds;
    for (auto i = m_references.begin(); i != m_references.end(); ++i) {
        nativeIds[i->value("identifier").toString()] = i.key();
        const auto cloud = i->value("cloud").toString(); if (!cloud.isEmpty()) cloudIds[cloud] = i.key();
    }
    for (auto i = m_paths.begin(); i != m_paths.end(); ++i) {
        const auto cloud = readRecord(i.key()).value("cloud").toString(); if (!cloud.isEmpty()) cloudIds[cloud] = i.key();
    }
    QString firstError;
    for (const auto &asset : snapshot.assets) {
        const auto update = [&]() -> bool {
        if (!intact()) return false;
        seen.insert(asset.identifier); QString id = nativeIds.value(asset.identifier);
        if (id.isEmpty() && !asset.cloudIdentifier.isEmpty()) id = cloudIds.value(asset.cloudIdentifier);
        if (id.isEmpty()) for (const auto &resource : asset.resources) {
            const auto imported = importedPhotoId(resource.name);
            if (!readRecord(imported).isEmpty()) { id = imported; break; }
        }
        auto record = readRecord(id);
        if (record.value("deleted").toBool()) {
            // Society deletion affects the container alias only. The native
            // library remains a read source, including during explicit import.
            return true;
        }
        const auto local = m_references.value(id);
        if (!record.isEmpty() && asset.resources.size() != record.value("resources").toArray().size()) {
            // A partially published multi-resource native import must not
            // shrink the portable object or advertise missing originals.
            bool imported = false;
            for (const auto &resource : asset.resources) imported |= importedPhotoId(resource.name) == id;
            if (imported) {
                if (!local.value("identifier").toString().isEmpty()) {
                    auto unavailable = local; unavailable["unavailable"] = true;
                    if (!saveReference(id, unavailable)) return false;
                }
                return fail("Waiting for all resources of this photo to finish importing.");
            }
        }
        // A remote resource revision must not be replaced by an unchanged
        // local copy while that revision is waiting to arrive.
        if (!record.isEmpty() && local.value("stamp") == asset.stamp
            && local.value("content") != contentId(record.value("resources").toArray())) return true;
        if (!record.isEmpty() && local.value("stamp") == asset.stamp && !local.value("unavailable").toBool()
            && local.value("content") == contentId(record.value("resources").toArray()) && iiFileProvider::FileDevice::exists(previewPath(id))) return true;
        // Imports discovered after a crash retain the originating Society ID.
        QJsonArray resources;
        for (const auto &resource : asset.resources) {
            const auto temporary = m_private + "/incoming/export-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!prepareParent(m_root, temporary)) return fail("Could not prepare the photo original.");
            if (!m_library->exportResource(asset.identifier, resource.token, temporary, &m_error)) { iiFileProvider::FileDevice::remove(temporary); return false; }
            const auto digest = hashResource(temporary, &m_error); const auto size = QFileInfo(temporary).size();
            if (digest.isEmpty() || size <= 0) { iiFileProvider::FileDevice::remove(temporary); return fail("The photo original is unavailable."); }
            // Discovery hashes a transient native export. Retain only the
            // reference and preview; a transfer stages an original on demand.
            iiFileProvider::FileDevice::remove(temporary);
            QString name = resource.name;
            const int resourceIndex = resources.size();
            if (!record.isEmpty() && resourceIndex < record.value("resources").toArray().size()) name = record.value("resources").toArray()[resourceIndex].toObject().value("name").toString();
            resources.append(QJsonObject{{"name", safeName(name)}, {"role", resource.role}, {"hash", digest}, {"size", QString::number(size)}});
        }
        if (resources.isEmpty()) return true;
        if (id.isEmpty()) id = contentId(resources);
        if (record.isEmpty()) record = readRecord(id); // Identical bytes from another library.
        if (record.isEmpty()) record = {{"schema", 1}, {"id", id}, {"name", safeName(asset.name)}, {"media", asset.media},
            {"created", asset.created.toUTC().toString(Qt::ISODateWithMs)}, {"deleted", false}};
        record["resources"] = resources;
        if (!asset.cloudIdentifier.isEmpty() && record.value("cloud").toString().isEmpty()) record["cloud"] = asset.cloudIdentifier;
        const auto preview = previewPath(id), temporary = m_private + "/incoming/preview-" + id + ".jpg";
        if (preview.isEmpty() || !confined(m_root, temporary)) return fail("The photo preview location was redirected.");
        if (!prepareParent(m_root, temporary)) return fail("Could not prepare the photo preview.");
        iiFileProvider::FileDevice::remove(temporary);
        const auto galleryKey = hash(asset.identifier.toUtf8());
        const auto cached = m_gallery.value(galleryKey);
        const auto cachedPreview = galleryPreview(galleryKey);
        const bool ready = cached.value("previewStamp") == asset.stamp && ordinary(cachedPreview);
        if (!(ready ? iiFileProvider::FileDevice::copy(cachedPreview, temporary) : m_library->preview(asset.identifier, temporary, &m_error))) {
            iiFileProvider::FileDevice::remove(temporary); return false;
        }
        const auto previewHash = hashResource(temporary, &m_error);
        if (previewHash.isEmpty()) { iiFileProvider::FileDevice::remove(temporary); return false; }
        record["previewHash"] = previewHash;
        if (hashResource(preview) != previewHash) {
            iiFileProvider::SaveFile output(preview); iiFileProvider::FileDevice input(temporary);
            if (!input.open(QIODevice::ReadOnly) || !prepareParent(m_root, preview)
                || !output.open(QIODevice::WriteOnly)) { iiFileProvider::FileDevice::remove(temporary); return fail("Could not save the photo preview."); }
            const auto bytes = input.readAll();
            if (bytes.size() > 16 * 1024 * 1024 || output.write(bytes) != bytes.size() || !output.commit()) { iiFileProvider::FileDevice::remove(temporary); return fail("Could not save the photo preview."); }
        }
        iiFileProvider::FileDevice::remove(temporary);
        if (!remember(asset, id, record) || !writeRecord(record)) return false;
        if (galleryProgress) galleryProgress({galleryRow(galleryKey)}, false);
        if (progress) progress(record);
        nativeIds[asset.identifier] = id; if (!asset.cloudIdentifier.isEmpty()) cloudIds[asset.cloudIdentifier] = id;
        return true;
        };
        if (!update()) {
            if (firstError.isEmpty()) firstError = m_error.isEmpty() ? QString("A photo original is not available yet.") : m_error;
            m_error.clear(); if (!intact()) return false;
        }
    }
    // Only two consecutive complete snapshots can establish a native deletion.
    // Losing permission, limited selection and failed scans break that chain.
    if (snapshot.complete) {
        const auto identifiers = m_references.keys();
        for (const auto &id : identifiers) {
            if (m_cancelled) return false;
            auto local = m_references.value(id);
            if (seen.contains(local.value("identifier").toString()) || local.value("unavailable").toBool()) continue;
            auto record = readRecord(id);
            if (m_local.value("complete").toBool() && !record.isEmpty() && !record.value("deleted").toBool()) { record["deleted"] = true; if (!writeRecord(record)) return false; }
            local["unavailable"] = true; if (!saveReference(id, local)) return false;
        }
    }
    m_local["complete"] = snapshot.complete;
    m_local["libraryRevision"] = snapshot.revision;
    if (!save() || !importLooseFiles() || !saveGalleryCatalog(gallery())) return false;
    // A verified original can be a viewing cache downloaded from the host.
    // Native originals are read sources. Container refresh, explicit library
    // import and transfer completion never register them in the OS library.
    return firstError.isEmpty() || fail(firstError);
}
bool PhotoStore::prepare(const QString &id) {
    m_error.clear(); if (!intact()) return false;
    const auto record = readRecord(id); if (record.isEmpty() || record.value("deleted").toBool()) return fail("The photo is unavailable.");
    const auto resources = record.value("resources").toArray(); const auto local = m_references.value(id);
    const auto tokens = local.value("tokens").toArray();
    for (int i = 0; i < resources.size(); ++i) {
        const auto resource = resources[i].toObject(); if (cacheValid(resource)) continue;
        if (local.value("identifier").toString().isEmpty() || i >= tokens.size() || local.value("unavailable").toBool()) return fail("Waiting for a device with this photo original.");
        const auto path = cachePath(resource.value("hash").toString()), temporary = m_private + "/incoming/read-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (path.isEmpty() || !prepareParent(m_root, temporary)) return fail("Could not prepare the photo original.");
        if (!m_library->exportResource(local.value("identifier").toString(), tokens[i].toString(), temporary, &m_error)) { iiFileProvider::FileDevice::remove(temporary); return false; }
        if (hashResource(temporary) != resource.value("hash").toString()) { iiFileProvider::FileDevice::remove(temporary); return fail("The original changed. Refresh Photos before transferring it."); }
        if (iiFileProvider::FileDevice::exists(path)) iiFileProvider::FileDevice::remove(path);
        if (!intact() || !prepareParent(m_root, path) || !iiFileProvider::FileDevice::rename(temporary, path)) { iiFileProvider::FileDevice::remove(temporary); return fail("Could not stage the photo original."); }
    }
    return true;
}
bool PhotoStore::consumeImpl(const QString &id) {
    const auto record = readRecord(id); if (record.isEmpty() || record.value("deleted").toBool()) return fail("The photo is unavailable.");
    for (const auto &resource : record.value("resources").toArray()) {
        if (!cacheValid(resource.toObject())) return fail("Waiting for the complete photo original.");
    }
    return intact();
}
bool PhotoStore::consume(const QString &id) {
    m_error.clear(); if (!intact()) return false;
    if (!prepareParent(m_root, m_private + "/operation.lock")) return fail("Could not prepare the photo operation.");
    iiFileProvider::LockFile lock(m_private + "/operation.lock"); lock.setStaleLockTime(0);
    return (lock.tryLock() || fail("Another Society photo operation is in progress.")) && consumeImpl(id);
}
bool PhotoStore::importLooseFiles() {
    QStringList files; QDirIterator it(m_photos, iiFileProvider::Directory::Files | iiFileProvider::Directory::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) { if (m_cancelled) return false; const auto path = it.next(); if (!path.endsWith(".societyphoto") && !iiFileProvider::Directory(m_photos).relativeFilePath(path).startsWith('.')) files.append(path); }
    for (const auto &path : files) {
        if (m_cancelled) return false;
        if (!intact() || !confined(m_photos, path) || !ordinary(path)) continue;
        const auto mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent).name();
        const auto media = mime.startsWith("image/") ? QString("photo") : mime.startsWith("video/") ? QString("video") : QString();
        if (media.isEmpty()) continue;
        const auto digest = hashResource(path, &m_error); if (digest.isEmpty()) return false;
        const QJsonArray resources{QJsonObject{{"name", safeName(QFileInfo(path).fileName())}, {"role", media}, {"hash", digest}, {"size", QString::number(QFileInfo(path).size())}}};
        const auto id = contentId(resources), cache = cachePath(digest);
        if (cache.isEmpty() || (!iiFileProvider::FileDevice::exists(cache) && (!prepareParent(m_root, cache) || !iiFileProvider::FileDevice::copy(path, cache)))) return fail("Could not prepare the photo import.");
        auto record = readRecord(id);
        if (record.value("deleted").toBool()) continue;
        if (record.isEmpty()) record = {{"schema", 1}, {"id", id}, {"name", safeName(QFileInfo(path).fileName())}, {"media", media},
            {"resources", resources}, {"created", QFileInfo(path).birthTime().toUTC().toString(Qt::ISODateWithMs)}, {"deleted", false}};
        if (media == "photo" && !iiFileProvider::FileDevice::exists(previewPath(id))) {
            QImageReader reader(path); reader.setAutoTransform(true);
            const auto size = reader.size(); if (size.isValid()) reader.setScaledSize(size.scaled(1024, 1024, Qt::KeepAspectRatio));
            const auto image = reader.read(); const auto preview = previewPath(id);
            iiFileProvider::SaveFile output(preview);
            if (!image.isNull() && !preview.isEmpty() && prepareParent(m_root, preview) && output.open(QIODevice::WriteOnly)
                && image.save(&output, "JPEG", 85) && output.commit()) record["previewHash"] = hashResource(preview);
        }
        if (!writeRecord(record, m_paths.value(id, QFileInfo(path).dir().filePath(id + ".societyphoto"))) || !consumeImpl(id)) return false;
        // A user's Photos file remains in place. The portable alias and verified
        // cache are container data; neither is an OS photo-library registration.
    }
    return true;
}
bool PhotoStore::trash(const QString &id) {
    m_error.clear(); if (!intact()) return false;
    if (!prepareParent(m_root, m_private + "/operation.lock")) return fail("Could not prepare the photo operation.");
    iiFileProvider::LockFile lock(m_private + "/operation.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock()) return fail("Another Society photo operation is in progress.");
    auto record = readRecord(id); if (record.isEmpty()) return fail("The photo is unavailable.");
    record["deleted"] = true; return writeRecord(record);
}
bool PhotoStore::addFiles(const QStringList &paths) {
    m_error.clear(); if (!intact()) return false;
    for (const auto &path : paths) {
        if (!ordinary(path)) return fail("Choose an ordinary photo or video file.");
        const auto mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent).name();
        if (!mime.startsWith("image/") && !mime.startsWith("video/")) return fail("Choose a photo or video file.");
        auto destination = m_photos + '/' + safeName(QFileInfo(path).fileName());
        if (iiFileProvider::FileDevice::exists(destination)) destination = m_photos + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + '-' + safeName(QFileInfo(path).fileName());
        if (!prepareParent(m_root, destination) || !iiFileProvider::FileDevice::copy(path, destination)) return fail("Could not add the photo to Society.");
    }
    return refresh();
}
QString PhotoStore::originalForViewing(const QString &id) {
    if (!prepare(id)) return {};
    const auto resource = readRecord(id).value("resources").toArray().first().toObject();
    const auto directory = m_private + "/viewing";
    if (!confined(m_root, directory) || !iiFileProvider::Directory().mkpath(directory)) return {};
    const auto path = directory + '/' + id + '-' + safeName(resource.value("name").toString());
    if (!confined(m_root, path)) return {};
    if (iiFileProvider::FileDevice::exists(path)) iiFileProvider::FileDevice::remove(path);
    return iiFileProvider::FileDevice::copy(resourcePath(id, 0), path) ? path : QString();
}
QStringList PhotoStore::originalsForSharing(const QString &id) {
    if (!prepare(id)) return {};
    const auto resources = readRecord(id).value("resources").toArray();
    const auto directory = m_private + "/viewing/" + id;
    if (!confined(m_root, directory) || !iiFileProvider::Directory().mkpath(directory)) { fail("Could not prepare the original for sharing."); return {}; }
    QStringList paths;
    for (int i = 0; i < resources.size(); ++i) {
        const auto path = directory + '/' + QString::number(i) + '-' + safeName(resources[i].toObject().value("name").toString());
        if (!confined(m_root, path)) { fail("The original export path is unavailable."); return {}; }
        if (iiFileProvider::FileDevice::exists(path)) iiFileProvider::FileDevice::remove(path);
        if (!iiFileProvider::FileDevice::copy(resourcePath(id, i), path)) { fail("Could not export the photo original."); return {}; }
        paths.append(path);
    }
    return paths;
}
bool PhotoStore::duplicate(const QString &id) {
    if (!prepare(id)) return false;
    if (!prepareParent(m_root, m_private + "/operation.lock")) return fail("Could not prepare the photo operation.");
    iiFileProvider::LockFile lock(m_private + "/operation.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock()) return fail("Another Society photo operation is in progress.");
    auto record = readRecord(id); if (record.isEmpty() || record.value("deleted").toBool()) return fail("The photo is unavailable.");
    const auto newId = QString::fromLatin1(QCryptographicHash::hash(QUuid::createUuid().toByteArray(), QCryptographicHash::Sha256).toHex());
    record["id"] = newId;
    // A separate alias identity represents the user's copy while both aliases
    // can share the same verified container resources.
    return writeRecord(record) && consumeImpl(newId);
}
QSet<QString> PhotoStore::containerOwnedOriginals() const {
    const auto access = m_library->access();
    QSet<QString> result;
    for (auto item = m_paths.begin(); item != m_paths.end(); ++item) {
        const auto record = readRecord(item.key());
        if (record.value("deleted").toBool()) continue;
        const auto resources = record.value("resources").toArray();
        const auto local = m_references.value(item.key());
        if ((access != Access::Full && access != Access::Limited)
            || local.value("identifier").toString().isEmpty() || local.value("unavailable").toBool()
            || local.value("tokens").toArray().size() != resources.size()
            || local.value("content") != contentId(resources))
            for (const auto &resource : resources) result.insert(resource.toObject().value("hash").toString());
    }
    return result;
}
void PhotoStore::releaseExports() {
    if (!intact()) return;
    const auto owned = containerOwnedOriginals();
    for (auto i = m_references.begin(); i != m_references.end(); ++i) {
        if (m_cancelled) return;
        const auto local = i.value();
        if (local.value("identifier").toString().isEmpty() || local.value("unavailable").toBool() || local.value("tokens").toArray().isEmpty()) continue;
        for (const auto &resource : readRecord(i.key()).value("resources").toArray()) {
            const auto digest = resource.toObject().value("hash").toString(), path = cachePath(digest);
            if (!path.isEmpty() && !owned.contains(digest)) iiFileProvider::FileDevice::remove(path);
        }
    }
}
void PhotoStore::releasePhoto(const QString &id) {
    const auto local = m_references.value(id);
    if (local.value("identifier").toString().isEmpty() || local.value("unavailable").toBool()
        || local.value("tokens").toArray().isEmpty()) return;
    const auto owned = containerOwnedOriginals();
    for (const auto &value : readRecord(id).value("resources").toArray()) {
        const auto digest = value.toObject().value("hash").toString();
        if (m_leases.value(digest).isEmpty() && !owned.contains(digest)) { const auto path = cachePath(digest); if (!path.isEmpty()) iiFileProvider::FileDevice::remove(path); }
    }
}
void PhotoStore::pruneExports() {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto owned = containerOwnedOriginals();
    // Viewing copies and abandoned transfers are temporary too. Never follow
    // redirected files or purge the NAS's committed originals here.
    for (const auto &directory : {m_private + "/viewing", m_private + "/incoming"}) {
        if (!confined(m_root, directory)) continue;
        QDirIterator files(directory, iiFileProvider::Directory::Files | iiFileProvider::Directory::NoDotAndDotDot);
        while (files.hasNext()) {
            if (m_cancelled) return;
            const auto path = files.next(); const QFileInfo info(path);
            const qint64 age = hex(info.fileName()) ? 7LL * 24 * 60 * 60 * 1000 : 24LL * 60 * 60 * 1000;
            if (confined(m_root, path) && ordinary(path) && now - info.lastModified().toMSecsSinceEpoch() > age) iiFileProvider::FileDevice::remove(path);
        }
    }
    for (auto &leases : m_leases) for (auto i = leases.begin(); i != leases.end();)
        if (now - i.value() > 60 * 60 * 1000) i = leases.erase(i); else ++i;
    for (auto i = m_references.begin(); i != m_references.end(); ++i) {
        if (m_cancelled) return;
        const auto local = i.value();
        if (local.value("identifier").toString().isEmpty() || local.value("unavailable").toBool() || local.value("tokens").toArray().isEmpty()) continue;
        for (const auto &value : readRecord(i.key()).value("resources").toArray()) {
            const auto digest = value.toObject().value("hash").toString(), path = cachePath(digest);
            if (!path.isEmpty() && !owned.contains(digest) && m_leases.value(digest).isEmpty() && QFileInfo(path).exists()
                && now - QFileInfo(path).lastModified().toMSecsSinceEpoch() > 10 * 60 * 1000) iiFileProvider::FileDevice::remove(path);
        }
    }
}
QJsonObject PhotoStore::command(const QJsonObject &request) {
    m_error.clear(); const auto failed = [this](const QString &message) { fail(message); return QJsonObject{{"ok", false}, {"error", message}}; };
    if (!intact()) return failed(m_error);
    const auto id = request.value("id").toString(), action = request.value("action").toString();
    if (readRecord(id).isEmpty() && !m_paths.contains(id)) catalog();
    const auto record = readRecord(id); if (record.isEmpty() || record.value("deleted").toBool()) return failed("photo_unavailable");
    if (action == "available") return {{"ok", true}, {"available", hasOriginal(id)}};
    const auto lease = request.value("lease").toString();
    if (!lease.isEmpty() && QUuid(lease).isNull()) return failed("invalid_photo_lease");
    if (action == "prepare") {
        if (!prepare(id)) return failed(m_error);
        if (!lease.isEmpty()) for (const auto &value : record.value("resources").toArray())
            m_leases[value.toObject().value("hash").toString()][lease] = QDateTime::currentMSecsSinceEpoch();
        return {{"ok", true}};
    }
    if (action == "release") {
        for (const auto &value : record.value("resources").toArray()) m_leases[value.toObject().value("hash").toString()].remove(lease);
        releasePhoto(id); return {{"ok", true}};
    }
    if (action == "consume") return consume(id) ? QJsonObject{{"ok", true}} : failed(m_error);
    const auto resources = record.value("resources").toArray();
    if (!request.value("resource").isDouble()) return failed("invalid_resource");
    const auto index = request.value("resource").toInt(-1);
    if (index < 0 || index >= resources.size()) return failed("invalid_resource");
    const auto resource = resources[index].toObject(); const auto path = resourcePath(id, index);
    if (path.isEmpty()) return failed("resource_redirected");
    const auto incoming = m_private + "/incoming/" + resource.value("hash").toString();
    if (!confined(m_root, incoming)) return failed("resource_redirected");
    const qint64 size = resource.value("size").toString().toLongLong();
    if (action == "begin") {
        if (cacheValid(resource)) return {{"ok", true}, {"offset", QString::number(size)}};
        if (QStorageInfo(m_root).bytesAvailable() < size + 16 * 1024 * 1024) return failed("insufficient_storage");
        if (!prepareParent(m_root, incoming)) return failed("could_not_create_transfer");
        iiFileProvider::FileDevice file(incoming); if (!file.open(QIODevice::ReadWrite)) return failed("could_not_create_transfer");
        if (file.size() > size && !file.resize(0)) return failed("invalid_partial_transfer");
        return {{"ok", true}, {"offset", QString::number(file.size())}};
    }
    if (action == "commit") {
        if (cacheValid(resource)) return {{"ok", true}};
        if (QFileInfo(incoming).size() != size || hashResource(incoming) != resource.value("hash").toString()) { iiFileProvider::FileDevice::remove(incoming); return failed("resource_hash_mismatch"); }
        if (iiFileProvider::FileDevice::exists(path) && !iiFileProvider::FileDevice::remove(path)) return failed("resource_replace_failed");
        if (!prepareParent(m_root, path) || !iiFileProvider::FileDevice::rename(incoming, path)) return failed("resource_commit_failed");
        return {{"ok", true}};
    }
    bool number; const auto offset = request.value("offset").toString().toLongLong(&number);
    if (!number || offset < 0 || offset > size) return failed("invalid_offset");
    if (action == "read") {
        if (!lease.isEmpty()) {
            auto &leases = m_leases[resource.value("hash").toString()];
            if (!leases.contains(lease)) return failed("photo_transfer_expired");
            leases[lease] = QDateTime::currentMSecsSinceEpoch();
        }
        iiFileProvider::FileDevice file(path); if (!ordinary(path) || file.size() != size || !file.open(QIODevice::ReadOnly) || !file.seek(offset)) return failed("original_unavailable");
        const auto data = file.read(qMin(ChunkBytes, size - offset));
        if (data.isEmpty() && offset != size) return failed("original_read_failed");
        return {{"ok", true}, {"offset", QString::number(offset)}, {"data", QString::fromLatin1(data.toBase64())}};
    }
    if (action == "chunk") {
        const auto data = QByteArray::fromBase64Encoding(request.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!data || data.decoded.isEmpty() || data.decoded.size() > ChunkBytes || data.decoded.size() > size - offset) return failed("invalid_chunk");
        if (!prepareParent(m_root, incoming)) return failed("chunk_out_of_order");
        iiFileProvider::FileDevice file(incoming); if (!file.open(QIODevice::ReadWrite) || offset > file.size() || !file.seek(offset)) return failed("chunk_out_of_order");
        if (offset < file.size()) {
            if (offset + data.decoded.size() > file.size() || file.read(data.decoded.size()) != data.decoded) return failed("chunk_replay_mismatch");
        } else if (file.write(data.decoded) != data.decoded.size() || !file.flush()) return failed("chunk_write_failed");
        return {{"ok", true}, {"offset", QString::number(offset + data.decoded.size())}};
    }
    return failed("unsupported_photo_action");
}
}
