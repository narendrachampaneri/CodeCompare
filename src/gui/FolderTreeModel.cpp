#include "FolderTreeModel.h"

#include "PathUtil.h"
#include "Theme.h"

#include <QDateTime>
#include <QFileIconProvider>
#include <QPainter>
#include <QPixmap>
#include <unordered_map>

FolderTreeModel::FolderTreeModel(QObject* parent) : QAbstractItemModel(parent) {
    QFileIconProvider provider;
    dirIcon_ = provider.icon(QFileIconProvider::Folder);
    fileIcon_ = provider.icon(QFileIconProvider::File);
    nodes_.resize(1);
}

void FolderTreeModel::clear() {
    beginResetModel();
    result_.reset();
    nodes_.assign(1, Node{});
    nodeOfEntry_.clear();
    endResetModel();
}

void FolderTreeModel::setResult(std::shared_ptr<const cc::FolderCompareResult> result) {
    beginResetModel();
    result_ = std::move(result);
    statusIcons_.clear();
    buildNodes();
    applyFilter();
    endResetModel();
}

void FolderTreeModel::setFilter(Filter f, const QString& text) {
    if (f == filter_ && text == text_) return;
    beginResetModel();
    filter_ = f;
    text_ = text;
    applyFilter();
    endResetModel();
}

void FolderTreeModel::buildNodes() {
    nodes_.assign(1, Node{});
    nodeOfEntry_.clear();
    if (!result_) return;
    const auto& entries = result_->entries;
    nodeOfEntry_.assign(entries.size(), -1);
    std::unordered_map<std::string, int> nodeOfKey;
    nodeOfKey.reserve(entries.size());
    nodes_.reserve(entries.size() + 1);
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const cc::Entry& e = entries[static_cast<std::size_t>(i)];
        int parent = 0;
        const auto slash = e.key.rfind('/');
        if (slash != std::string::npos) {
            auto it = nodeOfKey.find(e.key.substr(0, slash));
            if (it != nodeOfKey.end()) parent = it->second;
        }
        Node n;
        n.entry = i;
        n.parent = parent;
        const int id = static_cast<int>(nodes_.size());
        nodes_.push_back(std::move(n));
        nodes_[static_cast<std::size_t>(parent)].all.push_back(id);
        nodeOfKey.emplace(e.key, id);
        nodeOfEntry_[static_cast<std::size_t>(i)] = id;
    }
}

bool FolderTreeModel::entryMatches(const cc::Entry& e) const {
    switch (filter_) {
    case Filter::All: return true;
    case Filter::Differences: return cc::isDifferent(e.status);
    case Filter::Modified:
        return e.status == cc::EntryStatus::Modified || e.status == cc::EntryStatus::TypeMismatch ||
               e.status == cc::EntryStatus::Error;
    case Filter::OnlyLeft: return e.status == cc::EntryStatus::OnlyInLeft;
    case Filter::OnlyRight: return e.status == cc::EntryStatus::OnlyInRight;
    }
    return true;
}

bool FolderTreeModel::computeVisible(int nodeId) {
    Node& n = nodes_[static_cast<std::size_t>(nodeId)];
    n.visible.clear();
    for (int c : n.all) {
        if (computeVisible(c)) {
            nodes_[static_cast<std::size_t>(c)].row = static_cast<int>(n.visible.size());
            n.visible.push_back(c);
        }
    }
    if (nodeId == 0) return true;
    const cc::Entry& e = result_->entries[static_cast<std::size_t>(n.entry)];
    const bool textOk = text_.isEmpty() || qUtf8(e.name).contains(text_, Qt::CaseInsensitive);
    bool self;
    if (e.isDir) {
        // A folder is shown if something inside it is shown, or it matches by itself
        // (e.g. an empty folder that exists on one side only).
        self = textOk && entryMatches(e) &&
               (filter_ == Filter::All || e.status != cc::EntryStatus::Modified);
        if (filter_ == Filter::All && !text_.isEmpty()) self = textOk;
    } else {
        self = textOk && entryMatches(e);
    }
    n.visibleSelf = self || !n.visible.empty();
    return n.visibleSelf;
}

void FolderTreeModel::applyFilter() {
    if (!result_) return;
    computeVisible(0);
}

int FolderTreeModel::entryIndex(const QModelIndex& index) const {
    if (!index.isValid()) return -1;
    const int nodeId = static_cast<int>(index.internalId());
    return nodes_[static_cast<std::size_t>(nodeId)].entry;
}

bool FolderTreeModel::isEntryVisible(int entryIndex) const {
    if (entryIndex < 0 || entryIndex >= static_cast<int>(nodeOfEntry_.size())) return false;
    int node = nodeOfEntry_[static_cast<std::size_t>(entryIndex)];
    while (node > 0) {
        if (!nodes_[static_cast<std::size_t>(node)].visibleSelf) return false;
        node = nodes_[static_cast<std::size_t>(node)].parent;
    }
    return true;
}

