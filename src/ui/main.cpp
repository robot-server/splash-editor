// Splash Editor GUI 진입점.

#include "ui/main_window.h"

#include <QApplication>

int main(int argc, char ** argv)
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Splash Editor"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SPLASH_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Splash Editor"));

    splash::ui::MainWindow window;
    window.show();

    // 커맨드라인으로 맵 경로를 하나 받을 수 있다.
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1)
        window.openPath(args.at(1));

    return app.exec();
}
