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
#include "io/eud.h"
#include "io/game_graphics.h"
#include "io/map_archive.h"
#include "ui/main_window.h"
#include "ui/map_view.h"
#include "test_support.h"

#include <QApplication>
#include <QMouseEvent>
#include <QAction>
#include <QSettings>
#include <QPointer>
#include <QTimer>

#include <cstdlib>
#include <filesystem>
#include <iostream>
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

    // --- SCMDraft 문법으로 적은 트리거를 그대로 읽는지 ---
    //
    // 글줄은 SCMDraft 2 가 함께 주는 "Text Trigedit Syntax Help" 에 적힌
    // 형태 그대로다. 우리가 쓴 글을 우리가 다시 읽을 수 있는지도 본다.
    if (haveAssets)
    {
        const std::string script =
            "Trigger(\"Player 1\"){\n"
            "Conditions:\n"
            "\tAlways();\n"
            "\tDeaths(\"Player 1\", \"Terran Marine\", At least, 1);\n"
            "\tAccumulate(\"Player 1\", At least, 500, ore);\n"
            "Actions:\n"
            "\tCreate Unit(\"Player 1\", \"Terran Marine\", 1, \"Anywhere\");\n"
            "\tSet Resources(\"Player 1\", Add, 100, ore and gas);\n"
            "\tDisplay Text Message(Always Display, \"hi\");\n"
            "\tPreserve Trigger();\n"
            "}\n";

        chk::MapDocument scripted;
        SPLASH_CHECK(scripted.createNew(io::MapFormat::HybridScm, 0, 64, 64,
                                        io::MapArchive::DefaultTriggers::None));
        SPLASH_CHECK(scripted.applyTriggerText(script, graphics));
        SPLASH_CHECK_EQ(scripted.info().triggerCount, std::size_t(1));

        const auto written = scripted.triggerText(graphics);
        SPLASH_CHECK(written.has_value());
        if (written)
        {
            SPLASH_CHECK(written->find("Deaths(\"Player 1\", \"Terran Marine\", "
                                       "At least, 1)") != std::string::npos);
            SPLASH_CHECK(scripted.applyTriggerText(*written, graphics));
            SPLASH_CHECK_EQ(scripted.info().triggerCount, std::size_t(1));
        }

        // 인자에 쓰는 낱말도 그 문서의 Reference List 그대로여야 한다.
        // 하나라도 다르면 남이 만든 글을 못 읽는다.
        const char * const vocabulary[] = {
            // 자원 / 고치는 방식
            "Trigger(\"Player 1\"){\nConditions:\n"
            "\tAccumulate(\"Player 1\", At least, 1, ore);\n"
            "\tAccumulate(\"Player 1\", At most, 1, gas);\n"
            "\tAccumulate(\"Player 1\", Exactly, 1, ore and gas);\nActions:\n"
            "\tSet Resources(\"Player 1\", Set To, 1, ore);\n"
            "\tSet Resources(\"Player 1\", Add, 1, gas);\n"
            "\tSet Resources(\"Player 1\", Subtract, 1, ore and gas);\n}\n",

            // 상태 / 움직임
            "Trigger(\"Player 1\"){\nConditions:\n\tAlways();\nActions:\n"
            "\tSet Doodad State(\"Player 1\", \"Terran Marine\", \"Anywhere\", enabled);\n"
            "\tSet Doodad State(\"Player 1\", \"Terran Marine\", \"Anywhere\", disabled);\n"
            "\tSet Doodad State(\"Player 1\", \"Terran Marine\", \"Anywhere\", toggle);\n"
            "\tOrder(\"Player 1\", \"Terran Marine\", \"Anywhere\", \"Anywhere\", move);\n"
            "\tOrder(\"Player 1\", \"Terran Marine\", \"Anywhere\", \"Anywhere\", patrol);\n"
            "\tOrder(\"Player 1\", \"Terran Marine\", \"Anywhere\", \"Anywhere\", attack);\n}\n",

            // 점수 / 동맹 / 유닛 묶음 이름
            "Trigger(\"Player 1\"){\nConditions:\n"
            "\tScore(\"Player 1\", Total, At least, 1);\n"
            "\tScore(\"Player 1\", Kills, At least, 1);\n"
            "\tScore(\"Player 1\", Razings, At least, 1);\n"
            "\tScore(\"Player 1\", Custom, At least, 1);\n"
            "\tBring(\"Player 1\", \"Any unit\", \"Anywhere\", At least, 1);\n"
            "\tBring(\"Player 1\", \"Men\", \"Anywhere\", At least, 1);\n"
            "\tBring(\"Player 1\", \"Factories\", \"Anywhere\", At least, 1);\nActions:\n"
            "\tSet Alliance Status(\"Player 2\", Enemy);\n"
            "\tSet Alliance Status(\"Player 2\", Ally);\n"
            "\tSet Alliance Status(\"Player 2\", Allied Victory);\n"
            "\tKill Unit At Location(\"Player 1\", \"Terran Marine\", All, \"Anywhere\");\n}\n",

            // 실행 플레이어 이름들과 글자 색
            "Trigger(\"All players\"){\nConditions:\n\tAlways();\nActions:\n"
            "\tDisplay Text Message(Always Display, \"<1>a<4>b<8>c<C>d<R>e\");\n}\n"
            "Trigger(\"Current player\"){\nConditions:\n\tAlways();\nActions:\n"
            "\tPreserve Trigger();\n}\n"
            "Trigger(\"Foes\"){\nConditions:\n\tAlways();\nActions:\n"
            "\tPreserve Trigger();\n}\n"
            "Trigger(\"Allies\"){\nConditions:\n\tAlways();\nActions:\n"
            "\tPreserve Trigger();\n}\n",
        };

        for (const char * text : vocabulary)
        {
            chk::MapDocument sheet;
            SPLASH_CHECK(sheet.createNew(io::MapFormat::HybridScm, 0, 64, 64,
                                         io::MapArchive::DefaultTriggers::None));
            SPLASH_CHECK(sheet.applyTriggerText(text, graphics));
        }

        // --- EUD(메모리) 조건·동작이 편집기에 그렇게 보이는지 ---
        //
        // CHK 안에서 EUD 는 그냥 Deaths 다. 편집기가 그것을 Memory 로
        // 알아보지 못하면 첫 자리가 "플레이어" 로 보이고, 거기 든 큰 수를
        // 사람이 플레이어 번호로 고치는 순간 맵이 망가진다.
        {
            chk::MapDocument eudMap;
            SPLASH_CHECK(eudMap.createNew(io::MapFormat::HybridScm, 0, 64, 64,
                                          io::MapArchive::DefaultTriggers::None));
            SPLASH_CHECK(eudMap.addTrigger());

            io::MemoryActionSpec action;
            action.address = 0x0057F0F0; // 플레이어 1 미네랄
            action.modifier = 7;         // Set To
            action.amount = 1234;
            SPLASH_CHECK(eudMap.setActionMemory(0, 0, action));

            io::MemoryConditionSpec condition;
            condition.address = 0x0058D720;
            condition.comparison = 0;    // At least
            condition.amount = 1;
            condition.masked = true;
            condition.bitmask = 0xFF;
            SPLASH_CHECK(eudMap.setConditionMemory(0, 0, condition));

            const auto conditions = eudMap.triggerConditions(0, graphics);
            SPLASH_CHECK(!conditions.empty());
            if (!conditions.empty())
            {
                const auto & first = conditions.front();
                SPLASH_CHECK(first.memory);
                SPLASH_CHECK(first.masked);
                SPLASH_CHECK_EQ(first.typeKey, io::kMemoryMaskedType);
                SPLASH_CHECK(!first.args.empty());
                if (!first.args.empty())
                {
                    SPLASH_CHECK(first.args[0].role == io::TriggerArgRole::MemoryOffset);
                    SPLASH_CHECK_EQ(first.args[0].value,
                                    io::eud::epdFor(condition.address));
                }
                // 마스크 자리까지 보여야 한다 — 안 보이면 고칠 길이 없다.
                bool sawMask = false;
                for (const auto & arg : first.args)
                    sawMask = sawMask || arg.role == io::TriggerArgRole::MemoryBitmask;
                SPLASH_CHECK(sawMask);
            }

            const auto actions = eudMap.triggerActions(0, graphics);
            SPLASH_CHECK(!actions.empty());
            if (!actions.empty())
            {
                SPLASH_CHECK(actions.front().memory);
                SPLASH_CHECK(!actions.front().masked);
                SPLASH_CHECK_EQ(actions.front().typeKey, io::kMemoryType);
            }

            // 맵이 스스로 세는 EUD 자리와, 편집기가 보는 것이 같아야 한다.
            const auto usages = eudMap.eudUsages();
            SPLASH_CHECK_EQ(usages.size(), std::size_t(2));

            // 종류 목록에 Memory 가 있어야 고를 수 있다.
            bool offersMemory = false;
            for (const auto & choice : eudMap.conditionTypes(graphics))
                offersMemory = offersMemory || choice.value == io::kMemoryType;
            SPLASH_CHECK(offersMemory);

            // 텍스트로 뽑았다 다시 넣어도 그대로여야 한다.
            const auto text = eudMap.triggerText(graphics);
            SPLASH_CHECK(text.has_value());
            if (text)
            {
                SPLASH_CHECK(text->find("Memory") != std::string::npos);
                SPLASH_CHECK(eudMap.applyTriggerText(*text, graphics));
                SPLASH_CHECK_EQ(eudMap.eudUsages().size(), std::size_t(2));
            }
        }
    }

    // --- 실행 취소가 화면을 흔들지 않는지 ---
    SPLASH_CHECK(document.undo());
    view.refresh();
    SPLASH_CHECK_EQ(document.units().size(), before);

    // --- 맵이 없을 때 메뉴를 눌러도 버티는지 ---
    //
    // 맵을 열기 전에 메뉴를 누르는 일은 흔하다. 대부분 "먼저 맵을 여세요"
    // 로 끝나야 하는데, 한 군데라도 빈 문서를 만지면 거기서 터진다.
    //
    // 창을 띄우는 항목이 있으면 테스트가 멈추므로, 열린 창은 주기적으로
    // 닫아 준다.
    {
        ui::MainWindow window;
        window.resize(800, 600);
        window.show();

        QTimer closer;
        closer.setInterval(10);
        QObject::connect(&closer, &QTimer::timeout, [] {
            const auto windows = QApplication::topLevelWidgets();
            for (QWidget * widget : windows)
            {
                if (qobject_cast<ui::MainWindow *>(widget) != nullptr)
                    continue;
                if (widget->isVisible())
                    widget->close();
            }
        });
        closer.start();

        // 파일·색 고르기 창은 운영체제가 띄우는 것이라 닫기 어렵다.
        // (macOS 의 색 고르기는 모달이라 여기서 테스트가 멈춘다.)
        // 그런 항목만 빼고 누른다.
        const QStringList skip {
            QStringLiteral("새 맵"), QStringLiteral("열기"),
            QStringLiteral("다른 이름으로"), QStringLiteral("StarCraft 설치 폴더"),
            QStringLiteral("모드 자료"), QStringLiteral("종료"),
            QStringLiteral("맵을 그림으로"), QStringLiteral("쌓아 둔 사본"),
            QStringLiteral("격자 색"),
        };

        // 어떤 항목은 누르면 메뉴를 다시 만든다(최근 파일 목록 따위).
        // 그때 지워진 항목을 붙들고 있으면 안 되므로 약한 손잡이로 든다.
        const auto pressAll = [&skip](ui::MainWindow & target) {
            QList<QPointer<QAction>> actions;
            for (QAction * action : target.findChildren<QAction *>())
                actions.append(action);

            int count = 0;
            for (const QPointer<QAction> & action : actions)
            {
                if (action.isNull())
                    continue;

                const QString text = action->text();
                if (text.isEmpty() || !action->isEnabled())
                    continue;

                bool skipThis = false;
                for (const QString & needle : skip)
                    skipThis = skipThis || text.contains(needle);
                if (skipThis)
                    continue;

                action->trigger();
                QApplication::processEvents();
                ++count;
            }
            return count;
        };

        const int pressed = pressAll(window);

        std::cout << "  맵 없이 누른 메뉴 " << pressed << "개\n";
        SPLASH_CHECK(pressed > 20);

        // 맵을 연 뒤에도 한 바퀴 — 이쪽이 더 위험하다. 문서를 실제로
        // 만지는 길이 열리기 때문이다.
        window.openPath(QString::fromStdString(path.string()));

        const int pressedOpen = pressAll(window);

        closer.stop();
        std::cout << "  맵을 연 뒤 누른 메뉴 " << pressedOpen << "개\n";
        SPLASH_CHECK(pressedOpen > 20);
    }

    QSettings().clear();
    return splash::test::registry().report("화면 스모크");
}
