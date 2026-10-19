#include "MainWindow.h"

#include "DiffView.h"
#include "FolderCompare.h"
#include "FolderTreeModel.h"
#include "IgnoreRules.h"
#include "PathUtil.h"
#include "Report.h"
#include "Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {

const char* kDefaultExcludes = ".git; .svn; .hg; .vs; .idea; __pycache__; node_modules";
constexpr int kMaxRecent = 12;

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("CodeCompare"));
    setAcceptDrops(true);
    buildActions();
    buildUi();
    loadSettings();
    updateActions();
}

MainWindow::~MainWindow() {
    if (cancel_) cancel_->store(true);
}

// ------------------------------------------------------------------ UI

void MainWindow::buildActions() {
    QStyle* st = style();

    compareAct_ = new QAction(st->standardIcon(QStyle::SP_BrowserReload), tr("&Compare"), this);
    compareAct_->setShortcut(QKeySequence(Qt::Key_F5));
    compareAct_->setToolTip(tr("Compare the two folders (F5)"));
    connect(compareAct_, &QAction::triggered, this, &MainWindow::startCompare);

    stopAct_ = new QAction(st->standardIcon(QStyle::SP_BrowserStop), tr("&Stop"), this);
    stopAct_->setShortcut(QKeySequence(Qt::Key_Escape));
    stopAct_->setToolTip(tr("Stop the running comparison (Esc)"));
    connect(stopAct_, &QAction::triggered, this, &MainWindow::stopCompare);

    prevDiffAct_ = new QAction(st->standardIcon(QStyle::SP_ArrowUp), tr("Previous change"), this);
    prevDiffAct_->setShortcuts({QKeySequence(Qt::SHIFT | Qt::Key_F7), QKeySequence(Qt::ALT | Qt::Key_Up)});
    prevDiffAct_->setToolTip(tr("Previous change in this file (Shift+F7)"));

    nextDiffAct_ = new QAction(st->standardIcon(QStyle::SP_ArrowDown), tr("Next change"), this);
    nextDiffAct_->setShortcuts({QKeySequence(Qt::Key_F7), QKeySequence(Qt::ALT | Qt::Key_Down)});
    nextDiffAct_->setToolTip(tr("Next change in this file (F7)"));

    prevFileAct_ = new QAction(st->standardIcon(QStyle::SP_MediaSeekBackward), tr("Previous file"), this);
    prevFileAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
    prevFileAct_->setToolTip(tr("Previous different file (Shift+F8)"));
    connect(prevFileAct_, &QAction::triggered, this, [this] { gotoFile(false); });

    nextFileAct_ = new QAction(st->standardIcon(QStyle::SP_MediaSeekForward), tr("Next file"), this);
    nextFileAct_->setShortcut(QKeySequence(Qt::Key_F8));
    nextFileAct_->setToolTip(tr("Next different file (F8)"));
    connect(nextFileAct_, &QAction::triggered, this, [this] { gotoFile(true); });

    wsGroup_ = new QActionGroup(this);
    const QStringList wsNames = {tr("Compare all whitespace"), tr("Ignore trailing whitespace"),
                                 tr("Ignore changes in amount of whitespace"), tr("Ignore all whitespace")};
    for (int i = 0; i < wsNames.size(); ++i) {
        QAction* a = wsGroup_->addAction(wsNames[i]);
        a->setCheckable(true);
        a->setData(i);
        if (i == 0) a->setChecked(true);
    }
    connect(wsGroup_, &QActionGroup::triggered, this, &MainWindow::optionsChanged);

    ignoreCaseAct_ = new QAction(tr("Ignore case"), this);
    ignoreCaseAct_->setCheckable(true);
    connect(ignoreCaseAct_, &QAction::toggled, this, &MainWindow::optionsChanged);

    ignoreEolAct_ = new QAction(tr("Ignore line endings (CRLF / LF)"), this);
    ignoreEolAct_->setCheckable(true);
    connect(ignoreEolAct_, &QAction::toggled, this, &MainWindow::optionsChanged);
}

