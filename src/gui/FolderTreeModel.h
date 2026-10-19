// CodeCompare - tree model over a FolderCompareResult, with built-in filtering.
#pragma once

#include "Types.h"

#include <QAbstractItemModel>
#include <QIcon>
#include <memory>
#include <vector>

class FolderTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { ColName, ColStatus, ColLeftSize, ColRightSize, ColLeftDate, ColRightDate, ColCount };

    enum class Filter { All, Differences, Modified, OnlyLeft, OnlyRight };

    static constexpr int EntryIndexRole = Qt::UserRole + 1;

    explicit FolderTreeModel(QObject* parent = nullptr);

    void setResult(std::shared_ptr<const cc::FolderCompareResult> result);
    std::shared_ptr<const cc::FolderCompareResult> result() const { return result_; }
    void clear();

    void setFilter(Filter f, const QString& text);
    Filter filter() const { return filter_; }

    // Entry index (into result()->entries) for a model index, or -1.
    int entryIndex(const QModelIndex& index) const;
    // Model index for an entry, or invalid if the entry is hidden by the filter.
    QModelIndex indexForEntry(int entryIndex, int column = 0) const;
    // Is the entry currently visible (passes the filter)?
    bool isEntryVisible(int entryIndex) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    struct Node {
        int entry = -1;              // -1 for the invisible root
        int parent = -1;             // node index
        int row = 0;                 // row inside the parent's visible children
        std::vector<int> all;        // all child nodes
        std::vector<int> visible;    // children passing the filter
        bool visibleSelf = false;
    };

    void buildNodes();
    void applyFilter();
    bool entryMatches(const cc::Entry& e) const;
    bool computeVisible(int node);
    QIcon statusIcon(cc::EntryStatus s) const;

    std::shared_ptr<const cc::FolderCompareResult> result_;
    std::vector<Node> nodes_;              // nodes_[0] is the root
    std::vector<int> nodeOfEntry_;
    Filter filter_ = Filter::All;
    QString text_;
    QIcon dirIcon_, fileIcon_;
    mutable std::vector<QIcon> statusIcons_;
};
