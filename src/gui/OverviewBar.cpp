#include "OverviewBar.h"

#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>

OverviewBar::OverviewBar(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Change overview - click to jump"));
}

void OverviewBar::setRows(QVector<quint8> kinds) {
    kinds_ = std::move(kinds);
    update();
}

void OverviewBar::setViewport(int firstRow, int visibleRows) {
    if (firstRow == first_ && visibleRows == visible_) return;
    first_ = firstRow;
    visible_ = visibleRows;
    update();
}

int OverviewBar::rowAt(int y) const {
    const int n = kinds_.size();
    if (n == 0 || height() <= 0) return 0;
    const double f = qBound(0.0, double(y) / height(), 1.0);
    return qBound(0, int(f * n), n - 1);
}

void OverviewBar::paintEvent(QPaintEvent*) {
    const Theme& th = Theme::current();
    QPainter p(this);
    p.fillRect(rect(), th.overviewBg);
    const int n = kinds_.size();
    if (n == 0) return;
    const double h = height();
    const double perRow = h / n;

    // Merge consecutive rows of the same kind into one rectangle.
    int i = 0;
    while (i < n) {
        const quint8 k = kinds_[i];
        int j = i + 1;
        while (j < n && kinds_[j] == k) ++j;
        if (k != static_cast<quint8>(PaneRow::Equal)) {
            QColor c;
            switch (static_cast<PaneRow>(k)) {
            case PaneRow::Changed: c = th.statusModified; break;
            case PaneRow::Deleted: c = th.statusOnlyLeft; break;
            case PaneRow::Inserted: c = th.statusOnlyRight; break;
            default: c = th.fillerHatch; break;
            }
            const double top = i * perRow;
            const double hh = qMax(2.0, (j - i) * perRow);
            p.fillRect(QRectF(2, top, width() - 4, hh), c);
        }
        i = j;
    }
    if (visible_ > 0 && visible_ < n) {
        const QRectF vp(0, first_ * perRow, width(), qMax(4.0, visible_ * perRow));
        p.fillRect(vp, th.overviewViewport);
    }
}

void OverviewBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit rowRequested(rowAt(int(e->position().y())));
}

void OverviewBar::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) emit rowRequested(rowAt(int(e->position().y())));
}
