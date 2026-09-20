// 화면을 실제로 두드려 보는 스모크 테스트.
//
// 여태 화면은 "크래시 없이 뜬다"까지만 확인했다. 도구를 바꾸고 마우스를
// 눌러 유닛을 놓고 지우는 길은 손으로만 확인해 왔는데, 그 길에서 나온
// 버그가 적지 않았다(고른 것이 커서를 따라다니거나, 끌기가 끝나지 않거나).
// 여기서는 게임 자료 없이 갈 수 있는 데까지 두드린다.
//
// 자료가 필요한 것(유닛 그림·지형)은 없으면 건너뛴다 — 없다고 실패시키면
// 자료 없는 곳에서 테스트가 늘 빨개진다.

#include "chk/map_document.h"
#include "io/game_graphics.h"
#include "io/map_archive.h"
#include "ui/map_view.h"
#include "test_support.h"

#include <QApplication>
#include <QMouseEvent>
#include <QSettings>

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace splash;

namespace {

/// 그 자리를 왼쪽 단추로 눌렀다 뗀다.
void clickAt(QWidget * target, const QPointF & at)
{
    QMouseEvent press(QEvent::MouseButtonPress, at, target->mapToGlobal(at.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, at, target->mapToGlobal(at.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QApplication::sendEvent(target, &release);
}

fs::path newMapPath()
{
    fs::path dir = fs::temp_directory_path() / "splash-gui-test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / "smoke.scm";
}

} // namespace

int main(int argc, char ** argv)
{
    // 테스트가 사용자의 설정을 건드리지 않게 한다.
    QCoreApplication::setOrganizationName(QStringLiteral("SplashEditorTest"));
    QCoreApplication::setApplicationName(QStringLiteral("GuiSmoke"));
    QSettings().clear();

    QApplication app(argc, argv);

    // --- 빈 맵 하나 만들기 ---
    chk::MapDocument document;
    const fs::path path = newMapPath();
    SPLASH_CHECK(document.createNew(io::MapFormat::HybridScm, /*badlands*/ 0, 64, 64,
                                    io::MapArchive::DefaultTriggers::None));
    SPLASH_CHECK(document.isOpen());

    io::GameGraphics graphics;
    const char * install = std::getenv("SPLASH_STARCRAFT_PATH");
    const bool haveAssets = install != nullptr && graphics.load(install);

    ui::MapView view;
    view.resize(400, 300);
    view.setDocument(&document);
    if (haveAssets)
        view.setTileset(&graphics);
    view.show();

    // --- 도구 바꾸기 ---
    for (const auto tool : { ui::MapView::Tool::Select, ui::MapView::Tool::Terrain,
                             ui::MapView::Tool::PlaceUnit, ui::MapView::Tool::PlaceSprite,
                             ui::MapView::Tool::PlaceDoodad, ui::MapView::Tool::SelectTerrain,
                             ui::MapView::Tool::Location, ui::MapView::Tool::Fog })
    {
        view.setTool(tool);
        SPLASH_CHECK(view.tool() == tool);
    }

    // --- 유닛 놓기 ---
    const std::size_t before = document.units().size();
    view.setTool(ui::MapView::Tool::PlaceUnit);
    view.setPlacementUnit(/*Terran Marine*/ 0, /*owner*/ 0);
    clickAt(view.viewport(), QPointF(120, 90));
    SPLASH_CHECK_EQ(document.units().size(), before + 1);

    // --- 고르고 지우기 ---
    //
    // 놓은 자리와 유닛이 앉은 자리는 다르다 — 배치 상자에 맞춰 격자로
    // 끌어당기기 때문이다. 화면 좌표는 유닛이 앉은 자리에서 다시 구한다.
    view.setTool(ui::MapView::Tool::Select);
    {
        // 화면이 맵 왼쪽 위에 붙어 있으면 가운데로 옮길 수 없으므로,
        // 보이는 영역에서 화면 좌표를 직접 구한다.
        const auto & unit = document.units().back();
        const QRectF visible = view.visibleMapRect();
        clickAt(view.viewport(),
                QPointF(unit.x - visible.left(), unit.y - visible.top()));
    }
    SPLASH_CHECK_EQ(view.selectedUnits().size(), std::size_t(1));

    SPLASH_CHECK(view.deleteSelectedUnit());
    SPLASH_CHECK_EQ(document.units().size(), before);
    SPLASH_CHECK_EQ(view.selectedUnits().size(), std::size_t(0));

    // --- 놓았다 지운 뒤에도 저장되고 다시 열린다 ---
    view.setTool(ui::MapView::Tool::PlaceUnit);
    clickAt(view.viewport(), QPointF(150, 120));
    SPLASH_CHECK(document.saveAs(path.string()));

    chk::MapDocument reopened;
    SPLASH_CHECK(reopened.open(path.string()));
    SPLASH_CHECK_EQ(reopened.units().size(), document.units().size());

    // --- 실행 취소가 화면을 흔들지 않는지 ---
    SPLASH_CHECK(document.undo());
    view.refresh();
    SPLASH_CHECK_EQ(document.units().size(), before);

    QSettings().clear();
    return splash::test::registry().report("화면 스모크");
}
