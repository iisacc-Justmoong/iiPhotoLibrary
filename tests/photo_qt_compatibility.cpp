#include <iiPhotoLibrary/PhotoPresentationBridge.h>
#include <iiPhotoLibrary/PhotoViewModel.h>
#include <iiPhotoLibrary/PhotoController.h>
#include <QTest>
#include <memory>

using namespace iiPhotoLibrary;
using namespace iiPhotoLibrary::presentation;

class CompatibilityLibrary final : public PhotoLibrary {
public:
    QString name() const override { return "Compatibility fixture"; }
    Access access() const override { return Access::Full; }
    void authorize(std::function<void()> finished) override { finished(); }
    Snapshot scan(QString*) override { return {}; }
    bool preview(const QString&, const QString&, QString*) override { return false; }
    bool exportResource(const QString&, const QString&, const QString&, QString*) override { return false; }
    QString import(const QJsonObject&, const QStringList&, QString*) override { return {}; }
    bool trash(const QString&, QString*) override { return false; }
};

class PhotoQtCompatibilityTests final : public QObject {
    Q_OBJECT
    static QVariant row(const char* id, const char* name, const char* revision) {
        return QVariantMap{{"id", id}, {"galleryKey", id}, {"name", name},
                           {"preview", QUrl("file:///fixture.jpg")}, {"previewStamp", revision}, {"media", "photo"}};
    }
private slots:
    void existingServiceChangesAndRetires() {
        auto model = std::make_shared<PhotoViewModel>();
        auto controller = std::make_unique<PhotoController>(PhotoController::Sender{}, nullptr,
                                                            std::make_shared<CompatibilityLibrary>());
        auto* gallery = static_cast<PhotoGalleryModel*>(controller->galleryModel());
        gallery->replace({row("a", "First", "1"), row("b", "Second", "1")});
        std::vector<std::string> invalidations;
        PhotoPresentationBridge bridge(controller.get(), model, {}, [&](auto source) { invalidations.emplace_back(source); });
        QCOMPARE(model->rows.get().size(), std::size_t(2));
        auto first = model->rows.get()[0], second = model->rows.get()[1];
        gallery->replace({row("a", "Updated", "2"), row("b", "Second", "1")});
        QTRY_COMPARE(first->name.get(), std::string("Updated"));
        QTRY_COMPARE(invalidations.size(), std::size_t(1));
        gallery->replace({row("b", "Second", "1"), row("c", "Third", "1"), row("a", "Updated", "2")});
        QTRY_COMPARE(model->rows.get().size(), std::size_t(3));
        QCOMPARE(model->rows.get()[0], second);
        QCOMPARE(model->rows.get()[2], first);
        gallery->replace({row("b", "Second", "1")});
        QTRY_COMPARE(model->rows.get().size(), std::size_t(1));
        QCOMPARE(model->rows.get()[0], second);
        first->select();
        QVERIFY(model->selectedId.get().empty());
        controller.reset();
        QVERIFY(model->rows.get().empty());
        second->select();
        QVERIFY(model->selectedId.get().empty());
    }

    void bridgeMayRetireDuringDeferredReconciliation() {
        auto model = std::make_shared<PhotoViewModel>();
        PhotoController controller({}, nullptr, std::make_shared<CompatibilityLibrary>());
        auto* gallery = static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a", "First", "1")});
        auto bridge = std::make_unique<PhotoPresentationBridge>(&controller, model);
        auto subscription = model->rows.observe([&](const auto&, std::uint64_t) { bridge.reset(); });
        gallery->replace({row("a", "First", "1"), row("b", "Second", "1")});
        QTRY_VERIFY(!bridge);
        QCOMPARE(model->rows.get().size(), std::size_t(2));
    }
};

QTEST_GUILESS_MAIN(PhotoQtCompatibilityTests)
#include "photo_qt_compatibility.moc"
