#include "DiffView.h"

#include "Diff.h"
#include "DiffPane.h"
#include "OverviewBar.h"
#include "PathUtil.h"
#include "Theme.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {

QString sideInfo(const cc::TextFile& tf, bool present) {
    if (!present) return QObject::tr("(does not exist)");
    if (!tf.ok) return QObject::tr("(cannot read)");
    QStringList parts;
    if (tf.binary) {
        parts << QObject::tr("Binary");
    } else if (tf.tooLarge) {
        parts << QObject::tr("Too large to show");
    } else {
        if (!tf.encoding.empty()) parts << qUtf8(tf.encoding);
        if (!tf.eolStyle.empty() && tf.eolStyle != "None") parts << qUtf8(tf.eolStyle);
        parts << QObject::tr("%n line(s)", "", static_cast<int>(tf.lines.size()));
    }
    parts << humanSize(tf.size);
    return parts.join(QStringLiteral("  ·  "));
}

QString eolMarker(cc::Eol e) {
    switch (e) {
    case cc::Eol::CRLF: return QStringLiteral("  ↵CRLF");
    case cc::Eol::LF: return QStringLiteral("  ↵LF");
    case cc::Eol::CR: return QStringLiteral("  ↵CR");
    case cc::Eol::None: return QStringLiteral("  (no newline at end)");
    }
    return {};
}

} // namespace

