#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QPersistentModelIndex>
#include <QVariant>
#include <string>
#include <vector>

namespace photo_test {

// Contract for a non-lazy, one-column list model. Errors are recorded in signal
// callbacks and checked later so exceptions never propagate through Qt signals.
class FlatModelContract final {
public:
    explicit FlatModelContract(QAbstractItemModel* model) : model_(model)
    {
        if (!model_) { fail("model is null"); return; }
        QObject::connect(model_, &QObject::destroyed, &receiver_, [this] {
            model_ = nullptr;
            fail("model was destroyed before its contract observer");
        });
        QObject::connect(model_, &QAbstractItemModel::rowsAboutToBeInserted, &receiver_,
            [this](const QModelIndex& parent, int first, int last) { begin(Change::Insert, parent, first, last); });
        QObject::connect(model_, &QAbstractItemModel::rowsInserted, &receiver_,
            [this](const QModelIndex& parent, int first, int last) { finishRows(Change::Insert, parent, first, last); });
        QObject::connect(model_, &QAbstractItemModel::rowsAboutToBeRemoved, &receiver_,
            [this](const QModelIndex& parent, int first, int last) { begin(Change::Remove, parent, first, last); });
        QObject::connect(model_, &QAbstractItemModel::rowsRemoved, &receiver_,
            [this](const QModelIndex& parent, int first, int last) { finishRows(Change::Remove, parent, first, last); });
        QObject::connect(model_, &QAbstractItemModel::layoutAboutToBeChanged, &receiver_,
            [this] { begin(Change::Layout, {}, 0, 0); });
        QObject::connect(model_, &QAbstractItemModel::layoutChanged, &receiver_, [this] { finishLayout(); });
        QObject::connect(model_, &QAbstractItemModel::modelAboutToBeReset, &receiver_,
            [this] { begin(Change::Reset, {}, 0, 0); });
        QObject::connect(model_, &QAbstractItemModel::modelReset, &receiver_, [this] { finishReset(); });
        QObject::connect(model_, &QAbstractItemModel::dataChanged, &receiver_,
            [this](const QModelIndex& first, const QModelIndex& last, const QList<int>&) {
                check(pending_ == Change::None, "data changed during another model operation");
                check(first.isValid() && last.isValid() && first.model() == model_ && last.model() == model_,
                      "data change indexes are invalid or belong to another model");
                check(first.column() == 0 && last.column() == 0 && first.row() <= last.row(),
                      "data change range is invalid");
                validate();
            });
        QObject::connect(model_, &QAbstractItemModel::rowsAboutToBeMoved, &receiver_,
            [this] { fail("row moves are outside this flat-model observer's supported operations"); });
        QObject::connect(model_, &QAbstractItemModel::columnsAboutToBeInserted, &receiver_,
            [this] { fail("a flat one-column model attempted to insert columns"); });
        QObject::connect(model_, &QAbstractItemModel::columnsAboutToBeRemoved, &receiver_,
            [this] { fail("a flat one-column model attempted to remove columns"); });
        validate();
    }

    [[nodiscard]] bool good()
    {
        check(pending_ == Change::None, "model operation has no matching completion signal");
        validate();
        return errors_.empty();
    }
    [[nodiscard]] const std::string& error() const
    {
        static const std::string none;
        return errors_.empty() ? none : errors_.front();
    }

private:
    enum class Change { None, Insert, Remove, Layout, Reset };
    struct Saved { QPersistentModelIndex index; int row; QVariant value; };
    QAbstractItemModel* model_;
    QObject receiver_;
    Change pending_ = Change::None;
    int before_ = 0, first_ = 0, last_ = 0;
    std::vector<Saved> saved_;
    std::vector<std::string> errors_;

    void fail(const char* message) { errors_.emplace_back(message); }
    void check(bool condition, const char* message) { if (!condition) fail(message); }