void MainWindow::buildUi() {
    // ---- folder selection bar ----
    auto* top = new QWidget(this);
    auto* grid = new QGridLayout(top);
    grid->setContentsMargins(10, 8, 10, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    auto makeCombo = [this](const QString& placeholder) {
        auto* c = new QComboBox(this);
        c->setEditable(true);
        c->setInsertPolicy(QComboBox::NoInsert);
        c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        c->setMinimumContentsLength(30);
        c->lineEdit()->setPlaceholderText(placeholder);
        c->lineEdit()->setClearButtonEnabled(true);
        auto* fsModel = new QFileSystemModel(c);
        fsModel->setFilter(QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot);
        fsModel->setRootPath(QString());
        auto* completer = new QCompleter(fsModel, c);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        c->setCompleter(completer);
        connect(c->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::startCompare);
        return c;
    };
    leftCombo_ = makeCombo(tr("Left (original) folder - type, paste, browse or drop a folder here"));
    rightCombo_ = makeCombo(tr("Right (modified) folder"));

    auto* leftBrowse = new QPushButton(tr("Browse..."), this);
    auto* rightBrowse = new QPushButton(tr("Browse..."), this);
    connect(leftBrowse, &QPushButton::clicked, this, [this] { browse(leftCombo_); });
    connect(rightBrowse, &QPushButton::clicked, this, [this] { browse(rightCombo_); });

    auto* swapBtn = new QToolButton(this);
    swapBtn->setText(QStringLiteral("⇅"));
    swapBtn->setToolTip(tr("Swap left and right"));
    swapBtn->setMinimumSize(32, 32);
    connect(swapBtn, &QToolButton::clicked, this, &MainWindow::swapSides);

    auto* compareBtn = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), tr("Compare"), this);
    compareBtn->setDefault(true);
    compareBtn->setMinimumHeight(56);
    compareBtn->setMinimumWidth(110);
    QFont bf = compareBtn->font();
    bf.setBold(true);
    compareBtn->setFont(bf);
    connect(compareBtn, &QPushButton::clicked, this, &MainWindow::startCompare);

    auto* leftLbl = new QLabel(tr("Left:"), this);
    auto* rightLbl = new QLabel(tr("Right:"), this);
    grid->addWidget(leftLbl, 0, 0);
    grid->addWidget(leftCombo_, 0, 1);
    grid->addWidget(leftBrowse, 0, 2);
    grid->addWidget(rightLbl, 1, 0);
    grid->addWidget(rightCombo_, 1, 1);
    grid->addWidget(rightBrowse, 1, 2);
    grid->addWidget(swapBtn, 0, 3, 2, 1);
    grid->addWidget(compareBtn, 0, 4, 2, 1);
    grid->setColumnStretch(1, 1);

    // ---- toolbar ----
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("mainToolbar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setIconSize(QSize(16, 16));
    tb->addAction(compareAct_);
    tb->addAction(stopAct_);
    tb->addSeparator();
    tb->addAction(prevDiffAct_);
    tb->addAction(nextDiffAct_);
    tb->addSeparator();
    tb->addAction(prevFileAct_);
    tb->addAction(nextFileAct_);
    tb->addSeparator();

    tb->addWidget(new QLabel(tr(" Show: "), this));
    filterCombo_ = new QComboBox(this);
    filterCombo_->addItem(tr("All files"), static_cast<int>(FolderTreeModel::Filter::All));
    filterCombo_->addItem(tr("Differences only"), static_cast<int>(FolderTreeModel::Filter::Differences));
    filterCombo_->addItem(tr("Modified only"), static_cast<int>(FolderTreeModel::Filter::Modified));
    filterCombo_->addItem(tr("Only in left (deleted)"), static_cast<int>(FolderTreeModel::Filter::OnlyLeft));
    filterCombo_->addItem(tr("Only in right (added)"), static_cast<int>(FolderTreeModel::Filter::OnlyRight));
    filterCombo_->setCurrentIndex(1);
    connect(filterCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::applyFilter);
    tb->addWidget(filterCombo_);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Filter by name"));
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setMaximumWidth(220);
    auto* searchDelay = new QTimer(this);
    searchDelay->setSingleShot(true);
    searchDelay->setInterval(250);
    connect(searchEdit_, &QLineEdit::textChanged, searchDelay, qOverload<>(&QTimer::start));
    connect(searchDelay, &QTimer::timeout, this, &MainWindow::applyFilter);
    tb->addWidget(new QLabel(QStringLiteral("  "), this));
    tb->addWidget(searchEdit_);
    tb->addSeparator();

    auto* optMenu = new QMenu(this);
    optMenu->addSection(tr("Whitespace"));
    optMenu->addActions(wsGroup_->actions());
    optMenu->addSection(tr("Text"));
    optMenu->addAction(ignoreCaseAct_);
    optMenu->addAction(ignoreEolAct_);
    optMenu->addSection(tr("Folders"));
    optMenu->addAction(tr("Exclude patterns..."), this, &MainWindow::editExcludes);
    auto* optBtn = new QToolButton(this);
    optBtn->setText(tr("Options"));
    optBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    optBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    optBtn->setPopupMode(QToolButton::InstantPopup);
    optBtn->setMenu(optMenu);
    tb->addWidget(optBtn);

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    QAction* aboutAct = tb->addAction(style()->standardIcon(QStyle::SP_MessageBoxInformation), tr("About"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);

    // ---- tree + diff ----
    model_ = new FolderTreeModel(this);
    tree_ = new QTreeView(this);
    tree_->setModel(model_);
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(FolderTreeModel::ColName, QHeaderView::Interactive);
    tree_->setColumnWidth(FolderTreeModel::ColName, 260);
    tree_->setColumnWidth(FolderTreeModel::ColStatus, 130);
    tree_->setColumnWidth(FolderTreeModel::ColLeftSize, 80);
    tree_->setColumnWidth(FolderTreeModel::ColRightSize, 80);
    tree_->setColumnWidth(FolderTreeModel::ColLeftDate, 120);
    tree_->setColumnWidth(FolderTreeModel::ColRightDate, 120);
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onCurrentChanged);
    connect(tree_, &QTreeView::customContextMenuRequested, this, &MainWindow::showContextMenu);

    diffView_ = new DiffView(this);
    connect(diffView_, &DiffView::statusChanged, this, [this](const QString& s) { fileStatusLabel_->setText(s); });
    connect(diffView_, &DiffView::navigationChanged, this, &MainWindow::updateActions);
    connect(prevDiffAct_, &QAction::triggered, diffView_, &DiffView::prevChange);
    connect(nextDiffAct_, &QAction::triggered, diffView_, &DiffView::nextChange);

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->addWidget(tree_);
    splitter_->addWidget(diffView_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 3);
    splitter_->setChildrenCollapsible(false);
    splitter_->setSizes({380, 1000});

    auto* central = new QWidget(this);
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(top);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    v->addWidget(line);
    v->addWidget(splitter_, 1);
    setCentralWidget(central);

    // ---- status bar ----
    summaryLabel_ = new QLabel(tr("Choose two folders and press Compare."), this);
    fileStatusLabel_ = new QLabel(this);
    progressBar_ = new QProgressBar(this);
    progressBar_->setMaximumWidth(220);
    progressBar_->setTextVisible(true);
    progressBar_->hide();
    statusBar()->addWidget(summaryLabel_, 1);
    statusBar()->addPermanentWidget(fileStatusLabel_);
    statusBar()->addPermanentWidget(progressBar_);

    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout, this, &MainWindow::updateProgress);

    resize(1400, 860);
}

