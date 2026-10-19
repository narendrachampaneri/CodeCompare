// CodeCompare - one side of the side-by-side diff: read-only text with line-number
// gutter and per-row background colours.
#pragma once

#include <QPlainTextEdit>

enum class PaneRow : int { Equal = 0, Changed = 1, Deleted = 2, Inserted = 3, Filler = 4 };

class DiffPane : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit DiffPane(QWidget* parent = nullptr);

    // Takes ownership of doc. Each block's userState must be encodeState(...).
    void setDiffDocument(QTextDocument* doc, int maxLineNumber);

    static int encodeState(PaneRow kind, int lineNumber /*1-based, 0 = none*/) {
        return (lineNumber << 3) | static_cast<int>(kind);
    }
    static PaneRow kindOf(int state) { return state < 0 ? PaneRow::Equal : static_cast<PaneRow>(state & 7); }
    static int lineOf(int state) { return state < 0 ? 0 : (state >> 3); }

    // Rows [first, last] are the currently selected change block (drawn with a marker).
    void setCurrentBlock(int firstRow, int lastRow);

    int gutterWidth() const;

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    friend class GutterArea;
    void paintGutter(QPaintEvent* e);
    void updateGutterWidth();
    void updateGutter(const QRect& rect, int dy);

    QWidget* gutter_;
    int maxLineNumber_ = 1;
    int currentFirst_ = -1;
    int currentLast_ = -1;
};
