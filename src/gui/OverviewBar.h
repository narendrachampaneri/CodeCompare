// CodeCompare - narrow vertical map of the whole file showing where changes are.
// Clicking or dragging scrolls the diff to that position.
#pragma once

#include "DiffPane.h"

#include <QVector>
#include <QWidget>

class OverviewBar : public QWidget {
    Q_OBJECT
public:
    explicit OverviewBar(QWidget* parent = nullptr);

    void setRows(QVector<quint8> kinds);           // one PaneRow per diff row (right/left merged)
    void setViewport(int firstRow, int visibleRows);

    QSize sizeHint() const override { return QSize(16, 100); }
    QSize minimumSizeHint() const override { return QSize(16, 20); }

signals:
    void rowRequested(int row);   // centre this row in the view

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    int rowAt(int y) const;
    QVector<quint8> kinds_;
    int first_ = 0;
    int visible_ = 0;
};
