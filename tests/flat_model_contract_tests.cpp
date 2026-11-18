#include "CTestSupport.h"
#include "FlatModelContract.h"
#include <QAbstractListModel>
#include <QCoreApplication>
#include <QPersistentModelIndex>
#include <QVariantList>
#include <algorithm>

class FixtureList final : public QAbstractListModel {
public:
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : values_.size(); }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        return role == Qt::UserRole && index.isValid() && index.row() < values_.size() ? values_.at(index.row()) : QVariant{};
    }
    void insert(int row, const QVariant& value, bool wrongCount = false) {
        beginInsertRows({}, row, row);
        values_.insert(row, value);
        if (wrongCount) values_.insert(row, value);
        endInsertRows();
    }
    void remove(int row, bool wrongCount = false) {
        beginRemoveRows({}, row, row);
        values_.removeAt(row);
        if (wrongCount) values_.removeAt(row);
        endRemoveRows();
    }
    void reverse(bool preserveIndexes) {
        emit layoutAboutToBeChanged();
        const auto old = persistentIndexList();
        QModelIndexList next;
        for (const auto& index : old) next.append(createIndex(values_.size() - 1 - index.row(), 0));
        std::reverse(values_.begin(), values_.end());
        if (preserveIndexes) changePersistentIndexList(old, next);
        emit layoutChanged();
    }
    void reset() { beginResetModel(); values_.clear(); endResetModel(); }
private:
    QVariantList values_;
};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    photo_test::CTestRunner runner;
    runner.run("acceptsValidFlatModelNotifications", [] {
        FixtureList model;
        photo_test::FlatModelContract contract(&model);
        model.insert(0, "a"); model.insert(1, "b"); model.insert(1, "c");
        model.remove(1); model.reverse(true); model.reset();
        CTEST_CHECK_MESSAGE(contract.good(), contract.error().c_str());
    });
    runner.run("rejectsIncorrectInsertionCount", [] {
        FixtureList model;
        photo_test::FlatModelContract contract(&model);
        model.insert(0, "a", true);
        CTEST_CHECK(!contract.good());
    });
    runner.run("rejectsIncorrectRemovalCount", [] {
        FixtureList model;
        model.insert(0, "a"); model.insert(1, "b"); model.insert(2, "c");
        photo_test::FlatModelContract contract(&model);
        model.remove(0, true);
        CTEST_CHECK(!contract.good());
    });
    runner.run("rejectsLayoutThatLosesPersistentIdentity", [] {
        FixtureList model;
        model.insert(0, "a"); model.insert(1, "b");
        photo_test::FlatModelContract contract(&model);
        model.reverse(false);
        CTEST_CHECK(!contract.good());
    });
    return runner.result();
}
