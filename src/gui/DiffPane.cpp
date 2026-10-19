#include "DiffPane.h"

#include "Theme.h"

#include <QFontDatabase>
#include <QPainter>
#include <QPlainTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QWheelEvent>

class GutterArea : public QWidget {
public:
    explicit GutterArea(DiffPane* pane) : QWidget(pane), pane_(pane) {}
    QSize sizeHint() const override { return QSize(pane_->gutterWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* e) override { pane_->paintGutter(e); }

private:
    DiffPane* pane_;
};

DiffPane::DiffPane(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setUndoRedoEnabled(false);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setFrameShape(QFrame::NoFrame);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#ifdef _WIN32
    mono = QFont(QStringLiteral("Consolas"));
#endif
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    setFont(mono);
    setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')) * 4);

    gutter_ = new GutterArea(this);
    connect(this, &QPlainTextEdit::updateRequest, this, &DiffPane::updateGutter);
    updateGutterWidth();
}

void DiffPane::setDiffDocument(QTextDocument* doc, int maxLineNumber) {
    QTextDocument* old = document();
    if (!qobject_cast<QPlainTextDocumentLayout*>(doc->documentLayout()))
        doc->setDocumentLayout(new QPlainTextDocumentLayout(doc));
    doc->setDefaultFont(font());
    QTextOption opt = doc->defaultTextOption();
    opt.setTabStopDistance(tabStopDistance());
    opt.setWrapMode(QTextOption::NoWrap);
    doc->setDefaultTextOption(opt);
    doc->setParent(this);
    setDocument(doc);
    if (old && old != doc && old->parent() == this) old->deleteLater();

    maxLineNumber_ = qMax(1, maxLineNumber);
    currentFirst_ = currentLast_ = -1;
    updateGutterWidth();
    viewport()->update();
}

void DiffPane::setCurrentBlock(int firstRow, int lastRow) {
    currentFirst_ = firstRow;
    currentLast_ = lastRow;
    viewport()->update();
    gutter_->update();
}

int DiffPane::gutterWidth() const {
    int digits = 1;
    for (int n = maxLineNumber_; n >= 10; n /= 10) ++digits;
    digits = qMax(digits, 3);
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void DiffPane::updateGutterWidth() { setViewportMargins(gutterWidth(), 0, 0, 0); }

void DiffPane::updateGutter(const QRect& rect, int dy) {
    if (dy) gutter_->scroll(0, dy);
    else gutter_->update(0, rect.y(), gutter_->width(), rect.height());
}

void DiffPane::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    gutter_->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void DiffPane::wheelEvent(QWheelEvent* e) {
    // Ctrl+wheel would zoom only this pane and break the row alignment; ignore it.
    if (e->modifiers() & Qt::ControlModifier) {
        e->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

static QColor rowColor(PaneRow kind, const Theme& th) {
    switch (kind) {
    case PaneRow::Changed: return th.changedBg;
    case PaneRow::Deleted: return th.deletedBg;
    case PaneRow::Inserted: return th.insertedBg;
    case PaneRow::Filler: return th.fillerBg;
    default: return QColor();
    }
}

void DiffPane::paintEvent(QPaintEvent* e) {
    // Row backgrounds first, then let QPlainTextEdit draw the text on top.
    {
        const Theme& th = Theme::current();
        QPainter p(viewport());
        const int width = viewport()->width();
        QTextBlock block = firstVisibleBlock();
        const QPointF offset = contentOffset();
        while (block.isValid()) {
            const QRectF r = blockBoundingGeometry(block).translated(offset);
            if (r.top() > e->rect().bottom()) break;
            const PaneRow kind = kindOf(block.userState());
            const QRectF full(0, r.top(), width, r.height());
            if (kind != PaneRow::Equal) {
                p.fillRect(full, rowColor(kind, th));
                if (kind == PaneRow::Filler) {
                    QBrush hatch(th.fillerHatch, Qt::BDiagPattern);
                    p.fillRect(full, hatch);
                }
            }
            const int row = block.blockNumber();
            if (row >= currentFirst_ && row <= currentLast_ && currentFirst_ >= 0) {
                p.setPen(QPen(th.currentBlockMarker, 1));
                if (row == currentFirst_) p.drawLine(QPointF(0, r.top() + 0.5), QPointF(width, r.top() + 0.5));
                if (row == currentLast_) p.drawLine(QPointF(0, r.bottom() - 0.5), QPointF(width, r.bottom() - 0.5));
            }
            block = block.next();
        }
    }
    QPlainTextEdit::paintEvent(e);
}

void DiffPane::paintGutter(QPaintEvent* e) {
    const Theme& th = Theme::current();
    QPainter p(gutter_);
    p.fillRect(e->rect(), th.gutterBg);
    p.setFont(font());
    const int w = gutter_->width();

    QTextBlock block = firstVisibleBlock();
    const QPointF offset = contentOffset();
    while (block.isValid()) {
        const QRectF r = blockBoundingGeometry(block).translated(offset);
        if (r.top() > e->rect().bottom()) break;
        if (r.bottom() >= e->rect().top()) {
            const int state = block.userState();
            const PaneRow kind = kindOf(state);
            const QRectF full(0, r.top(), w, r.height());
            if (kind != PaneRow::Equal) {
                QColor c = rowColor(kind, th);
                p.fillRect(full, c);
                if (kind == PaneRow::Filler) p.fillRect(full, QBrush(th.fillerHatch, Qt::BDiagPattern));
            }
            const int row = block.blockNumber();
            if (row >= currentFirst_ && row <= currentLast_ && currentFirst_ >= 0)
                p.fillRect(QRectF(0, r.top(), 3, r.height()), th.currentBlockMarker);
            const int line = lineOf(state);
            if (line > 0) {
                p.setPen(kind == PaneRow::Equal ? th.gutterText : th.gutterChangedText);
                p.drawText(QRectF(0, r.top(), w - 8, r.height()), Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(line));
            }
        }
        block = block.next();
    }
}