// ------------------------------------------------------------------ settings

void MainWindow::loadSettings() {
    QSettings s;
    const QStringList lefts = s.value(QStringLiteral("recent/left")).toStringList();
    const QStringList rights = s.value(QStringLiteral("recent/right")).toStringList();
    leftCombo_->addItems(lefts);
    rightCombo_->addItems(rights);
    leftCombo_->setCurrentIndex(lefts.isEmpty() ? -1 : 0);
    rightCombo_->setCurrentIndex(rights.isEmpty() ? -1 : 0);

    excludes_ = s.value(QStringLiteral("options/excludes"), QString::fromLatin1(kDefaultExcludes)).toString();
    const int ws = s.value(QStringLiteral("options/whitespace"), 0).toInt();
    for (QAction* a : wsGroup_->actions()) a->setChecked(a->data().toInt() == ws);
    {
        const QSignalBlocker b1(ignoreCaseAct_), b2(ignoreEolAct_);
        ignoreCaseAct_->setChecked(s.value(QStringLiteral("options/ignoreCase"), false).toBool());
        ignoreEolAct_->setChecked(s.value(QStringLiteral("options/ignoreEol"), false).toBool());
    }
    {
        const QSignalBlocker b(filterCombo_);
        filterCombo_->setCurrentIndex(qBound(0, s.value(QStringLiteral("view/filter"), 1).toInt(), filterCombo_->count() - 1));
    }
    if (s.contains(QStringLiteral("window/geometry"))) restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    if (s.contains(QStringLiteral("window/splitter"))) splitter_->restoreState(s.value(QStringLiteral("window/splitter")).toByteArray());
    if (s.contains(QStringLiteral("window/treeHeader"))) tree_->header()->restoreState(s.value(QStringLiteral("window/treeHeader")).toByteArray());
}

