// CodeCompare - side-by-side, line-by-line file comparison view.
#pragma once

#include "Types.h"

#include <QFutureWatcher>
#include <QWidget>
#include <atomic>
#include <memory>

class DiffPane;
class OverviewBar;
class QLabel;

class DiffView : public QWidget {
    Q_OBJECT
public:
    explicit DiffView(QWidget* parent = nullptr);
    ~DiffView() override;

    // Compare two files in the background. An empty path means "file absent on that side".
    void showFiles(const QString& leftPath, const QString& rightPath, const QString& displayName,
                   const cc::CompareOptions& options, const QString& note = QString());
    // Show only a message (e.g. for folders), clearing the text panes.
    void showMessage(const QString& title, const QString& message);
    void clear();

    int changeCount() const;
    int currentChange() const { return current_; }
    bool hasNextChange() const;
    bool hasPrevChange() const;

public slots:
    void nextChange();
    void prevChange();
    void firstChange();

signals:
    void statusChanged(const QString& text);
    void navigationChanged();

private:
    void render(std::shared_ptr<const cc::FileDiffResult> r);
    void goToChange(int index, bool center);
    void setHeaders(const cc::FileDiffResult* r);
    void updateOverviewViewport();
    void emitStatus();
    int changeEndRow(int index) const;

    QLabel* leftHeader_;
    QLabel* rightHeader_;
    QLabel* banner_;
    DiffPane* left_;
    DiffPane* right_;
    OverviewBar* overview_;

    std::shared_ptr<std::atomic<bool>> cancel_;
    quint64 generation_ = 0;

    std::shared_ptr<const cc::FileDiffResult> result_;
    QString leftPath_, rightPath_, displayName_, note_;
    cc::CompareOptions options_;
    int current_ = -1;
};