QModelIndex FolderTreeModel::indexForEntry(int entryIndex, int column) const {
    if (!isEntryVisible(entryIndex)) return {};
    const int node = nodeOfEntry_[static_cast<std::size_t>(entryIndex)];
    return createIndex(nodes_[static_cast<std::size_t>(node)].row, column, quintptr(node));
}

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (column < 0 || column >= ColCount || row < 0) return {};
    const int parentNode = parent.isValid() ? static_cast<int>(parent.internalId()) : 0;
    const Node& p = nodes_[static_cast<std::size_t>(parentNode)];
    if (row >= static_cast<int>(p.visible.size())) return {};
    return createIndex(row, column, quintptr(p.visible[static_cast<std::size_t>(row)]));
}

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    const Node& n = nodes_[static_cast<std::size_t>(child.internalId())];
    if (n.parent <= 0) return {};
    const Node& p = nodes_[static_cast<std::size_t>(n.parent)];
    return createIndex(p.row, 0, quintptr(n.parent));
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const {
    if (!result_) return 0;
    if (parent.isValid() && parent.column() != 0) return 0;
    const int node = parent.isValid() ? static_cast<int>(parent.internalId()) : 0;
    return static_cast<int>(nodes_[static_cast<std::size_t>(node)].visible.size());
}

int FolderTreeModel::columnCount(const QModelIndex&) const { return ColCount; }

Qt::ItemFlags FolderTreeModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QIcon FolderTreeModel::statusIcon(cc::EntryStatus s) const {
    if (statusIcons_.empty()) {
        const Theme& th = Theme::current();
        for (int i = 0; i <= static_cast<int>(cc::EntryStatus::Error); ++i) {
            QPixmap pm(24, 24);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(th.statusColor(static_cast<cc::EntryStatus>(i)));
            p.drawEllipse(QRectF(6, 6, 12, 12));
            p.end();
            statusIcons_.push_back(QIcon(pm));
        }
    }
    return statusIcons_[static_cast<std::size_t>(s)];
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !result_) return {};
    const Node& n = nodes_[static_cast<std::size_t>(index.internalId())];
    const cc::Entry& e = result_->entries[static_cast<std::size_t>(n.entry)];
    const Theme& th = Theme::current();

    if (role == EntryIndexRole) return n.entry;

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColName: return qUtf8(e.name);
        case ColStatus: {
            QString s = QString::fromLatin1(cc::statusName(e.status));
            if (e.status == cc::EntryStatus::Modified && e.binary) s += tr(" (binary)");
            return s;
        }
        case ColLeftSize:
            return (e.inLeft && !e.leftIsDir) ? humanSize(e.sizeLeft) : QVariant();
        case ColRightSize:
            return (e.inRight && !e.rightIsDir) ? humanSize(e.sizeRight) : QVariant();
        case ColLeftDate:
            return (e.inLeft && e.mtimeLeft)
                       ? QDateTime::fromSecsSinceEpoch(e.mtimeLeft).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                       : QVariant();
        case ColRightDate:
            return (e.inRight && e.mtimeRight)
                       ? QDateTime::fromSecsSinceEpoch(e.mtimeRight).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                       : QVariant();
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == ColName) return e.isDir ? dirIcon_ : fileIcon_;
        if (index.column() == ColStatus) return statusIcon(e.status);
        break;
    case Qt::ForegroundRole:
        if (index.column() == ColName || index.column() == ColStatus) {
            if (e.status == cc::EntryStatus::Identical) {
                return index.column() == ColStatus ? QVariant(th.statusIdentical) : QVariant();
            }
            return th.statusColor(e.status);
        }
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == ColLeftSize || index.column() == ColRightSize)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case Qt::ToolTipRole: {
        QString tip = qUtf8(e.displayPath);
        if (!e.error.empty()) tip += QStringLiteral("\n") + qUtf8(e.error);
        if (e.firstDiffOffset >= 0 && e.binary)
            tip += tr("\nFirst difference at byte offset %1 (0x%2)")
                       .arg(e.firstDiffOffset)
                       .arg(e.firstDiffOffset, 0, 16);
        return tip;
    }
    default:
        break;
    }
    return {};
}

QVariant FolderTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColName: return tr("Name");
    case ColStatus: return tr("Status");
    case ColLeftSize: return tr("Left size");
    case ColRightSize: return tr("Right size");
    case ColLeftDate: return tr("Left modified");
    case ColRightDate: return tr("Right modified");
    }
    return {};
}