void MainWindow::saveSettings() {
    QSettings s;
    auto items = [](QComboBox* c) {
        QStringList l;
        for (int i = 0; i < c->count() && i < kMaxRecent; ++i) l << c->itemText(i);
        return l;
    };
    s.setValue(QStringLiteral("recent/left"), items(leftCombo_));
    s.setValue(QStringLiteral("recent/right"), items(rightCombo_));
    s.setValue(QStringLiteral("options/excludes"), excludes_);
    s.setValue(QStringLiteral("options/whitespace"), wsGroup_->checkedAction() ? wsGroup_->checkedAction()->data().toInt() : 0);
    s.setValue(QStringLiteral("options/ignoreCase"), ignoreCaseAct_->isChecked());
    s.setValue(QStringLiteral("options/ignoreEol"), ignoreEolAct_->isChecked());
    s.setValue(QStringLiteral("view/filter"), filterCombo_->currentIndex());
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/splitter"), splitter_->saveState());
    s.setValue(QStringLiteral("window/treeHeader"), tree_->header()->saveState());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (cancel_) cancel_->store(true);
    saveSettings();
    e->accept();
}

// ------------------------------------------------------------------ folder input

QString MainWindow::cleanPath(const QString& text) const {
    QString p = text.trimmed();
    if (p.size() >= 2 && p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"'))) p = p.mid(1, p.size() - 2);
    if (p.isEmpty()) return p;
    return QDir::cleanPath(QDir::fromNativeSeparators(p));
}

void MainWindow::setFolders(const QString& left, const QString& right) {
    leftCombo_->setCurrentText(QDir::toNativeSeparators(left));
    rightCombo_->setCurrentText(QDir::toNativeSeparators(right));
}

void MainWindow::setLeftFolder(const QString& left) {
    leftCombo_->setCurrentText(QDir::toNativeSeparators(left));
    rightCombo_->setCurrentText(QString());
    rightCombo_->setFocus();
}

void MainWindow::browse(QComboBox* combo) {
    const QString start = cleanPath(combo->currentText());
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select folder"), start);
    if (!dir.isEmpty()) combo->setCurrentText(QDir::toNativeSeparators(dir));
}

void MainWindow::swapSides() {
    const QString l = leftCombo_->currentText();
    leftCombo_->setCurrentText(rightCombo_->currentText());
    rightCombo_->setCurrentText(l);
    if (model_->result()) startCompare();
}

void MainWindow::rememberPath(QComboBox* combo, const QString& path) {
    const QString native = QDir::toNativeSeparators(path);
    const int existing = combo->findText(native, Qt::MatchFixedString);
    if (existing >= 0) combo->removeItem(existing);
    combo->insertItem(0, native);
    while (combo->count() > kMaxRecent) combo->removeItem(combo->count() - 1);
    combo->setCurrentIndex(0);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList dirs;
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString p = u.toLocalFile();
        if (!p.isEmpty() && QFileInfo(p).isDir()) dirs << p;
    }
    if (dirs.isEmpty()) return;
    if (dirs.size() >= 2) {
        setFolders(dirs[0], dirs[1]);
    } else {
        // Drop onto whichever path box is closer to the cursor.
        const QPoint pos = e->position().toPoint();
        const QPoint rc = rightCombo_->mapTo(this, rightCombo_->rect().center());
        const QPoint lc = leftCombo_->mapTo(this, leftCombo_->rect().center());
        QComboBox* target = (pos - rc).manhattanLength() < (pos - lc).manhattanLength() ? rightCombo_ : leftCombo_;
        target->setCurrentText(QDir::toNativeSeparators(dirs[0]));
    }
    e->acceptProposedAction();
}