DiffView::DiffView(QWidget* parent) : QWidget(parent) {
    leftHeader_ = new QLabel(this);
    rightHeader_ = new QLabel(this);
    for (QLabel* l : {leftHeader_, rightHeader_}) {
        l->setTextFormat(Qt::RichText);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setContentsMargins(8, 4, 8, 4);
        l->setMinimumWidth(50);
        l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    banner_ = new QLabel(this);
    banner_->setWordWrap(true);
    banner_->setContentsMargins(10, 6, 10, 6);
    banner_->setAutoFillBackground(true);
    banner_->hide();

    left_ = new DiffPane(this);
    right_ = new DiffPane(this);
    overview_ = new OverviewBar(this);

    left_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    left_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    right_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    right_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    auto* headers = new QGridLayout;
    headers->setContentsMargins(0, 0, 0, 0);
    headers->setSpacing(0);
    headers->addWidget(leftHeader_, 0, 0);
    headers->addItem(new QSpacerItem(overview_->sizeHint().width(), 1, QSizePolicy::Fixed), 0, 1);
    headers->addWidget(rightHeader_, 0, 2);
    headers->setColumnStretch(0, 1);
    headers->setColumnStretch(2, 1);

    auto* panes = new QHBoxLayout;
    panes->setContentsMargins(0, 0, 0, 0);
    panes->setSpacing(0);
    panes->addWidget(left_, 1);
    panes->addWidget(overview_);
    panes->addWidget(right_, 1);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addLayout(headers);
    lay->addWidget(banner_);
    lay->addLayout(panes, 1);

    // Keep both sides scrolled together.
    connect(left_->verticalScrollBar(), &QScrollBar::valueChanged, right_->verticalScrollBar(), &QScrollBar::setValue);
    connect(right_->verticalScrollBar(), &QScrollBar::valueChanged, left_->verticalScrollBar(), &QScrollBar::setValue);
    connect(left_->horizontalScrollBar(), &QScrollBar::valueChanged, right_->horizontalScrollBar(), &QScrollBar::setValue);
    connect(right_->horizontalScrollBar(), &QScrollBar::valueChanged, left_->horizontalScrollBar(), &QScrollBar::setValue);
    connect(right_->verticalScrollBar(), &QScrollBar::valueChanged, this, &DiffView::updateOverviewViewport);
    connect(right_->verticalScrollBar(), &QScrollBar::rangeChanged, this, &DiffView::updateOverviewViewport);
    connect(overview_, &OverviewBar::rowRequested, this, [this](int row) {
        QScrollBar* sb = right_->verticalScrollBar();
        sb->setValue(qMax(0, row - sb->pageStep() / 2));
    });

    clear();
}

DiffView::~DiffView() {
    if (cancel_) cancel_->store(true);
}

void DiffView::clear() {
    if (cancel_) cancel_->store(true);
    ++generation_;
    result_.reset();
    current_ = -1;
    leftHeader_->setText(QString());
    rightHeader_->setText(QString());
    banner_->hide();
    left_->setDiffDocument(new QTextDocument, 1);
    right_->setDiffDocument(new QTextDocument, 1);
    overview_->setRows({});
    emit navigationChanged();
}

void DiffView::showMessage(const QString& title, const QString& message) {
    clear();
    leftHeader_->setText(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    QPalette pal = banner_->palette();
    pal.setColor(QPalette::Window, palette().color(QPalette::AlternateBase));
    banner_->setPalette(pal);
    banner_->setText(message);
    banner_->setVisible(!message.isEmpty());
    emit statusChanged(QString());
}

void DiffView::showFiles(const QString& leftPath, const QString& rightPath, const QString& displayName,
                         const cc::CompareOptions& options, const QString& note) {
    if (cancel_) cancel_->store(true);
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const quint64 gen = ++generation_;
    leftPath_ = leftPath;
    rightPath_ = rightPath;
    displayName_ = displayName;
    options_ = options;
    note_ = note;

    banner_->setText(tr("Comparing %1 ...").arg(displayName.toHtmlEscaped()));
    banner_->setPalette(palette());
    banner_->show();

    const std::filesystem::path lp = leftPath.isEmpty() ? std::filesystem::path() : toFsPath(leftPath);
    const std::filesystem::path rp = rightPath.isEmpty() ? std::filesystem::path() : toFsPath(rightPath);
    auto cancel = cancel_;
    auto* watcher = new QFutureWatcher<std::shared_ptr<cc::FileDiffResult>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, gen]() {
        watcher->deleteLater();
        if (gen != generation_) return;   // a newer request replaced this one
        render(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([lp, rp, options, cancel]() {
        cc::DiffLimits limits;
        limits.cancel = cancel.get();
        return std::make_shared<cc::FileDiffResult>(cc::diffFiles(lp, rp, options, limits));
    }));
}

void DiffView::setHeaders(const cc::FileDiffResult* r) {
    auto header = [this](const QString& side, const QString& path, const QString& info) {
        const QString name = path.isEmpty() ? tr("(not present)") : displayName_;
        return QStringLiteral("<span style='color:gray'>%1</span>&nbsp; <b>%2</b><br>"
                              "<span style='color:gray'>%3</span>")
            .arg(side.toHtmlEscaped(), name.toHtmlEscaped(), info.toHtmlEscaped());
    };
    leftHeader_->setText(header(tr("LEFT"), leftPath_, r ? sideInfo(r->left, !leftPath_.isEmpty()) : QString()));
    rightHeader_->setText(header(tr("RIGHT"), rightPath_, r ? sideInfo(r->right, !rightPath_.isEmpty()) : QString()));
    leftHeader_->setToolTip(QDir::toNativeSeparators(leftPath_));
    rightHeader_->setToolTip(QDir::toNativeSeparators(rightPath_));
}

void DiffView::render(std::shared_ptr<const cc::FileDiffResult> r) {
    result_ = r;
    current_ = -1;
    setHeaders(r.get());
    const Theme& th = Theme::current();

    // ---- banner ----
    QString note;
    if (!r->message.empty()) {
        note = qUtf8(r->message);
    } else if (r->hunkStarts.empty()) {
        const bool bothPresent = !leftPath_.isEmpty() && !rightPath_.isEmpty();
        if (bothPresent && !options_.anyContentNormalization() && r->left.size == r->right.size &&
            r->left.encoding == r->right.encoding)
            note = tr("Files are identical.");
        else if (bothPresent)
            note = tr("No line differences under the current options (files differ only in encoding, BOM, "
                      "line endings or ignored whitespace/case).");
        else
            note = tr("The file is empty.");
    }
    if (!r->hunkStarts.empty()) {
        bool eolOnly = true;
        for (const cc::DiffRow& row : r->rows) {
            if (row.kind == cc::RowKind::Equal) continue;
            if (row.kind != cc::RowKind::Changed ||
                r->left.lines[static_cast<std::size_t>(row.left)].text !=
                    r->right.lines[static_cast<std::size_t>(row.right)].text) {
                eolOnly = false;
                break;
            }
        }
        if (eolOnly)
            note = tr("Only line endings differ (%1 vs %2). Turn on Options ▸ Ignore line endings to hide this.")
                       .arg(qUtf8(r->left.eolStyle), qUtf8(r->right.eolStyle));
    }
    if (r->left.binary || r->right.binary) {
        note = tr("Binary files: %1 vs %2. Line comparison is not available for binary files.")
                   .arg(humanSize(r->left.size), humanSize(r->right.size));
    }
    if (!note_.isEmpty()) note += QLatin1Char(' ') + note_;
    if (!note.isEmpty()) {
        QPalette pal = banner_->palette();
        pal.setColor(QPalette::Window, palette().color(QPalette::AlternateBase));
        banner_->setPalette(pal);
        banner_->setText(note);
        banner_->show();
    } else {
        banner_->hide();
    }

    // ---- build both documents row by row ----
    auto* ldoc = new QTextDocument;
    auto* rdoc = new QTextDocument;
    ldoc->setUndoRedoEnabled(false);
    rdoc->setUndoRedoEnabled(false);
    ldoc->setDocumentLayout(new QPlainTextDocumentLayout(ldoc));
    rdoc->setDocumentLayout(new QPlainTextDocumentLayout(rdoc));
    QTextCursor lc(ldoc), rc(rdoc);
    lc.beginEditBlock();
    rc.beginEditBlock();

    QTextCharFormat plain;
    QTextCharFormat delInline, insInline, chgInline, eolFmt;
    delInline.setBackground(th.deletedInline);
    insInline.setBackground(th.insertedInline);
    chgInline.setBackground(th.changedInline);
    eolFmt.setForeground(th.gutterText);
    eolFmt.setFontItalic(true);

    QVector<quint8> overviewKinds;
    overviewKinds.reserve(static_cast<int>(r->rows.size()));

    auto emitLine = [&](QTextCursor& c, const cc::TextLine* line, const std::vector<cc::Span>* spans,
                        const QTextCharFormat& inlineFmt, bool showEol) {
        if (!line) return;
        if (!spans || spans->empty()) {
            c.insertText(qUtf8(line->text), plain);
        } else {
            int pos = 0;
            const std::string& s = line->text;
            for (const cc::Span& sp : *spans) {
                if (sp.begin > pos) c.insertText(qUtf8(s.substr(static_cast<std::size_t>(pos), static_cast<std::size_t>(sp.begin - pos))), plain);
                c.insertText(qUtf8(s.substr(static_cast<std::size_t>(sp.begin), static_cast<std::size_t>(sp.end - sp.begin))), inlineFmt);
                pos = sp.end;
            }
            if (pos < static_cast<int>(s.size())) c.insertText(qUtf8(s.substr(static_cast<std::size_t>(pos))), plain);
        }
        if (showEol) c.insertText(eolMarker(line->eol), eolFmt);
    };

    bool first = true;
    for (const cc::DiffRow& row : r->rows) {
        if (!first) {
            lc.insertBlock();
            rc.insertBlock();
        }
        first = false;
        const cc::TextLine* ll = row.left >= 0 ? &r->left.lines[static_cast<std::size_t>(row.left)] : nullptr;
        const cc::TextLine* rl = row.right >= 0 ? &r->right.lines[static_cast<std::size_t>(row.right)] : nullptr;

        PaneRow lk = PaneRow::Equal, rk = PaneRow::Equal;
        bool showEol = false;
        switch (row.kind) {
        case cc::RowKind::Equal: break;
        case cc::RowKind::Changed:
            lk = rk = PaneRow::Changed;
            // Same visible text but still a change: it is the line ending.
            showEol = row.leftSpans.empty() && row.rightSpans.empty() && ll && rl && ll->text == rl->text;
            break;
        case cc::RowKind::Deleted: lk = PaneRow::Deleted; rk = PaneRow::Filler; break;
        case cc::RowKind::Inserted: lk = PaneRow::Filler; rk = PaneRow::Inserted; break;
        }
        const bool changed = row.kind == cc::RowKind::Changed;
        emitLine(lc, ll, changed ? &row.leftSpans : nullptr, changed ? chgInline : delInline, showEol);
        emitLine(rc, rl, changed ? &row.rightSpans : nullptr, changed ? chgInline : insInline, showEol);
        lc.block().setUserState(DiffPane::encodeState(lk, row.left + 1));
        rc.block().setUserState(DiffPane::encodeState(rk, row.right + 1));
        overviewKinds.push_back(static_cast<quint8>(row.kind == cc::RowKind::Equal    ? PaneRow::Equal
                                                    : row.kind == cc::RowKind::Changed ? PaneRow::Changed
                                                    : row.kind == cc::RowKind::Deleted ? PaneRow::Deleted
                                                                                       : PaneRow::Inserted));
    }
    if (first) {
        // Empty diff: make sure the single empty block is not numbered.
        lc.block().setUserState(DiffPane::encodeState(PaneRow::Equal, 0));
        rc.block().setUserState(DiffPane::encodeState(PaneRow::Equal, 0));
    }
    lc.endEditBlock();
    rc.endEditBlock();

    left_->setDiffDocument(ldoc, static_cast<int>(r->left.lines.size()));
    right_->setDiffDocument(rdoc, static_cast<int>(r->right.lines.size()));
    overview_->setRows(std::move(overviewKinds));
    updateOverviewViewport();

    if (!r->hunkStarts.empty()) goToChange(0, false);
    emitStatus();
    emit navigationChanged();
}

int DiffView::changeCount() const {
    return result_ ? static_cast<int>(result_->hunkStarts.size()) : 0;
}

bool DiffView::hasNextChange() const { return changeCount() > 0 && current_ < changeCount() - 1; }
bool DiffView::hasPrevChange() const { return changeCount() > 0 && current_ > 0; }

void DiffView::nextChange() {
    if (hasNextChange()) goToChange(current_ + 1, true);
}

void DiffView::prevChange() {
    if (hasPrevChange()) goToChange(current_ - 1, true);
}

void DiffView::firstChange() {
    if (changeCount() > 0) goToChange(0, true);
}

int DiffView::changeEndRow(int index) const {
    const int start = result_->hunkStarts[static_cast<std::size_t>(index)];
    int end = start;
    const int n = static_cast<int>(result_->rows.size());
    while (end + 1 < n && result_->rows[static_cast<std::size_t>(end + 1)].kind != cc::RowKind::Equal) ++end;
    return end;
}

void DiffView::goToChange(int index, bool center) {
    if (!result_ || index < 0 || index >= changeCount()) return;
    current_ = index;
    const int start = result_->hunkStarts[static_cast<std::size_t>(index)];
    const int end = changeEndRow(index);
    left_->setCurrentBlock(start, end);
    right_->setCurrentBlock(start, end);
    QScrollBar* sb = right_->verticalScrollBar();
    const int page = sb->pageStep();
    if (center || start < sb->value() || end >= sb->value() + page) {
        sb->setValue(qMax(0, start - page / 3));
    }
    emitStatus();
    emit navigationChanged();
}

void DiffView::updateOverviewViewport() {
    QScrollBar* sb = right_->verticalScrollBar();
    overview_->setViewport(sb->value(), sb->pageStep());
}

void DiffView::emitStatus() {
    if (!result_) {
        emit statusChanged(QString());
        return;
    }
    QString s;
    if (result_->left.binary || result_->right.binary) {
        s = tr("Binary file");
    } else if (result_->hunkStarts.empty()) {
        s = tr("No line differences");
    } else {
        s = tr("+%1  -%2 lines  ·  %3 change block(s)")
                .arg(result_->linesAdded)
                .arg(result_->linesRemoved)
                .arg(result_->hunkStarts.size());
        if (current_ >= 0) s += tr("  ·  block %1 of %2").arg(current_ + 1).arg(result_->hunkStarts.size());
    }
    emit statusChanged(s);
}
