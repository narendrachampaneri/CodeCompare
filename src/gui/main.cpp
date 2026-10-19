// CodeCompare - application entry point.
//   CodeCompare.exe                      open empty window
//   CodeCompare.exe <left> <right>       compare two folders immediately

#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CodeCompare"));
    QApplication::setOrganizationName(QStringLiteral("CodeCompare"));
    QApplication::setApplicationVersion(QStringLiteral(CC_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    Theme::refresh();

    MainWindow w;
    const QStringList args = QApplication::arguments();
    if (args.size() >= 3 && QFileInfo(args.at(1)).isDir() && QFileInfo(args.at(2)).isDir()) {
        w.setFolders(QDir(args.at(1)).absolutePath(), QDir(args.at(2)).absolutePath());
        QTimer::singleShot(0, &w, &MainWindow::startCompare);
    }
    w.show();
    return QApplication::exec();
}