// ------------------------------------------------------------------ options

cc::CompareOptions MainWindow::currentOptions() const {
    cc::CompareOptions o;
    const int ws = wsGroup_->checkedAction() ? wsGroup_->checkedAction()->data().toInt() : 0;
    o.whitespace = static_cast<cc::WhitespaceMode>(ws);
    o.ignoreCase = ignoreCaseAct_->isChecked();
    o.ignoreLineEndings = ignoreEolAct_->isChecked();
    o.excludePatterns = cc::IgnoreRules::parseList(excludes_.toStdString());
    return o;
}

void MainWindow::optionsChanged() {
    if (model_->result() && !running_) startCompare();
}

void MainWindow::editExcludes() {
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, tr("Exclude patterns"),
        tr("Files and folders matching these patterns are skipped (one per line or ';'-separated).\n"
           "Examples:  .git    build/    *.o    Debug/    /out    src/*.tmp    docs/**/*.pdf"),
        excludes_.split(QLatin1Char(';'), Qt::SkipEmptyParts).replaceInStrings(QStringLiteral(" "), QString()).join(QLatin1Char('\n')),
        &ok);
    if (!ok) return;
    QStringList parts;
    for (const std::string& p : cc::IgnoreRules::parseList(text.toStdString())) parts << QString::fromStdString(p);
    excludes_ = parts.join(QStringLiteral("; "));
    optionsChanged();
}

// ------------------------------------------------------------------ compare

void MainWindow::startCompare() {
    if (running_) return;
    const QString left = cleanPath(leftCombo_->currentText());
    const QString right = cleanPath(rightCombo_->currentText());
    if (left.isEmpty() || right.isEmpty()) {
        QMessageBox::information(this, tr("CodeCompare"), tr("Please choose both a left and a right folder."));
        return;
    }
    for (const QString& p : {left, right}) {
        if (!QFileInfo(p).isDir()) {
            QMessageBox::warning(this, tr("CodeCompare"),
                                 tr("This folder does not exist or cannot be opened:\n%1").arg(QDir::toNativeSeparators(p)));
            return;
        }
    }
    rememberPath(leftCombo_, left);
    rememberPath(rightCombo_, right);

    running_ = true;
    leftRoot_ = left;
    rightRoot_ = right;
    shownOptions_ = currentOptions();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    progress_ = std::make_shared<ProgressState>();

    diffView_->clear();
    model_->clear();
    summaryLabel_->setText(tr("Scanning..."));
    fileStatusLabel_->clear();
    progressBar_->setRange(0, 0);
    progressBar_->show();
    progressTimer_->start();
    updateActions();
    setWindowTitle(QStringLiteral("%1  ⇄  %2 - CodeCompare")
                       .arg(QFileInfo(left).fileName(), QFileInfo(right).fileName()));

    const std::filesystem::path lp = toFsPath(left);
    const std::filesystem::path rp = toFsPath(right);
    const cc::CompareOptions opt = shownOptions_;
    auto cancel = cancel_;
    auto prog = progress_;

    auto* watcher = new QFutureWatcher<std::shared_ptr<cc::FolderCompareResult>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        watcher->deleteLater();
        onCompareFinished(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([lp, rp, opt, cancel, prog]() {
        auto onProgress = [prog](const cc::Progress& p) {
            QMutexLocker lock(&prog->mutex);
            prog->phase = static_cast<int>(p.phase);
            prog->done = p.done;
            prog->total = p.total;
            prog->current = qUtf8(p.current);
        };
        return std::make_shared<cc::FolderCompareResult>(cc::compareFolders(lp, rp, opt, cancel.get(), onProgress));
    }));
}