    void validate()
    {
        if (!model_) return;
        const int rows = model_->rowCount();
        check(rows >= 0, "negative row count");
        check(model_->columnCount() == 1, "root must have one column");
        check(model_->hasChildren() == (rows > 0), "root child state does not match row count");
        check(!model_->index(-1, 0).isValid(), "negative row produced a valid index");
        check(!model_->index(rows, 0).isValid(), "past-the-end row produced a valid index");
        check(!model_->index(0, -1).isValid() && !model_->index(0, 1).isValid(), "invalid column produced a valid index");
        for (int row = 0; row < rows; ++row) {
            const auto index = model_->index(row, 0);
            check(index.isValid() && index.model() == model_ && index.row() == row && index.column() == 0,
                  "index does not identify the expected row");
            if (!index.isValid()) continue;
            check(!index.parent().isValid(), "a flat list index has a parent");
            check(model_->rowCount(index) == 0 && model_->columnCount(index) == 0 && !model_->hasChildren(index),
                  "a flat list item has children");
            check(model_->checkIndex(index, QAbstractItemModel::CheckIndexOption::IndexIsValid |
                                           QAbstractItemModel::CheckIndexOption::ParentIsInvalid),
                  "Qt Core rejected an index");
        }
    }

    void begin(Change change, const QModelIndex& parent, int first, int last)
    {
        if (!model_) return;
        if (pending_ != Change::None) { fail("overlapping model operations"); return; }
        validate();
        before_ = model_->rowCount(); first_ = first; last_ = last;
        if (change == Change::Insert || change == Change::Remove) {
            check(!parent.isValid(), "row operation used a non-root parent");
            check(first >= 0 && last >= first, "invalid row operation range");
            if (change == Change::Insert) check(first <= before_, "insertion begins beyond the end");
            else check(last < before_, "removal extends beyond the end");
        }
        saved_.clear();
        for (int row = 0; row < before_; ++row) {
            const auto index = model_->index(row, 0);
            saved_.push_back({QPersistentModelIndex(index), row, model_->data(index, Qt::UserRole)});
        }
        pending_ = change;
    }

    void finishRows(Change change, const QModelIndex& parent, int first, int last)
    {
        if (!model_) return;
        if (pending_ != change) { fail("row completion has no matching beginning"); return; }
        check(!parent.isValid() && first == first_ && last == last_, "row completion range changed");
        const int count = last_ - first_ + 1;
        check(model_->rowCount() == before_ + (change == Change::Insert ? count : -count),
              "row count disagrees with the announced operation");
        for (const auto& saved : saved_) {
            const bool removed = change == Change::Remove && saved.row >= first_ && saved.row <= last_;
            if (removed) { check(!saved.index.isValid(), "removed row kept a persistent index"); continue; }
            const int expected = change == Change::Insert && saved.row >= first_ ? saved.row + count
                               : change == Change::Remove && saved.row > last_ ? saved.row - count : saved.row;
            check(saved.index.isValid() && saved.index.row() == expected, "persistent index shifted incorrectly");
            if (saved.index.isValid()) check(saved.index.data(Qt::UserRole) == saved.value, "persistent row identity changed");
        }
        saved_.clear(); pending_ = Change::None; validate();
    }

    void finishLayout()
    {
        if (!model_) return;
        if (pending_ != Change::Layout) { fail("layout completion has no matching beginning"); return; }
        check(model_->rowCount() == before_, "layout change modified row count");
        for (const auto& saved : saved_) {
            check(saved.index.isValid(), "layout change invalidated a persistent index");
            if (saved.index.isValid()) check(saved.index.data(Qt::UserRole) == saved.value, "layout change lost persistent row identity");
        }
        saved_.clear(); pending_ = Change::None; validate();
    }

    void finishReset()
    {
        if (!model_) return;
        if (pending_ != Change::Reset) { fail("reset completion has no matching beginning"); return; }
        for (const auto& saved : saved_) check(!saved.index.isValid(), "reset kept a persistent index");
        saved_.clear(); pending_ = Change::None; validate();
    }
};
} // namespace photo_test
