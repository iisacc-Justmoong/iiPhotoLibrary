#include <iiPhotoLibrary/PhotoController.h>
#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

class GalleryModelTests final : public QObject {
    Q_OBJECT
private:
    static QVariant row(const char *id, int revision = 0) {
        return QVariantMap{{"id", QString::fromLatin1(id)}, {"galleryKey", QString::fromLatin1(id)}, {"revision", revision}};
    }
private slots:
    void incrementalChangesPreserveIdentity() {
        iiPhotoLibrary::PhotoGalleryModel model;
        QAbstractItemModelTester contract(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.replace({row("a"), row("b"), row("c")});
        QPersistentModelIndex b(model.index(1)), c(model.index(2));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy insertions(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removals(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        model.replace({row("c"), row("b", 2), row("d")});
        QCOMPARE(resets.size(), 0); QCOMPARE(insertions.size(), 1); QCOMPARE(removals.size(), 1);
        QVERIFY(!changes.isEmpty());
        QVERIFY(b.isValid()); QCOMPARE(b.row(), 1); QCOMPARE(b.data(Qt::UserRole).toMap().value("revision").toInt(), 2);
        QVERIFY(c.isValid()); QCOMPARE(c.row(), 0); QCOMPARE(c.data(Qt::UserRole).toMap().value("id").toString(), QString("c"));
        QCOMPARE(model.rowCount(), 3);
        const auto changedCount = changes.size();
        model.replace({row("c"), row("b", 2), row("d")});
        QCOMPARE(resets.size(), 0); QCOMPARE(insertions.size(), 1); QCOMPARE(removals.size(), 1);
        QCOMPARE(changes.size(), changedCount);
    }
    void ambiguousIdentitiesResetSafely() {
        iiPhotoLibrary::PhotoGalleryModel model;
        model.replace({row("a"), row("b")});
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.replace({row("a"), row("a", 1)});
        QCOMPARE(resets.size(), 1); QCOMPARE(model.rowCount(), 2);
    }
};
QTEST_GUILESS_MAIN(GalleryModelTests)
#include "gallery_model.moc"