void MainWindow::stopCompare() {
    if (running_ && cancel_) {
        cancel_->store(true);
        summaryLabel_->setText(tr("Stopping..."));
    }
}

void MainWindow::updateProgress() {
    if (!progress_) return;
    int phase;
    qulonglong done, total;
    QString current;
    {
        QMutexLocker lock(&progress_->mutex);
        phase = progress_->phase;
        done = progress_->done;
        total = progress_->total;
        current = progress_->current;
    }
    if (phase == static_cast<int>(cc::Progress::Phase::Scanning)) {
        progressBar_->setRange(0, 0);
        summaryLabel_->setText(tr("Scanning... %1 items  %2").arg(done).arg(current));
    } else if (total > 0) {
        progressBar_->setRange(0, 1000);
        progressBar_->setValue(static_cast<int>(done * 1000 / total));
        progressBar_->setFormat(QStringLiteral("%1 / %2").arg(done).arg(total));
        summaryLabel_->setText(tr("Comparing  %1").arg(current));
    }
}

void MainWindow::onCompareFinished(std::shared_ptr<cc::FolderCompareResult> res) {
    running_ = false;
    progressTimer_->stop();
    progressBar_->hide();
    updateActions();

    if (!res || res->cancelled) {
        summaryLabel_->setText(tr("Comparison stopped."));
        return;
    }
    if (!res->fatalError.empty()) {
        summaryLabel_->setText(tr("Comparison failed."));
        QMessageBox::warning(this, tr("CodeCompare"), qUtf8(res->fatalError));
        return;
    }
    const cc::CompareSummary& s = res->summary;
    model_->setResult(res);
    applyFilter();

    QStringList parts;
    parts << tr("%1 files").arg(s.files);
    if (s.modified) parts << tr("%1 modified").arg(s.modified);
    if (s.onlyRight) parts << tr("%1 added").arg(s.onlyRight);
    if (s.onlyLeft) parts << tr("%1 deleted").arg(s.onlyLeft);
    parts << tr("%1 identical").arg(s.identical);
    if (s.equivalent) parts << tr("%1 same after ignore options").arg(s.equivalent);
    if (s.typeMismatch) parts << tr("%1 file/folder conflicts").arg(s.typeMismatch);
    if (s.errors) parts << tr("%1 errors").arg(s.errors);
    QString text = parts.join(QStringLiteral("  ·  "));
    text += tr("   (%1 s)").arg(s.seconds, 0, 'f', 2);
    if (s.modified + s.onlyLeft + s.onlyRight + s.typeMismatch + s.errors == 0)
        text = tr("No differences found.  ") + text;
    summaryLabel_->setText(text);

    // Jump straight to the first different file.
    gotoFile(true);
    if (!tree_->currentIndex().isValid() && model_->rowCount() > 0)
        tree_->setCurrentIndex(model_->index(0, 0));
}

// ------------------------------------------------------------------ tree

