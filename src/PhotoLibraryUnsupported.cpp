#include <iiPhotoLibrary/PhotoLibrary.h>
namespace iiPhotoLibrary {
namespace {
class UnsupportedLibrary final : public PhotoLibrary {
public:
    QString name() const override { return "Photo storage"; }
    Access access() const override { return Access::Unsupported; }
    void authorize(std::function<void()> finished) override { finished(); }
    Snapshot scan(QString *) override { return {}; }
    bool preview(const QString &, const QString &, QString *) override { return false; }
    bool exportResource(const QString &, const QString &, const QString &, QString *) override { return false; }
    QString import(const QJsonObject &, const QStringList &, QString *) override { return {}; }
    bool trash(const QString &, QString *) override { return false; }
};
}
std::shared_ptr<PhotoLibrary> nativePhotoLibrary() { return std::make_shared<UnsupportedLibrary>(); }
}
