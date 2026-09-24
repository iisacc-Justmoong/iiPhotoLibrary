#include <iiPhotoLibrary/PhotoLibrary.h>
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>
#include <QUuid>

namespace iiPhotoLibrary {
namespace {
constexpr char javaClass[] = "com/iisacc/iiphotolibrary/PhotoLibrary";
QList<std::function<void()>> authorizations;
void accessFinished(JNIEnv *, jclass) {
    QMetaObject::invokeMethod(qApp, [] { auto callbacks = std::exchange(authorizations, {}); for (auto &done : callbacks) done(); }, Qt::QueuedConnection);
}
QJsonObject execute(QJsonObject request, QString *error) {
    const auto payload = QJniObject::fromString(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
    const auto context = QNativeInterface::QAndroidApplication::context();
    const auto result = QJniObject::callStaticObjectMethod(javaClass, "execute", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;", context.object(), payload.object<jstring>());
    QJniEnvironment environment;
    if (environment.checkAndClearExceptions() || !result.isValid()) { if (error) *error = "The system gallery is unavailable."; return {}; }
    const auto response = QJsonDocument::fromJson(result.toString().toUtf8()).object();
    if (!response.value("ok").toBool() && error) *error = response.value("error").toString("The gallery operation failed.");
    return response;
}
class AndroidLibrary final : public PhotoLibrary {
    QString session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject run(QJsonObject request, QString *error) const {
        request["session"] = session; return execute(std::move(request), error);
    }
public:
    void cancel() override {
        const auto id = QJniObject::fromString(session);
        QJniObject::callStaticMethod<void>(javaClass, "cancelSession", "(Ljava/lang/String;)V", id.object<jstring>());
        QJniEnvironment environment; environment.checkAndClearExceptions();
    }
    QString name() const override { return "System gallery"; }
    Access access() const override {
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto result = QJniObject::callStaticObjectMethod(javaClass, "access", "(Landroid/content/Context;)Ljava/lang/String;", context.object());
        QJniEnvironment environment; if (environment.checkAndClearExceptions()) return Access::Denied;
        return result.toString() == "full" ? Access::Full : result.toString() == "limited" ? Access::Limited : Access::Denied;
    }
    void authorize(std::function<void()> finished) override {
        QJniEnvironment environment;
        static const bool registered = environment.registerNativeMethods(javaClass, {{"photoAccessFinished", "()V", reinterpret_cast<void *>(accessFinished)}});
        if (!registered || !QNativeInterface::QAndroidApplication::isActivityContext()) { finished(); return; }
        authorizations.append(std::move(finished)); const auto context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(javaClass, "requestAccess", "(Landroid/app/Activity;)V", context.object());
        if (environment.checkAndClearExceptions()) accessFinished(nullptr, nullptr);
    }
    Snapshot scan(QString *error) override {
        const auto result = run({{"action", "scan"}}, error); Snapshot snapshot;
        snapshot.complete = result.value("complete").toBool();
        snapshot.revision = result.value("revision").toString();
        for (const auto &value : result.value("assets").toArray()) {
            const auto row = value.toObject(); Asset asset;
            asset.identifier = row.value("identifier").toString(); asset.name = row.value("name").toString();
            asset.stamp = row.value("stamp").toString(); asset.media = row.value("media").toString();
            asset.created = QDateTime::fromMSecsSinceEpoch(row.value("created").toString().toLongLong(), QTimeZone::UTC);
            for (const auto &resourceValue : row.value("resources").toArray()) {
                const auto resource = resourceValue.toObject(); asset.resources.append({resource.value("token").toString(), resource.value("name").toString(), resource.value("role").toString()});
            }
            snapshot.assets.append(asset);
        }
        return snapshot;
    }
    bool preview(const QString &id, const QString &path, QString *error) override {
        return run({{"action", "preview"}, {"identifier", id}, {"path", path}}, error).value("ok").toBool();
    }
    bool exportResource(const QString &, const QString &token, const QString &path, QString *error) override {
        return run({{"action", "export"}, {"token", token}, {"path", path}}, error).value("ok").toBool();
    }
    QString import(const QJsonObject &record, const QStringList &paths, QString *error) override {
        return run({{"action", "import"}, {"record", record}, {"paths", QJsonArray::fromStringList(paths)}}, error).value("identifier").toString();
    }
    bool trash(const QString &id, QString *error) override { return run({{"action", "trash"}, {"identifier", id}}, error).value("ok").toBool(); }
};
}
std::shared_ptr<PhotoLibrary> nativePhotoLibrary() { return std::make_shared<AndroidLibrary>(); }
}
