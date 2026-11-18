#include <iiPhotoLibrary/PhotoPresentationBridge.h>
#include <iiPhotoLibrary/PhotoViewModel.h>
#include <iiPhotoLibrary/PhotoController.h>
#include "CTestSupport.h"
#include <QCoreApplication>
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

class PhotoQtCompatibilityTests final {
    static QVariant row(const char* id, const char* name, const char* revision) {
        return QVariantMap{{"id", id}, {"galleryKey", id}, {"name", name},
                           {"preview", QUrl("file:///fixture.jpg")}, {"previewStamp", revision}, {"media", "photo"}};
    }
public:
    void existingServiceChangesAndRetires() {
        auto model = std::make_shared<PhotoViewModel>();
        auto controller = std::make_unique<PhotoController>(PhotoController::Sender{}, nullptr,
                                                            std::make_shared<CompatibilityLibrary>());
        auto* gallery = static_cast<PhotoGalleryModel*>(controller->galleryModel());
        gallery->replace({row("a", "First", "1"), row("b", "Second", "1")});
        std::vector<std::string> invalidations;
        PhotoPresentationBridge bridge(controller.get(), model, {}, [&](auto source) { invalidations.emplace_back(source); });
        CTEST_COMPARE(model->rows.get().size(), std::size_t(2));
        CTEST_COMPARE(invalidations.size(), std::size_t(1));invalidations.clear();
        auto first = model->rows.get()[0], second = model->rows.get()[1];
        gallery->replace({row("a", "Updated", "2"), row("b", "Second", "1")});
        CTEST_WAIT_VERIFY(first->name.get() == std::string("Updated"), 5000);
        CTEST_WAIT_VERIFY(invalidations.size() == std::size_t(1), 5000);
        gallery->replace({row("b", "Second", "1"), row("c", "Third", "1"), row("a", "Updated", "2")});
        CTEST_WAIT_VERIFY(model->rows.get().size() == std::size_t(3), 5000);
        CTEST_COMPARE(model->rows.get()[0], second);
        CTEST_COMPARE(model->rows.get()[2], first);
        gallery->replace({row("b", "Second", "1")});
        CTEST_WAIT_VERIFY(model->rows.get().size() == std::size_t(1), 5000);
        CTEST_COMPARE(model->rows.get()[0], second);
        first->select();
        CTEST_CHECK(model->selectedId.get().empty());
        controller.reset();
        CTEST_CHECK(model->rows.get().empty());
        second->select();
        CTEST_CHECK(model->selectedId.get().empty());
    }

    void previewBecomesVisibleOnlyWhenReady() {
        auto model = std::make_shared<PhotoViewModel>();
        PhotoController controller({}, nullptr, std::make_shared<CompatibilityLibrary>());
        auto* gallery = static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a", "First", "")});
        std::vector<std::string> invalidations;
        PhotoPresentationBridge bridge(&controller, model, {}, [&](auto source) { invalidations.emplace_back(source); });
        CTEST_COMPARE(model->rows.get().size(), std::size_t(1));
        auto first = model->rows.get().front();
        CTEST_CHECK(first->preview.get().empty());
        first->select();
        CTEST_CHECK(model->selectedPreview.get().empty());
        bool readyAfterInvalidation = true;
        auto previewObserver = first->preview.observe([&](const auto& source, std::uint64_t) {
            if (!source.empty() && (invalidations.empty() || invalidations.back() != source))
                readyAfterInvalidation = false;
        });
        gallery->replace({row("a", "First", "1")});
        CTEST_WAIT_VERIFY(first->preview.get() == std::string("file:///fixture.jpg"), 5000);
        CTEST_COMPARE(model->selectedPreview.get(), std::string("file:///fixture.jpg"));
        CTEST_CHECK(readyAfterInvalidation);
        CTEST_COMPARE(invalidations.size(), std::size_t(1));
        gallery->replace({row("a", "First", "")});
        CTEST_WAIT_VERIFY(first->preview.get().empty(), 5000);
        CTEST_CHECK(model->selectedPreview.get().empty());
        invalidations.clear();
        gallery->replace({row("a", "First", "2")});
        CTEST_WAIT_VERIFY(first->preview.get() == std::string("file:///fixture.jpg"), 5000);
        CTEST_CHECK(readyAfterInvalidation);
        CTEST_COMPARE(invalidations.size(), std::size_t(1));
    }

    void bridgeMayRetireDuringDeferredReconciliation() {
        auto model = std::make_shared<PhotoViewModel>();
        PhotoController controller({}, nullptr, std::make_shared<CompatibilityLibrary>());
        auto* gallery = static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a", "First", "1")});
        auto bridge = std::make_unique<PhotoPresentationBridge>(&controller, model);
        auto subscription = model->rows.observe([&](const auto&, std::uint64_t) { bridge.reset(); });
        gallery->replace({row("a", "First", "1"), row("b", "Second", "1")});
        CTEST_WAIT_VERIFY(!bridge, 5000);
        CTEST_COMPARE(model->rows.get().size(), std::size_t(2));
    }
};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    photo_test::CTestRunner runner;
    PhotoQtCompatibilityTests tests;
    runner.run("existingServiceChangesAndRetires", [&] { tests.existingServiceChangesAndRetires(); });
    runner.run("previewBecomesVisibleOnlyWhenReady", [&] { tests.previewBecomesVisibleOnlyWhenReady(); });
    runner.run("bridgeMayRetireDuringDeferredReconciliation", [&] { tests.bridgeMayRetireDuringDeferredReconciliation(); });
    return runner.result();
}