void MainWindow::applyFilter() {
    const int prevEntry = model_->entryIndex(tree_->currentIndex());
    const auto f = static_cast<FolderTreeModel::Filter>(filterCombo_->currentData().toInt());
    model_->setFilter(f, searchEdit_->text().trimmed());
    expandDifferences();
    if (prevEntry >= 0) {
        const QModelIndex idx = model_->indexForEntry(prevEntry);
        if (idx.isValid()) {
            const QSignalBlocker block(tree_->selectionModel());
            tree_->setCurrentIndex(idx);
            tree_->scrollTo(idx);
        }
    }
    updateActions();
}

void MainWindow::expandDifferences() {
    const auto res = model_->result();
    if (!res) return;
    const bool expandAll = model_->filter() != FolderTreeModel::Filter::All || !searchEdit_->text().trimmed().isEmpty();
    if (expandAll) {
        tree_->expandAll();
        return;
    }
    // In "All files" view, open only folders that contain changes.
    for (int i = 0; i < static_cast<int>(res->entries.size()); ++i) {
        const cc::Entry& e = res->entries[static_cast<std::size_t>(i)];
        if (e.isDir && e.status == cc::EntryStatus::Modified) {
            const QModelIndex idx = model_->indexForEntry(i);
            if (idx.isValid()) tree_->expand(idx);
        }
    }
}

void MainWindow::selectEntry(int entryIndex) {
    const QModelIndex idx = model_->indexForEntry(entryIndex);
    if (!idx.isValid()) return;
    for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent()) tree_->expand(p);
    tree_->setCurrentIndex(idx);
    tree_->scrollTo(idx);
}

bool MainWindow::selectRelativePath(const QString& relPath) {
    const auto res = model_->result();
    if (!res) return false;
    const std::string want = relPath.toStdString();
    for (int i = 0; i < static_cast<int>(res->entries.size()); ++i) {
        if (res->entries[static_cast<std::size_t>(i)].displayPath == want && model_->isEntryVisible(i)) {
            selectEntry(i);
            return true;
        }
    }
    return false;
}

void MainWindow::gotoFile(bool forward) {
    const auto res = model_->result();
    if (!res) return;
    const int n = static_cast<int>(res->entries.size());
    int cur = model_->entryIndex(tree_->currentIndex());
    if (cur < 0) cur = forward ? -1 : n;
    for (int i = forward ? cur + 1 : cur - 1; i >= 0 && i < n; i += forward ? 1 : -1) {
        const cc::Entry& e = res->entries[static_cast<std::size_t>(i)];
        if (!e.isDir && cc::isDifferent(e.status) && model_->isEntryVisible(i)) {
            selectEntry(i);
            return;
        }
    }
}

void MainWindow::onCurrentChanged() {
    updateActions();
    const auto res = model_->result();
    const int idx = model_->entryIndex(tree_->currentIndex());
    if (!res || idx < 0) {
        diffView_->clear();
        return;
    }
    const cc::Entry& e = res->entries[static_cast<std::size_t>(idx)];
    const QString rel = qUtf8(e.displayPath);
    const QString name = QDir::toNativeSeparators(rel);

    if (e.status == cc::EntryStatus::TypeMismatch) {
        diffView_->showMessage(name, tr("This is a file on one side and a folder on the other side."));
        return;
    }
    if (e.isDir) {
        int changed = 0, total = 0;
        const std::string prefix = e.key + "/";
        for (const cc::Entry& c : res->entries) {
            if (c.isDir || c.key.compare(0, prefix.size(), prefix) != 0) continue;
            ++total;
            if (cc::isDifferent(c.status)) ++changed;
        }
        QString msg;
        if (e.status == cc::EntryStatus::OnlyInLeft) msg = tr("This folder exists only on the left side (deleted).");
        else if (e.status == cc::EntryStatus::OnlyInRight) msg = tr("This folder exists only on the right side (added).");
        else msg = tr("Folder: %1 of %2 files inside are different.").arg(changed).arg(total);
        if (!e.error.empty()) msg += QStringLiteral("\n") + qUtf8(e.error);
        diffView_->showMessage(name, msg);
        return;
    }
    if (e.status == cc::EntryStatus::Error) {
        diffView_->showMessage(name, tr("Cannot compare this file: %1").arg(qUtf8(e.error)));
        return;
    }
    const QString lp = e.inLeft ? leftRoot_ + QLatin1Char('/') + rel : QString();
    const QString rp = e.inRight ? rightRoot_ + QLatin1Char('/') + rel : QString();
    QString note;
    if (e.binary && e.firstDiffOffset >= 0)
        note = tr("First difference at byte offset %1 (0x%2).")
                   .arg(e.firstDiffOffset)
                   .arg(QString::number(e.firstDiffOffset, 16).toUpper());
    diffView_->showFiles(lp, rp, name, shownOptions_, note);
}

