#include <iiPhotoLibrary/PhotoController.h>
#include "FlatModelContract.h"
#include "CTestSupport.h"
#include <QCoreApplication>
#include <QPersistentModelIndex>

class GalleryModelTests final {
private:
    static QVariant row(const char *id, int revision = 0) {
        return QVariantMap{{"id", QString::fromLatin1(id)}, {"galleryKey", QString::fromLatin1(id)}, {"revision", revision}};
    }
public:
    void incrementalChangesPreserveIdentity() {
        iiPhotoLibrary::PhotoGalleryModel model;
        photo_test::FlatModelContract contract(&model);
        model.replace({row("a"), row("b"), row("c")});
        QPersistentModelIndex b(model.index(1)), c(model.index(2));
        photo_test::SignalCounter resets(&model, &QAbstractItemModel::modelReset);
        photo_test::SignalCounter insertions(&model, &QAbstractItemModel::rowsInserted);
        photo_test::SignalCounter removals(&model, &QAbstractItemModel::rowsRemoved);
        photo_test::SignalCounter changes(&model, &QAbstractItemModel::dataChanged);
        model.replace({row("c"), row("b", 2), row("d")});
        CTEST_COMPARE(resets.count(), 0); CTEST_COMPARE(insertions.count(), 1); CTEST_COMPARE(removals.count(), 1);
        CTEST_CHECK(changes.count() > 0);
        CTEST_CHECK(b.isValid()); CTEST_COMPARE(b.row(), 1); CTEST_COMPARE(b.data(Qt::UserRole).toMap().value("revision").toInt(), 2);
        CTEST_CHECK(c.isValid()); CTEST_COMPARE(c.row(), 0); CTEST_COMPARE(c.data(Qt::UserRole).toMap().value("id").toString(), QString("c"));
        CTEST_COMPARE(model.rowCount(), 3);
        const auto changedCount = changes.count();
        model.replace({row("c"), row("b", 2), row("d")});
        CTEST_COMPARE(resets.count(), 0); CTEST_COMPARE(insertions.count(), 1); CTEST_COMPARE(removals.count(), 1);
        CTEST_COMPARE(changes.count(), changedCount);
        CTEST_CHECK_MESSAGE(contract.good(), contract.error().c_str());
    }
    void ambiguousIdentitiesResetSafely() {
        iiPhotoLibrary::PhotoGalleryModel model;
        photo_test::FlatModelContract contract(&model);
        model.replace({row("a"), row("b")});
        photo_test::SignalCounter resets(&model, &QAbstractItemModel::modelReset);
        model.replace({row("a"), row("a", 1)});
        CTEST_COMPARE(resets.count(), 1); CTEST_COMPARE(model.rowCount(), 2);
        CTEST_CHECK_MESSAGE(contract.good(), contract.error().c_str());
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    photo_test::CTestRunner runner;
    GalleryModelTests tests;
    runner.run("incrementalChangesPreserveIdentity", [&] { tests.incrementalChangesPreserveIdentity(); });
    runner.run("ambiguousIdentitiesResetSafely", [&] { tests.ambiguousIdentitiesResetSafely(); });
    return runner.result();
}
