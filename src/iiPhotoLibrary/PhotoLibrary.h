#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <functional>
#include <memory>

namespace iiPhotoLibrary {
enum class Access { Unsupported, NotDetermined, Denied, Limited, Full };
struct Resource {
    QString token, name, role; // Native tokens never leave this device.
};
struct Asset {
    QString identifier, cloudIdentifier, stamp, name, media;
    QDateTime created;
    bool favorite = false;
    QList<Resource> resources;
    bool screenshot = false, imported = false, hidden = false, deleted = false;
};
struct Snapshot {
    QList<Asset> assets;
    // Restricted access or an interrupted query is never evidence of deletion.
    bool complete = false;
    QString revision;
};

// All I/O methods run on the photo worker. Authorization runs on the GUI thread.
// Native originals remain owned by the OS library. PhotoStore reads selected
// originals into its container and never calls import/trash; those low-level
// mutations remain available to consumers that explicitly request OS changes.
class PhotoLibrary {
public:
    virtual ~PhotoLibrary() = default;
    virtual QString name() const = 0;
    virtual Access access() const = 0;
    virtual void authorize(std::function<void()> finished) = 0;
    virtual void cancel() {} // Retire outstanding native reads during handoff.
    virtual Snapshot scan(QString *error) = 0;
    // Resolve slow cross-device identities only after local gallery previews are published.
    virtual bool resolveIdentifiers(Snapshot &, QString *) { return true; }
    virtual bool preview(const QString &identifier, const QString &destination, QString *error) = 0;
    virtual bool exportResource(const QString &identifier, const QString &token,
                                const QString &destination, QString *error) = 0;
    virtual QString import(const QJsonObject &record, const QStringList &resources, QString *error) = 0;
    virtual bool trash(const QString &identifier, QString *error) = 0;
};
std::shared_ptr<PhotoLibrary> nativePhotoLibrary();
QString importedResourceName(const QString &id, int index, const QString &name);
QString importedPhotoId(const QString &name);
QString accessName(Access access);
}