void MainWindow::showContextMenu(const QPoint& pos) {
    const QModelIndex index = tree_->indexAt(pos);
    const auto res = model_->result();
    const int idx = model_->entryIndex(index);
    if (!res || idx < 0) return;
    const cc::Entry& e = res->entries[static_cast<std::size_t>(idx)];
    const QString rel = qUtf8(e.displayPath);
    const QString lp = leftRoot_ + QLatin1Char('/') + rel;
    const QString rp = rightRoot_ + QLatin1Char('/') + rel;

    QMenu menu(this);
    QAction* openL = menu.addAction(tr("Open left"));
    QAction* openR = menu.addAction(tr("Open right"));
    openL->setEnabled(e.inLeft);
    openR->setEnabled(e.inRight);
    menu.addSeparator();
    QAction* folderL = menu.addAction(tr("Open containing folder (left)"));
    QAction* folderR = menu.addAction(tr("Open containing folder (right)"));
    folderL->setEnabled(e.inLeft);
    folderR->setEnabled(e.inRight);
    menu.addSeparator();
    QAction* copyRel = menu.addAction(tr("Copy relative path"));
    QAction* copyL = menu.addAction(tr("Copy left full path"));
    QAction* copyR = menu.addAction(tr("Copy right full path"));
    copyL->setEnabled(e.inLeft);
    copyR->setEnabled(e.inRight);

    QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == openL) QDesktopServices::openUrl(QUrl::fromLocalFile(lp));
    else if (chosen == openR) QDesktopServices::openUrl(QUrl::fromLocalFile(rp));
    else if (chosen == folderL) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(lp).absolutePath()));
    else if (chosen == folderR) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(rp).absolutePath()));
    else if (chosen == copyRel) QApplication::clipboard()->setText(QDir::toNativeSeparators(rel));
    else if (chosen == copyL) QApplication::clipboard()->setText(QDir::toNativeSeparators(lp));
    else if (chosen == copyR) QApplication::clipboard()->setText(QDir::toNativeSeparators(rp));
}

void MainWindow::updateActions() {
    compareAct_->setEnabled(!running_);
    stopAct_->setEnabled(running_);
    nextDiffAct_->setEnabled(diffView_ && diffView_->hasNextChange());
    prevDiffAct_->setEnabled(diffView_ && diffView_->hasPrevChange());
    const bool haveResult = model_ && model_->result() && !running_;
    nextFileAct_->setEnabled(haveResult);
    prevFileAct_->setEnabled(haveResult);
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About CodeCompare"),
        tr("<h3>CodeCompare %1</h3>"
           "<p>Offline folder and line-by-line code comparison.</p>"
           "<p>Files with equal size are compared byte by byte; text files are diffed with "
           "the Myers algorithm (the same family of algorithm git uses), with word-level "
           "highlighting inside changed lines.</p>"
           "<p><b>Shortcuts</b><br>F5 compare &middot; Esc stop<br>"
           "F7 / Shift+F7 next / previous change<br>F8 / Shift+F8 next / previous file</p>"
           "<p>Built with Qt %2.</p>")
            .arg(QStringLiteral(CC_VERSION), QString::fromLatin1(qVersion())));
}
