// CodeCompare - main window: folder selection, result tree and file diff.
#pragma once

#include "Types.h"

#include <QMainWindow>
#include <QMutex>
#include <atomic>
#include <memory>

class DiffView;
class FolderTreeModel;
class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QSplitter;
class QTimer;
class QTreeView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void setFolders(const QString& left, const QString& right);
    void setLeftFolder(const QString& left);
    // Select an entry by relative path ('/' separators). Returns false if not shown.
    bool selectRelativePath(const QString& relPath);

public slots:
    void startCompare();
    void stopCompare();

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    struct ProgressState {
        QMutex mutex;
        int phase = 0;
        qulonglong done = 0, total = 0;
        QString current;
    };

    void buildUi();
    void buildActions();
    void loadSettings();
    void saveSettings();
    void browse(QComboBox* combo);
    void swapSides();
    void rememberPath(QComboBox* combo, const QString& path);
    void onCompareFinished(std::shared_ptr<cc::FolderCompareResult> res);
    void onCurrentChanged();
    void applyFilter();
    void expandDifferences();
    void selectEntry(int entryIndex);
    void gotoFile(bool forward);
    void updateActions();
    void updateProgress();
    void editExcludes();
    void optionsChanged();
    void showContextMenu(const QPoint& pos);
    void showAbout();
    cc::CompareOptions currentOptions() const;
    QString cleanPath(const QString& text) const;

    // widgets
    QComboBox* leftCombo_ = nullptr;
    QComboBox* rightCombo_ = nullptr;
    QComboBox* filterCombo_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QTreeView* tree_ = nullptr;
    FolderTreeModel* model_ = nullptr;
    DiffView* diffView_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* fileStatusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QTimer* progressTimer_ = nullptr;

    // actions
    QAction* compareAct_ = nullptr;
    QAction* stopAct_ = nullptr;
    QAction* nextDiffAct_ = nullptr;
    QAction* prevDiffAct_ = nullptr;
    QAction* nextFileAct_ = nullptr;
    QAction* prevFileAct_ = nullptr;
    QAction* ignoreCaseAct_ = nullptr;
    QAction* ignoreEolAct_ = nullptr;
    QActionGroup* wsGroup_ = nullptr;

    // state
    QString excludes_;
    bool running_ = false;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::shared_ptr<ProgressState> progress_;
    QString leftRoot_, rightRoot_;   // roots of the result being shown
    cc::CompareOptions shownOptions_;
};
