// Splash Editor GUI 진입점.

#include "ui/main_window.h"

#include <QApplication>

int main(int argc, char ** argv)
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Splash Editor"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SPLASH_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Splash Editor"));

    // 사용법: splash-editor [맵파일] [--install <StarCraft 설치폴더>]
    //
    // --install 은 지정한 폴더를 타일셋 소스로 쓰고 기억한다. 메뉴에서
    // 고르는 것과 같은 일을 하며, 스크립트나 첫 실행에 편하다.
    QString mapPath;
    QString installPath;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i)
    {
        const QString & arg = args.at(i);
        if (arg == QStringLiteral("--install") && i + 1 < args.size())
            installPath = args.at(++i);
        else if (mapPath.isEmpty() && !arg.startsWith(QStringLiteral("--")))
            mapPath = arg;
    }

    splash::ui::MainWindow window;
    window.show();

    if (!installPath.isEmpty())
        window.useInstallPath(installPath);

    if (!mapPath.isEmpty())
        window.openPath(mapPath);

    return app.exec();
}
