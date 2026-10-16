#include <iiPhotoLibrary/PhotoController.h>
#include <FileActions.h>
#include <QDesktopServices>
#include <QDebug>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QPointer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace iiPhotoLibrary {
QVariant PhotoGalleryModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::UserRole || !index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    return m_rows[index.row()];
}
void PhotoGalleryModel::replace(const QVariantList &rows) {
    bool sameOrder = rows.size() == m_rows.size();
    for (int i = 0; sameOrder && i < rows.size(); ++i) {
        const auto before = m_rows[i].toMap(), after = rows[i].toMap();
        sameOrder = before.value("id") == after.value("id")
            || (!before.value("galleryKey").toString().isEmpty() && before.value("galleryKey") == after.value("galleryKey"));
    }
    if (!sameOrder) { beginResetModel(); m_rows = rows; endResetModel(); return; }
    int first = -1;
    for (int i = 0; i < rows.size(); ++i) {
        if (m_rows[i] != rows[i]) {
            m_rows[i] = rows[i]; if (first < 0) first = i;
        } else if (first >= 0) {
            emit dataChanged(index(first), index(i - 1), {Qt::UserRole}); first = -1;
        }
    }
    if (first >= 0) emit dataChanged(index(first), index(rows.size() - 1), {Qt::UserRole});
}
namespace {
QJsonObject photoSnapshot(PhotoStore &store, bool ok) {
    if (store.cancelled()) return {{"ok", false}};
    const auto records = store.catalog(); QJsonObject previews;
    for (const auto &value : records) {
        if (store.cancelled()) return {{"ok", false}};
        const auto id = value.toObject().value("id").toString(); const QFileInfo preview(store.previewPath(id));
        previews[id] = preview.exists() ? QString::number(preview.lastModified().toMSecsSinceEpoch()) + ':' + QString::number(preview.size()) : QString();
    }
    return {{"ok", ok}, {"error", store.errorString()}, {"records", records}, {"previews", previews}, {"gallery", store.gallery()}};
}
}
PhotoController::PhotoController(Sender sender, QObject *parent, std::shared_ptr<PhotoLibrary> library)
    : QObject(parent), m_galleryModel(this), m_send(std::move(sender)), m_library(library ? library : nativePhotoLibrary()), m_customLibrary(bool(library)) {
    m_worker.setMaxThreadCount(1);
    m_galleryTimer.setSingleShot(true); m_galleryTimer.setInterval(32);
    connect(&m_galleryTimer, &QTimer::timeout, this, [this] { updateEntries(m_records); });
    m_timer.setInterval(30000); connect(&m_timer, &QTimer::timeout, this, &PhotoController::refresh);
    m_timeout.setSingleShot(true); m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finishCycle(tr("Photo transfer is waiting for the other device. It will retry.")); });
    m_status = tr("Connect your photo library to synchronize photos and videos.");
    connect(this, &PhotoController::changed, this, [this] {
        if (!busy() && !m_openAfterDownload.isEmpty() && m_requestedOriginal.isEmpty()) {
            const auto id = std::exchange(m_openAfterDownload, {});
            const auto action = std::exchange(m_afterDownloadAction, {});
            QTimer::singleShot(0, this, [this, id, action] { usePhoto(id, action); });
        }
    });
}
PhotoController::~PhotoController() { if (m_store) m_store->cancel(); m_worker.waitForDone(); }
void PhotoController::configure(const QString &container, bool enabled, const QStringList &authorized, const QStringList &hosts) {
    if (qEnvironmentVariableIntValue("IIPHOTOLIBRARY_DISABLE_PHOTOS") == 1) enabled = false;
    m_authorized = authorized; m_hosts = hosts;
    auto drive = enabled ? iiSocietyContainer::SocietyDrive::open(container) : std::nullopt;
    const auto identifier = drive && drive->isReady() ? drive->identifier() : QString();
    if (m_container == container && m_identifier == identifier) {
        if (m_cycle && !m_hosts.contains(m_host)) finishCycle({}, false);
        return;
    }
    ++m_generation; finishCycle({}, false); m_timer.stop(); m_jobs.clear();
    m_authorizing = false; m_importLibraryRequested = false;
    if (m_store) { m_store->cancel(); if (!m_customLibrary) m_library = nativePhotoLibrary(); }
    m_store.reset();
    m_container = container; m_identifier = identifier; m_refreshing = false;
    m_requestedOriginal.clear(); m_openAfterDownload.clear(); m_afterDownloadAction.clear();
    m_galleryTimer.stop(); m_galleryRows.clear();
    updateEntries({});
    if (identifier.isEmpty()) { emit changed(); return; }
    m_store = std::make_shared<PhotoStore>(container, m_library);
    m_refreshing = true;
    const QPointer<PhotoController> guard(this); const auto generation = m_generation;
    work([guard, generation](PhotoStore &store) {
        const bool ok = store.open([guard, generation](const QJsonArray &rows, bool reset) {
            if (!guard || rows.isEmpty()) return;
            QMetaObject::invokeMethod(guard, [guard, generation, rows, reset] {
                if (guard && guard->m_generation == generation) guard->updateGallery(rows, reset);
            }, Qt::QueuedConnection);
        });
        return photoSnapshot(store, ok);
    }, [this](const auto &result) {
        m_refreshing = false;
        if (!result.value("ok").toBool()) { m_status = result.value("error").toString(); emit changed(); return; }
        updateGallery(result.value("gallery").toArray(), true);
        m_previewStamps = result.value("previews").toObject();
        updateEntries(result.value("records").toArray());
        m_timer.start(); refresh();
    });
    emit changed();
}
void PhotoController::work(std::function<QJsonObject(PhotoStore &)> operation, Completion done) {
    if (!m_store) { done({{"ok", false}, {"error", "photo_session_unavailable"}}); return; }
    const auto store = m_store; const auto generation = m_generation;
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, generation, done = std::move(done)] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation == m_generation) done(result);
    });
    watcher->setFuture(QtConcurrent::run(&m_worker, [store, operation = std::move(operation)] { return operation(*store); }));
}
void PhotoController::setApplicationActive(bool active) {
    if (m_applicationActive == active) return;
    m_applicationActive = active;
    if (active) refresh();
}
void PhotoController::connectLibrary() {
    if (!m_store || busy()) return;
    if (m_library->access() == Access::Full || m_library->access() == Access::Limited) {
        m_importLibraryRequested = true; refresh(); return;
    }
    m_authorizing = true; emit changed();
    qInfo() << "iiPhotoLibrary: requesting library access; current access:" << access();
    const auto library = m_library;
    const auto generation = m_generation;
    QPointer<PhotoController> self(this);
    library->authorize([self, library, generation] {
        if (!self || self->m_generation != generation) return;
        self->m_authorizing = false;
        self->m_importLibraryRequested = true;
        emit self->changed();
        self->refresh();
    });
}
void PhotoController::prioritizeVisible(const QStringList &keys) {
    if (m_store) m_store->prioritize(keys);
}
void PhotoController::updateGallery(const QJsonArray &rows, bool reset) {
    if (reset) m_galleryRows.clear();
    for (const auto &value : rows) {
        const auto row = value.toObject(); const auto key = row.value("galleryKey").toString();
        if (!key.isEmpty()) m_galleryRows[key] = row;
    }
    if (!m_galleryTimer.isActive()) m_galleryTimer.start();
}
void PhotoController::updateEntries(const QJsonArray &records) {
    m_records = records; QVariantList entries;
    QHash<QString, QVariantMap> rows;
    for (const auto &row : m_galleryRows) rows[row.value("id").toString()] = row.toVariantMap();
    for (const auto &value : records) {
        const auto record = value.toObject(); const auto id = record.value("id").toString();
        if (record.value("deleted").toBool()) { rows.remove(id); continue; }
        auto row = record.toVariantMap();
        row["galleryKey"] = rows.value(id).value("galleryKey", id);
        row["indexing"] = false;
        const auto stamp = m_previewStamps.value(id).toString();
        if (!stamp.isEmpty()) {
            row["preview"] = QUrl::fromLocalFile(m_container + "/Photos/.previews/" + id + ".jpg");
            row["previewStamp"] = stamp;
        } else {
            row["preview"] = rows.value(id).value("preview", QString());
            row["previewStamp"] = rows.value(id).value("previewStamp", QString());
        }
        rows[id] = row;
    }
    for (const auto &row : rows) entries.append(row);
    std::sort(entries.begin(), entries.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        return left.value("created") == right.value("created") ? left.value("galleryKey").toString() < right.value("galleryKey").toString()
            : left.value("created").toString() < right.value("created").toString();
    });
    if (m_entries != entries) { emit entriesAboutToChange(); m_entries = entries; m_galleryModel.replace(entries); emit entriesChanged(); }
}
void PhotoController::refresh() {
    if (!m_store || busy()) return;
    const bool includeLibrary = std::exchange(m_importLibraryRequested, false);
    m_refreshing = true; m_status = tr("Updating photos and videos…"); emit changed();
    const QPointer<PhotoController> guard(this); const auto generation = m_generation;
    work([guard, generation, includeLibrary](PhotoStore &store) {
        QElapsedTimer published;
        QJsonArray pendingRecords; QJsonObject pendingPreviews;
        const auto refresh = includeLibrary ? &PhotoStore::importLibrary : &PhotoStore::refresh;
        const bool ok = (store.*refresh)([&](const QJsonObject &record) {
            // Read only the newly committed preview. Re-reading the entire
            // catalog for every partial update stalls large libraries for
            // seconds under operation.lock and starves real progress reports.
            const auto id = record.value("id").toString();
            const QFileInfo preview(store.previewPath(id));
            pendingRecords.append(record);
            pendingPreviews[id] = preview.exists() ? QString::number(preview.lastModified().toMSecsSinceEpoch())
                + ':' + QString::number(preview.size()) : QString();
            const bool publish = !published.isValid() || published.elapsed() >= 500;
            const auto records = publish ? std::exchange(pendingRecords, {}) : QJsonArray{};
            const auto previews = publish ? std::exchange(pendingPreviews, {}) : QJsonObject{};
            if (publish) published.start();
            if (!guard) return;
            QMetaObject::invokeMethod(guard, [guard, generation, record, records, previews] {
                if (!guard || guard->m_generation != generation) return;
                const auto resources = record.value("resources").toArray();
                for (int i = 0; i < resources.size(); ++i) {
                    const auto bytes = resources[i].toObject().value("size").toString().toLongLong();
                    emit guard->progress("Photos/index/" + record.value("id").toString() + '/' + QString::number(i), bytes, bytes);
                }
                if (!records.isEmpty()) {
                    auto merged = guard->m_records;
                    for (const auto &value : records) {
                        const auto id = value.toObject().value("id").toString();
                        qsizetype index = 0;
                        while (index < merged.size() && merged[index].toObject().value("id") != id) ++index;
                        if (index == merged.size()) merged.append(value); else merged[index] = value;
                        guard->m_previewStamps[id] = previews.value(id);
                    }
                    guard->updateEntries(merged);
                    guard->m_status = tr("Updating photos and videos… %1 available").arg(guard->m_entries.size());
                    emit guard->changed(); emit guard->contentsChanged();
                }
            }, Qt::QueuedConnection);
        }, [guard, generation](const QJsonArray &rows, bool reset) {
            if (!guard) return;
            QMetaObject::invokeMethod(guard, [guard, generation, rows, reset] {
                if (guard && guard->m_generation == generation) guard->updateGallery(rows, reset);
            }, Qt::QueuedConnection);
        });
        return photoSnapshot(store, ok);
    }, [this](const auto &result) {
        m_refreshing = false;
        const auto records = result.value("records").toArray();
        qInfo() << "iiPhotoLibrary: scan finished; access:" << access() << "records:" << records.size() << "ok:" << result.value("ok").toBool(); const bool changed = records != m_records;
        updateGallery(result.value("gallery").toArray(), true);
        m_previewStamps = result.value("previews").toObject();
        updateEntries(records);
        if (!result.value("ok").toBool()) m_status = result.value("error").toString();
        else m_status = tr("%1 photos and videos in this container.").arg(m_entries.size());
        if (changed) emit contentsChanged();
        startCycle();
        if (!m_cycle) emit this->changed();
    });
}
void PhotoController::addFiles(const QList<QUrl> &files) {
    if (busy() || !m_store) return;
    QStringList paths; for (const auto &url : files) if (url.isLocalFile()) paths.append(url.toLocalFile());
    m_refreshing = true; emit changed();
    work([paths](PhotoStore &store) { const bool ok = store.addFiles(paths); return QJsonObject{{"ok", ok}, {"error", store.errorString()}}; }, [this](const auto &result) {
        m_refreshing = false;
        if (!result.value("ok").toBool()) { m_status = result.value("error").toString(); emit changed(); }
        else refresh();
    });
}
void PhotoController::openPhoto(const QString &id) {
    usePhoto(id, "open");
}
bool PhotoController::canShare() const { return iiSocietyContainer::FileActions::sharingAvailable(); }
void PhotoController::copyPhoto(const QString &id) { usePhoto(id, "copy"); }
void PhotoController::sharePhoto(const QString &id) { usePhoto(id, "share"); }
void PhotoController::duplicatePhoto(const QString &id) { usePhoto(id, "duplicate"); }
void PhotoController::usePhoto(const QString &id, const QString &action) {
    if (busy() || !m_store) return;
    m_refreshing = true; emit changed();
    work([id, action](PhotoStore &store) {
        if (!store.prepare(id)) return QJsonObject{{"ok", false}, {"needsOriginal", true}, {"error", store.errorString()}};
        if (action == "duplicate") { const bool ok = store.duplicate(id); return QJsonObject{{"ok", ok}, {"error", store.errorString()}}; }
        const auto paths = action == "open" ? QStringList{store.originalForViewing(id)} : store.originalsForSharing(id);
        return QJsonObject{{"ok", !paths.isEmpty() && !paths.first().isEmpty()}, {"paths", QJsonArray::fromStringList(paths)}, {"error", store.errorString()}};
    }, [this, id, action](const auto &result) {
        m_refreshing = false;
        if (result.value("ok").toBool()) {
            QStringList paths; for (const auto &path : result.value("paths").toArray()) paths.append(path.toString());
            if (action == "duplicate") { refresh(); return; }
            if (action == "open") QDesktopServices::openUrl(QUrl::fromLocalFile(paths.first()));
            else if (action == "copy") { iiSocietyContainer::FileActions::copyFiles(paths); m_status = tr("Photo originals copied."); }
            else if (action == "share") {
                QString error; if (!iiSocietyContainer::FileActions::shareFiles(paths, &error)) m_status = error;
            }
        } else if (result.value("needsOriginal").toBool() && !m_hosts.isEmpty()) {
            m_openAfterDownload = id; m_afterDownloadAction = action; downloadPhoto(id);
            m_status = tr("Downloading the selected photo original…");
        } else m_status = result.value("error").toString(tr("Connect to the Society host to download this original."));
        emit changed();
    });
}
void PhotoController::downloadPhoto(const QString &id) {
    if (!m_store) return;
    bool exists = false; for (const auto &value : m_records) if (value.toObject().value("id") == id && !value.toObject().value("deleted").toBool()) exists = true;
    if (!exists) return;
    m_requestedOriginal = id; startCycle();
}
void PhotoController::trashPhoto(const QString &id) {
    if (busy() || !m_store) return;
    m_refreshing = true; emit changed();
    work([id](PhotoStore &store) { const bool ok = store.trash(id); return QJsonObject{{"ok", ok}, {"error", store.errorString()}}; }, [this](const auto &result) {
        m_refreshing = false;
        if (!result.value("ok").toBool()) { m_status = result.value("error").toString(); emit changed(); }
        else refresh();
    });
}
QJsonObject PhotoController::handle(const QString &peer, const QJsonObject &packet) {
    const auto denied = [](const QString &reason) { return QJsonObject{{"ok", false}, {"error", reason}}; };
    if (!m_store || !m_authorized.contains(peer) || packet.value("container") != m_identifier) return denied("photo_session_not_authorized");
    const auto ticket = packet.value("ticket").toString();
    if (QUuid(ticket).isNull() || packet.value("op") != "society.photos") return denied("invalid_photo_request");
    const auto bytes = QJsonDocument(packet).toJson(QJsonDocument::Compact);
    if (bytes.size() > 400000) return denied("photo_request_too_large");
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (auto i = m_jobs.begin(); i != m_jobs.end();) {
        if (i->future.isFinished() && now - i->created > 60000) i = m_jobs.erase(i); else ++i;
    }
    const auto key = peer + ':' + ticket;
    if (m_jobs.contains(key)) {
        const auto &job = m_jobs[key];
        if (job.payload != bytes) return denied("photo_ticket_reused");
        return job.future.isFinished() ? job.future.result() : QJsonObject{{"ok", true}, {"pending", true}};
    }
    if (m_jobs.size() >= 64) {
        auto oldest = m_jobs.end();
        for (auto i = m_jobs.begin(); i != m_jobs.end(); ++i)
            if (i->future.isFinished() && (oldest == m_jobs.end() || i->created < oldest->created)) oldest = i;
        // Commands are resumable and idempotent. Keep active native work,
        // evict only a finished reply so long videos can continue streaming.
        if (oldest == m_jobs.end()) return denied("photo_service_busy");
        m_jobs.erase(oldest);
    }
    const auto store = m_store;
    m_jobs.insert(key, {QtConcurrent::run(&m_worker, [store, packet] { return store->command(packet); }), bytes, now});
    return {{"ok", true}, {"pending", true}};
}
void PhotoController::local(const QJsonObject &command, Completion done) {
    const auto cycle = m_cycleGeneration;
    work([command](PhotoStore &store) { return store.command(command); }, [this, cycle, done = std::move(done)](const QJsonObject &reply) {
        if (cycle == m_cycleGeneration && m_cycle) done(reply);
    });
}
void PhotoController::remote(QJsonObject command, Completion done) {
    command["op"] = "society.photos"; command["container"] = m_identifier;
    command["ticket"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_packet = command; m_reply = std::move(done); m_requestAge.start(); sendPacket();
}
void PhotoController::sendPacket() {
    if (!m_cycle || !m_reply || !m_hosts.contains(m_host)) { finishCycle(); return; }
    if (m_requestAge.elapsed() > 15 * 60 * 1000) { finishCycle(tr("The photo original is still downloading from its cloud library.")); return; }
    m_request = m_send(m_host, m_packet);
    if (m_request.isEmpty()) finishCycle(tr("Waiting for a connected Society host.")); else m_timeout.start();
}
void PhotoController::receive(const QString &id, const QJsonObject &reply) {
    if (id != m_request || !m_reply) return;
    m_timeout.stop(); m_request.clear();
    if (reply.value("pending").toBool() && reply.value("ok").toBool()) {
        const auto generation = m_generation, cycle = m_cycleGeneration;
        QTimer::singleShot(100, this, [this, generation, cycle] { if (generation == m_generation && cycle == m_cycleGeneration) sendPacket(); }); return;
    }
    auto done = std::exchange(m_reply, {}); done(reply);
}
void PhotoController::startCycle() {
    if (!m_store || m_cycle || m_hosts.isEmpty()) return;
    m_cycle = true; m_transferred = false; m_waitingOriginals = 0; m_host = m_hosts.first(); m_cycleRecords = m_records; m_photoIndex = 0; emit changed(); nextPhoto();
}
bool PhotoController::accepted(const QJsonObject &reply) {
    if (reply.value("ok").toBool()) return true;
    finishCycle(reply.value("error").toString(tr("Photo transfer will retry."))); return false;
}
void PhotoController::nextPhoto() {
    if (!m_cycle) return;
    while (m_photoIndex < m_cycleRecords.size() && m_cycleRecords[m_photoIndex].toObject().value("deleted").toBool()) ++m_photoIndex;
    if (m_photoIndex >= m_cycleRecords.size()) { finishCycle(); return; }
    m_photo = m_cycleRecords[m_photoIndex++].toObject(); const auto id = m_photo.value("id");
    local({{"action", "available"}, {"id", id}}, [this, id](const auto &localReply) {
        if (!accepted(localReply)) return;
        const bool have = localReply.value("available").toBool();
        if (have && id.toString() == m_requestedOriginal) m_requestedOriginal.clear();
        remote({{"action", "available"}, {"id", id}}, [this, have](const auto &reply) {
            if (reply.value("error") == "photo_unavailable") { ++m_waitingOriginals; nextPhoto(); return; } // Metadata sync may still be applying.
            if (!accepted(reply)) return;
            const bool there = reply.value("available").toBool();
            if (have && !there) transfer(true);
            else if (!have && there && m_photo.value("id").toString() == m_requestedOriginal) transfer(false);
            else { if (!have && !there) ++m_waitingOriginals; nextPhoto(); }
        });
    });
}
void PhotoController::transfer(bool uploading) {
    m_uploading = uploading; m_resourceIndex = 0; m_transferred = true;
    m_lease = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_status = tr("Syncing %1…").arg(m_photo.value("name").toString()); emit changed();
    source({{"action", "prepare"}, {"id", m_photo.value("id")}, {"lease", m_lease}}, [this](const auto &reply) { if (accepted(reply)) nextResource(); });
}
QJsonObject PhotoController::resourceCommand(const QString &action) const {
    return {{"action", action}, {"id", m_photo.value("id")}, {"lease", m_lease}, {"resource", m_resourceIndex}, {"offset", QString::number(m_offset)}};
}
void PhotoController::source(const QJsonObject &command, Completion done) { if (m_uploading) local(command, std::move(done)); else remote(command, std::move(done)); }
void PhotoController::destination(const QJsonObject &command, Completion done) { if (m_uploading) remote(command, std::move(done)); else local(command, std::move(done)); }
void PhotoController::nextResource() {
    if (!m_cycle) return;
    if (m_resourceIndex >= m_photo.value("resources").toArray().size()) {
        if (!m_uploading) {
            m_requestedOriginal.clear();
            source({{"action", "release"}, {"id", m_photo.value("id")}, {"lease", m_lease}}, [this](const auto &released) { if (accepted(released)) nextPhoto(); });
            return;
        }
        destination({{"action", "consume"}, {"id", m_photo.value("id")}}, [this](const auto &reply) {
            if (!accepted(reply)) return;
            source({{"action", "release"}, {"id", m_photo.value("id")}, {"lease", m_lease}}, [this](const auto &released) { if (accepted(released)) nextPhoto(); });
        }); return;
    }
    destination(resourceCommand("begin"), [this](const auto &reply) {
        if (!accepted(reply)) return;
        bool valid; m_offset = reply.value("offset").toString().toLongLong(&valid);
        const auto size = m_photo.value("resources").toArray()[m_resourceIndex].toObject().value("size").toString().toLongLong();
        if (!valid || m_offset < 0 || m_offset > size) { finishCycle("invalid_photo_offset"); return; }
        nextChunk();
    });
}
void PhotoController::nextChunk() {
    if (!m_cycle) return;
    const auto size = m_photo.value("resources").toArray()[m_resourceIndex].toObject().value("size").toString().toLongLong();
    if (m_offset == size) {
        destination(resourceCommand("commit"), [this](const auto &reply) { if (accepted(reply)) { ++m_resourceIndex; nextResource(); } }); return;
    }
    source(resourceCommand("read"), [this, size](const auto &reply) {
        if (!accepted(reply)) return;
        const auto data = QByteArray::fromBase64Encoding(reply.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!data || data.decoded.isEmpty() || data.decoded.size() > PhotoStore::ChunkBytes || data.decoded.size() > size - m_offset
            || reply.value("offset").toString() != QString::number(m_offset)) { finishCycle("invalid_photo_chunk"); return; }
        const auto next = m_offset + data.decoded.size(); auto command = resourceCommand("chunk"); command["data"] = reply.value("data");
        destination(command, [this, next](const auto &ack) {
            if (!accepted(ack)) return;
            if (ack.value("offset").toString() != QString::number(next)) { finishCycle("invalid_photo_acknowledgement"); return; }
            m_offset = next;
            const auto bytes = m_photo.value("resources").toArray()[m_resourceIndex].toObject().value("size").toString().toLongLong();
            emit progress("Photos/transfer/" + m_photo.value("id").toString() + '/' + QString::number(m_resourceIndex), next, bytes);
            nextChunk();
        });
    });
}
void PhotoController::finishCycle(const QString &error, bool refreshAfterTransfer) {
    ++m_cycleGeneration;
    const bool wasRunning = m_cycle;
    m_cycle = false; m_timeout.stop(); m_reply = {}; m_request.clear(); m_packet = {};
    if (!error.isEmpty()) m_status = error;
    else if (wasRunning) {
        if (m_waitingOriginals) m_status = tr("%1 photo originals are waiting for their source device.").arg(m_waitingOriginals);
        else if (access() == "denied" || access() == "notDetermined") m_status = tr("Connect your photo library to add received photos and videos to this device.");
        else m_status = tr("Photos and videos are synchronized with this Society host.");
    }
    if (wasRunning) {
        if (error.isEmpty() && m_transferred && refreshAfterTransfer) {
            m_transferred = false;
            // Account for the post-import catalog refresh before publishing
            // idle; otherwise the background owner disconnects in this gap.
            refresh();
        } else emit changed();
    }
}
}
