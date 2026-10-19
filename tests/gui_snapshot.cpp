// Developer tool: open the main window on two folders, wait for the comparison
// and the first file diff, then save a screenshot. Not part of the shipped app.
//   cc_gui_snapshot <left> <right> <out.png> [width height]

#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CodeCompareSnapshot"));
    QApplication::setOrganizationName(QStringLiteral("CodeCompareSnapshot"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    Theme::refresh();
    const QStringList a = QApplication::arguments();
    if (a.size() < 4) return 2;

    MainWindow w;
    w.resize(a.size() > 5 ? a[4].toInt() : 1400, a.size() > 5 ? a[5].toInt() : 820);
    w.setFolders(QDir(a[1]).absolutePath(), QDir(a[2]).absolutePath());
    w.show();
    QTimer::singleShot(100, &w, &MainWindow::startCompare);
    if (a.size() > 6) QTimer::singleShot(1200, &w, [&] { w.selectRelativePath(a[6]); });
    QTimer::singleShot(2500, &w, [&] {
        w.grab().save(a[3]);
        QApplication::quit();
    });
    return QApplication::exec();
}
