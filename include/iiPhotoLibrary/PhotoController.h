#pragma once
#include "PhotoStore.h"
#include <QElapsedTimer>
#include <QAbstractListModel>
#include <QFuture>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

namespace iiPhotoLibrary {
// Preserve delegates and decoded images when only preview metadata changes.
class PhotoGalleryModel final : public QAbstractListModel {
public:
    explicit PhotoGalleryModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}}; }
    void replace(const QVariantList &rows);
private:
    QVariantList m_rows;
};
class PhotoController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QAbstractItemModel *galleryModel READ galleryModel CONSTANT)
    Q_PROPERTY(QString libraryName READ libraryName CONSTANT)
    Q_PROPERTY(QString access READ access NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool canShare READ canShare CONSTANT)
public:
    using Sender = std::function<QString(const QString &, const QJsonObject &)>;
    using Completion = std::function<void(QJsonObject)>;
    explicit PhotoController(Sender sender, QObject *parent = nullptr,
                             std::shared_ptr<PhotoLibrary> library = {});
    ~PhotoController() override;
    void configure(const QString &container, bool active, const QStringList &authorized = {}, const QStringList &hosts = {});
    // Call after disabling the controller, before returning a background grant.
    void waitForDone() { m_worker.waitForDone(); }
    QJsonObject handle(const QString &peer, const QJsonObject &packet);
    void receive(const QString &request, const QJsonObject &reply);
    QVariantList entries() const { return m_entries; }
    QAbstractItemModel *galleryModel() { return &m_galleryModel; }
    QString libraryName() const { return m_library->name(); }
    QString access() const { return accessName(m_library->access()); }
    QString status() const { return m_status; }
    bool busy() const { return m_authorizing || m_refreshing || m_cycle; }
    bool active() const { return bool(m_store); }
    bool canShare() const;
    void setApplicationActive(bool active);
    Q_INVOKABLE void connectLibrary();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void prioritizeVisible(const QStringList &keys);
    Q_INVOKABLE void addFiles(const QList<QUrl> &files);
    Q_INVOKABLE void openPhoto(const QString &id);
    Q_INVOKABLE void downloadPhoto(const QString &id);
    Q_INVOKABLE void trashPhoto(const QString &id);
    Q_INVOKABLE void copyPhoto(const QString &id);
    Q_INVOKABLE void duplicatePhoto(const QString &id);
    Q_INVOKABLE void sharePhoto(const QString &id);
signals:
    void changed();
    void entriesAboutToChange();
    void entriesChanged();
    void contentsChanged();
    void progress(const QString &path, qint64 done, qint64 total);
private:
    void usePhoto(const QString &id, const QString &action);
    struct Job { QFuture<QJsonObject> future; QByteArray payload; qint64 created = 0; };
    void work(std::function<QJsonObject(PhotoStore &)> operation, Completion done);
    void local(const QJsonObject &command, Completion done);
    void remote(QJsonObject command, Completion done);
    void sendPacket();
    void updateEntries(const QJsonArray &records);
    void updateGallery(const QJsonArray &rows, bool reset);
    void startCycle();
    void nextPhoto();
    void transfer(bool uploading);
    void nextResource();
    void nextChunk();
    void finishCycle(const QString &error = {}, bool refreshAfterTransfer = true);
    void destination(const QJsonObject &command, Completion done);
    void source(const QJsonObject &command, Completion done);
    QJsonObject resourceCommand(const QString &action) const;
    bool accepted(const QJsonObject &reply);
    PhotoGalleryModel m_galleryModel;
    Sender m_send;
    std::shared_ptr<PhotoLibrary> m_library;
    std::shared_ptr<PhotoStore> m_store;
    QThreadPool m_worker;
    QTimer m_timer, m_timeout, m_galleryTimer;
    QString m_container, m_identifier, m_status, m_host, m_request, m_lease;
    QStringList m_authorized, m_hosts;
    QString m_requestedOriginal, m_openAfterDownload;
    QString m_afterDownloadAction;
    QVariantList m_entries;
    QJsonArray m_records, m_cycleRecords;
    QJsonObject m_previewStamps;
    QHash<QString, QJsonObject> m_galleryRows;
    QHash<QString, Job> m_jobs;
    QJsonObject m_packet, m_photo;
    Completion m_reply;
    QElapsedTimer m_requestAge;
    quint64 m_generation = 0;
    quint64 m_cycleGeneration = 0;
    bool m_refreshing = false, m_cycle = false, m_uploading = false;
    bool m_customLibrary = false, m_transferred = false;
    bool m_applicationActive = false, m_authorizing = false, m_automaticAccessRequested = false;
    int m_photoIndex = 0, m_resourceIndex = 0;
    int m_waitingOriginals = 0;
    qint64 m_offset = 0;
};
}
