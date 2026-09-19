#include "ui/main_window.h"

#include "ui/map_view.h"
#include "ui/tile_palette.h"
#include "ui/mini_map.h"
#include "ui/sound_player.h"
#include "ui/briefing_editor.h"
#include "ui/code_editor_pane.h"
#include "ui/location_editor.h"
#include "ui/preset_editor.h"
#include "ui/settings_dialogs.h"
#include "ui/sound_editor.h"
#include "ui/string_editor.h"
#include "ui/switch_editor.h"
#include "ui/trigger_editor.h"
#include "ui/unit_palette.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QGridLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QGroupBox>
#include <QSettings>
#include <QSplitter>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QTabBar>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QDialog>
#include <QDockWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace splash::ui {
namespace {

constexpr const char * kAppName = "Splash Editor";

/// StarCraft 설치 경로를 기억해 두는 설정 키.
constexpr const char * kInstallPathKey = "installPath";

/// 열기/저장 대화상자에서 쓸 필터.
QString mapFilter()
{
    return QObject::tr(
        "StarCraft 맵 (*.scm *.scx *.chk);;"
        "StarCraft 맵 (*.scm);;"
        "Brood War 맵 (*.scx);;"
        "시나리오 청크 (*.chk);;"
        "모든 파일 (*)");
}

QString orDash(const std::string & value)
{
    return value.empty() ? QStringLiteral("—") : QString::fromStdString(value);
}

} // namespace

MainWindow::MainWindow(QWidget * parent) : QMainWindow(parent)
{
    // 맵이 하나도 없는 상태를 만들지 않는다 — 빈 문서로 시작한다.
    documents_.push_back(std::make_unique<chk::MapDocument>());

    buildCentralWidget();
    buildMenus();
    refreshFromDocument();
    resize(960, 640);

    // 지난번에 지정한 설치 폴더가 있으면 조용히 읽는다.
    loadTilesetFrom(QSettings().value(kInstallPathKey).toString(), /*announce*/ false);
}

MainWindow::~MainWindow() = default;

chk::MapDocument & MainWindow::document()
{
    return *documents_[static_cast<std::size_t>(currentDocument_)];
}

const chk::MapDocument & MainWindow::document() const
{
    return *documents_[static_cast<std::size_t>(currentDocument_)];
}

int MainWindow::addDocumentTab()
{
    documents_.push_back(std::make_unique<chk::MapDocument>());
    const int index = static_cast<int>(documents_.size()) - 1;

    if (tabs_ != nullptr)
        tabs_->addTab(tr("새 맵"));

    return index;
}

void MainWindow::switchToDocument(int index)
{
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return;

    currentDocument_ = index;

    if (tabs_ != nullptr && tabs_->currentIndex() != index)
        tabs_->setCurrentIndex(index);

    mapView_->setDocument(&document());
    mapView_->refresh();
    miniMap_->setDocument(&document());
    refreshFromDocument();
}

void MainWindow::closeDocumentTab(int index)
{
    if (index < 0 || index >= static_cast<int>(documents_.size()))
        return;

    // 마지막 하나는 닫는 대신 비운다 — 맵 없는 상태를 따로 다루지 않는다.
    if (documents_.size() == 1)
    {
        documents_[0] = std::make_unique<chk::MapDocument>();
        currentDocument_ = 0;
        if (tabs_ != nullptr)
            tabs_->setTabText(0, tr("새 맵"));
        switchToDocument(0);
        return;
    }

    documents_.erase(documents_.begin() + index);
    if (tabs_ != nullptr)
        tabs_->removeTab(index);

    currentDocument_ = std::min(currentDocument_, static_cast<int>(documents_.size()) - 1);
    switchToDocument(currentDocument_);
}

void MainWindow::refreshTabText(int index)
{
    if (tabs_ == nullptr || index < 0 || index >= tabs_->count())
        return;

    const auto & doc = *documents_[static_cast<std::size_t>(index)];

    QString label = doc.isOpen()
        ? QFileInfo(QString::fromStdString(doc.filePath())).fileName()
        : tr("새 맵");
    if (label.isEmpty())
        label = tr("새 맵");
    if (doc.isModified())
        label += QStringLiteral(" *");

    tabs_->setTabText(index, label);
}

void MainWindow::buildCentralWidget()
{
    mapView_ = new MapView(this);
    mapView_->setDocument(&document());
    mapView_->setTileset(&tileset_);

    connect(mapView_, &MapView::documentEdited, this, &MainWindow::onDocumentEdited);
    connect(mapView_, &MapView::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(mapView_, &MapView::unitActivated, this,
            [this](int) { onUnitProperties(); });

    soundPlayer_ = new SoundPlayer(this);
    connect(mapView_, &MapView::unitPlaced, this, [this](std::uint16_t unitType) {
        if (tileset_.hasUnitGraphics())
            soundPlayer_->play(tileset_.unitSound(unitType));
    });
    connect(mapView_, &MapView::brushTileChanged, this, [this](std::uint16_t tileId) {
        // 맵에서 스포이드로 집으면 팔레트 선택도 따라간다.
        if (tilePalette_ != nullptr)
            tilePalette_->setSelectedTile(tileId);
        statusBar()->showMessage(tr("브러시 타일: %1").arg(tileId), 3000);
    });

    auto * side = new QWidget(this);
    auto * outer = new QVBoxLayout(side);

    // 미니맵은 정보 패널 맨 위에 둔다 — 맵 전체를 보며 옮겨 다니는 용도다.
    miniMap_ = new MiniMap(side);
    miniMap_->setDocument(&document());
    miniMap_->setTileset(&tileset_);
    outer->addWidget(miniMap_);

    connect(miniMap_, &MiniMap::navigationRequested, this, [this](const QPointF & at) {
        mapView_->centerOnMap(at);
    });
    connect(mapView_, &MapView::viewportMoved, this, [this] {
        miniMap_->setViewportRect(mapView_->visibleMapRect());
    });

    auto * form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    nameValue_    = new QLabel(side);
    sizeValue_    = new QLabel(side);
    tilesetValue_ = new QLabel(side);
    versionValue_ = new QLabel(side);
    countsValue_  = new QLabel(side);
    pathValue_    = new QLabel(side);

    // 긴 경로/이름이 창을 늘리지 않도록.
    for (QLabel * label : {nameValue_, tilesetValue_, versionValue_, countsValue_, pathValue_})
    {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
    }

    form->addRow(tr("이름"),      nameValue_);
    form->addRow(tr("크기"),      sizeValue_);
    form->addRow(tr("타일셋"),    tilesetValue_);
    form->addRow(tr("버전"),      versionValue_);
    form->addRow(tr("내용"),      countsValue_);
    form->addRow(tr("경로"),      pathValue_);

    outer->addLayout(form);
    outer->addStretch();

    auto * splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(mapView_);
    splitter->addWidget(side);
    splitter->setStretchFactor(0, 1); // 맵이 주인공이다
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({640, 280});

    // 여러 맵을 함께 열어 두고 탭으로 오간다.
    tabs_ = new QTabBar(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(false);
    tabs_->setExpanding(false);
    tabs_->setDocumentMode(true);
    tabs_->addTab(tr("새 맵"));

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index != currentDocument_)
            switchToDocument(index);
    });
    connect(tabs_, &QTabBar::tabCloseRequested, this, [this](int index) {
        // 닫기 전에 저장을 물어야 하므로 그 탭으로 옮겨 놓고 확인한다.
        switchToDocument(index);
        if (!confirmDiscardChanges())
            return;
        closeDocumentTab(index);
    });

    auto * central = new QWidget(this);
    auto * centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(tabs_);
    centralLayout->addWidget(splitter, 1);

    setCentralWidget(central);

    // 지형 타일 팔레트는 도크로 둔다 — 지형 작업을 할 때만 열어 두면 된다.
    auto * terrainPanel = new QWidget(this);
    auto * terrainLayout = new QVBoxLayout(terrainPanel);
    terrainLayout->setContentsMargins(4, 4, 4, 4);

    // 지형을 놓는 방식. SCMDraft 와 같은 세 가지다.
    auto * modeBox = new QComboBox(terrainPanel);
    modeBox->addItem(tr("Isometric — 절벽·해안 자동 연결"),
                     int(MapView::TerrainMode::Isometric));
    modeBox->addItem(tr("Rectangular — 브러시 크기로 칠하기"),
                     int(MapView::TerrainMode::Rectangular));
    modeBox->addItem(tr("Subtile — 한 칸씩 정밀하게"),
                     int(MapView::TerrainMode::Subtile));
    // 두들은 지형 모드가 아니라 따로 놓는 물체라 -1 로 구분한다.
    modeBox->addItem(tr("두들 — 나무·바위 같은 지형 장식"), -1);
    modeBox->setCurrentIndex(1); // Rectangular

    tilePalette_ = new TilePalette(terrainPanel);
    tilePalette_->setTileset(&tileset_);

    terrainLayout->addWidget(modeBox);
    terrainLayout->addWidget(tilePalette_, 1);

    connect(modeBox, &QComboBox::currentIndexChanged, this, [this, modeBox](int) {
        const int value = modeBox->currentData().toInt();

        if (value < 0)
        {
            tilePalette_->setDoodadMode(true);
            mapView_->setTool(MapView::Tool::PlaceDoodad);
            statusBar()->showMessage(tr("두들 — 팔레트에서 고르고 맵을 클릭하세요"), 4000);
            return;
        }

        tilePalette_->setDoodadMode(false);

        const auto mode = static_cast<MapView::TerrainMode>(value);
        mapView_->setTerrainMode(mode);
        mapView_->setTool(MapView::Tool::Terrain);

        // ISOM 은 지형 종류를 고르고, 나머지는 타일을 고른다.
        tilePalette_->setTerrainTypeMode(mode == MapView::TerrainMode::Isometric);
    });

    connect(tilePalette_, &TilePalette::doodadSelected, this, [this](std::uint16_t doodadId) {
        mapView_->setPlacementDoodad(doodadId);
        mapView_->setTool(MapView::Tool::PlaceDoodad);
        statusBar()->showMessage(tr("두들을 골랐습니다 — 맵을 클릭하세요"), 3000);
    });

    connect(tilePalette_, &TilePalette::terrainTypeSelected, this,
            [this](std::size_t brushIndex) {
        mapView_->setIsomTerrainType(brushIndex);
        mapView_->setTool(MapView::Tool::Terrain);
        statusBar()->showMessage(tr("ISOM 지형을 골랐습니다 — 맵을 클릭하세요"), 3000);
    });

    paletteDock_ = new QDockWidget(tr("지형 팔레트"), this);
    paletteDock_->setWidget(terrainPanel);
    paletteDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock_);
    paletteDock_->show(); // 타일을 고르는 곳이라 열어 둔다

    // 팔레트에서 타일을 고르면 브러시가 되고, 지형 도구로 넘어간다.
    connect(tilePalette_, &TilePalette::tileSelected, this, [this](std::uint16_t tileId) {
        mapView_->setBrushTile(tileId);
        mapView_->setTool(MapView::Tool::Terrain);
        statusBar()->showMessage(tr("브러시 타일: %1").arg(tileId), 2000);
    });

    // 유닛 팔레트 — 놓을 유닛과 소유자를 고른다.
    auto * unitPanel = new QWidget(this);
    auto * unitLayout = new QVBoxLayout(unitPanel);
    unitLayout->setContentsMargins(4, 4, 4, 4);

    auto * categoryBox = new QComboBox(unitPanel);
    for (auto category : {UnitPalette::Category::All,
                          UnitPalette::Category::TerranUnits,
                          UnitPalette::Category::TerranBuildings,
                          UnitPalette::Category::ZergUnits,
                          UnitPalette::Category::ZergBuildings,
                          UnitPalette::Category::ProtossUnits,
                          UnitPalette::Category::ProtossBuildings,
                          UnitPalette::Category::Neutral,
                          UnitPalette::Category::Sprites})
    {
        categoryBox->addItem(UnitPalette::categoryName(category), static_cast<int>(category));
    }

    ownerBox_ = new QComboBox(unitPanel);
    QComboBox * ownerBox = ownerBox_;
    for (int player = 1; player <= 12; ++player)
        ownerBox->addItem(tr("플레이어 %1").arg(player), player - 1);
    ownerBox->setCurrentIndex(0);

    unitPalette_ = new UnitPalette(unitPanel);
    unitPalette_->setTileset(&tileset_);

    unitLayout->addWidget(categoryBox);
    unitLayout->addWidget(ownerBox);
    unitLayout->addWidget(unitPalette_, 1);

    connect(categoryBox, &QComboBox::currentIndexChanged, this, [this, categoryBox](int) {
        unitPalette_->setCategory(
            static_cast<UnitPalette::Category>(categoryBox->currentData().toInt()));
    });

    unitDock_ = new QDockWidget(tr("유닛 팔레트"), this);
    unitDock_->setWidget(unitPanel);
    unitDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, unitDock_);
    // 팔레트는 열어 둔다 — 놓을 것을 고르는 곳이라 늘 쓰인다.
    unitDock_->show();

    connect(ownerBox, &QComboBox::currentIndexChanged, this, [this, ownerBox](int) {
        const auto owner = static_cast<std::uint8_t>(ownerBox->currentData().toInt());
        unitPalette_->setOwner(owner);
        mapView_->setPlacementUnit(unitPalette_->selectedUnit(), owner);
    });

    connect(mapView_, &MapView::ownerRequested, this, [this](std::uint8_t owner) {
        if (ownerBox_ == nullptr)
            return;
        const int index = ownerBox_->findData(static_cast<int>(owner));
        if (index < 0)
            return;
        ownerBox_->setCurrentIndex(index); // 나머지는 콤보의 신호가 처리한다
        statusBar()->showMessage(tr("플레이어 %1").arg(owner + 1), 2000);
    });

    connect(mapView_, &MapView::toolChanged, this, [this](MapView::Tool tool) {
        if (tool == MapView::Tool::Select && selectToolAction_ != nullptr)
        {
            selectToolAction_->setChecked(true);
            statusBar()->showMessage(tr("배치를 그만두고 선택 도구로 돌아갑니다"), 2000);
        }
    });

    connect(mapView_, &MapView::placementRejected, this, [this](const QString & reason) {
        statusBar()->showMessage(reason, 3000);
    });

    connect(unitPalette_, &UnitPalette::spriteSelected, this, [this](std::uint16_t spriteType) {
        mapView_->setPlacementSprite(spriteType, unitPalette_->owner());
        mapView_->setTool(MapView::Tool::PlaceSprite);
        statusBar()->showMessage(
            tr("놓을 스프라이트: %1 — 맵을 클릭하세요").arg(spriteType), 4000);
    });

    connect(unitPalette_, &UnitPalette::unitSelected, this, [this](std::uint16_t unitType) {
        mapView_->setPlacementUnit(unitType, unitPalette_->owner());
        mapView_->setTool(MapView::Tool::PlaceUnit);
        statusBar()->showMessage(
            tr("놓을 유닛: %1 — 맵을 클릭하세요")
                .arg(QString::fromStdString(splash::io::unitTypeName(unitType))), 4000);
    });

    statusBar();
}

void MainWindow::buildMenus()
{
    QMenu * fileMenu = menuBar()->addMenu(tr("파일(&F)"));

    QAction * newAction = fileMenu->addAction(tr("새 맵(&N)…"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewMap);

    QAction * openAction = fileMenu->addAction(tr("열기(&O)…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);

    recentMenu_ = fileMenu->addMenu(tr("최근 파일(&R)"));
    rebuildRecentMenu();

    saveAction_ = fileMenu->addAction(tr("저장(&S)"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::onSave);

    saveAsAction_ = fileMenu->addAction(tr("다른 이름으로 저장(&A)…"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::onSaveAs);

    fileMenu->addSeparator();

    closeAction_ = fileMenu->addAction(tr("닫기(&C)"));
    closeAction_->setShortcut(QKeySequence::Close);
    connect(closeAction_, &QAction::triggered, this, &MainWindow::onClose);

    fileMenu->addSeparator();

    QAction * stringsAction = fileMenu->addAction(tr("문자열 편집기(&S)…"));
    stringsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(stringsAction, &QAction::triggered, this, &MainWindow::onStringEditor);

    QAction * playersAction = fileMenu->addAction(tr("플레이어 설정(&L)…"));
    playersAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(playersAction, &QAction::triggered, this, &MainWindow::onPlayerSettings);

    QAction * propertiesAction = fileMenu->addAction(tr("맵 속성(&P)…"));
    propertiesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(propertiesAction, &QAction::triggered, this, &MainWindow::onMapProperties);

    fileMenu->addSeparator();

    QAction * installAction = fileMenu->addAction(tr("StarCraft 설치 폴더 지정(&I)…"));
    connect(installAction, &QAction::triggered, this, &MainWindow::onChooseInstallPath);

    fileMenu->addSeparator();

    QAction * quitAction = fileMenu->addAction(tr("종료(&Q)"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu * editMenu = menuBar()->addMenu(tr("편집(&E)"));

    undoAction_ = editMenu->addAction(tr("실행 취소(&U)"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::onUndo);

    redoAction_ = editMenu->addAction(tr("다시 실행(&R)"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::onRedo);

    editMenu->addSeparator();

    QAction * copyAction = editMenu->addAction(tr("복사(&C)"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, [this] {
        if (mapView_->copySelection())
            statusBar()->showMessage(tr("유닛을 복사했습니다"), 2000);
        else
            statusBar()->showMessage(tr("복사할 유닛을 먼저 고르세요"), 2000);
    });

    QAction * pasteAction = editMenu->addAction(tr("붙여넣기(&V)"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, [this] {
        if (mapView_->pasteAtCentre())
            statusBar()->showMessage(tr("유닛을 붙였습니다"), 2000);
        else
            statusBar()->showMessage(tr("붙일 유닛이 없습니다"), 2000);
    });

    editMenu->addSeparator();

    QAction * unitPropsAction = editMenu->addAction(tr("유닛 속성(&P)…"));
    unitPropsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(unitPropsAction, &QAction::triggered, this, &MainWindow::onUnitProperties);

    deleteAction_ = editMenu->addAction(tr("선택 삭제(&D)"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::onDeleteSelection);

    QMenu * toolMenu = menuBar()->addMenu(tr("도구(&T)"));

    auto * toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    selectToolAction_ = toolMenu->addAction(tr("선택(&S)"));
    QAction * selectTool = selectToolAction_;
    selectTool->setCheckable(true);
    selectTool->setChecked(true);
    selectTool->setShortcut(QKeySequence(Qt::Key_S));
    toolGroup->addAction(selectTool);
    connect(selectTool, &QAction::triggered, this,
            [this] { mapView_->setTool(MapView::Tool::Select);
                     statusBar()->showMessage(tr("선택 도구"), 2000); });

    QAction * placeTool = toolMenu->addAction(tr("유닛 놓기(&U)"));
    placeTool->setCheckable(true);
    placeTool->setShortcut(QKeySequence(Qt::Key_U));
    toolGroup->addAction(placeTool);
    connect(placeTool, &QAction::triggered, this, [this] {
        mapView_->setTool(MapView::Tool::PlaceUnit);
        if (unitDock_ != nullptr)
            unitDock_->show();
        statusBar()->showMessage(tr("유닛 놓기 — 팔레트에서 유닛을 고르세요"), 4000);
    });

    QAction * terrainTool = toolMenu->addAction(tr("지형 칠하기(&T)"));
    terrainTool->setCheckable(true);
    terrainTool->setShortcut(QKeySequence(Qt::Key_T));
    toolGroup->addAction(terrainTool);
    connect(terrainTool, &QAction::triggered, this,
            [this] { mapView_->setTool(MapView::Tool::Terrain);
                     if (paletteDock_ != nullptr)
                         paletteDock_->show(); // 칠하려면 타일을 골라야 한다
                     statusBar()->showMessage(
                         tr("지형 도구 — 팔레트에서 타일을 고르거나 Alt+클릭으로 집기"), 5000); });

    QAction * fogTool = toolMenu->addAction(tr("시야 가리개(&F)"));
    fogTool->setCheckable(true);
    fogTool->setShortcut(QKeySequence(Qt::Key_F));
    toolGroup->addAction(fogTool);
    connect(fogTool, &QAction::triggered, this, [this] {
        mapView_->setTool(MapView::Tool::Fog);
        mapView_->setFogVisible(true);
        statusBar()->showMessage(
            tr("시야 가리개 — 끌어서 가리고, 걷기를 켜면 지웁니다"), 4000);
    });

    // 어느 플레이어의 가리개를 칠할지.
    QMenu * fogMenu = toolMenu->addMenu(tr("가리개 플레이어"));
    auto * fogGroup = new QActionGroup(this);
    fogGroup->setExclusive(true);
    for (int player = 0; player < 8; ++player)
    {
        QAction * action = fogMenu->addAction(tr("플레이어 %1").arg(player + 1));
        action->setCheckable(true);
        action->setChecked(player == 0);
        fogGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, player] {
            mapView_->setFogPlayers(static_cast<std::uint8_t>(1u << player));
            statusBar()->showMessage(tr("가리개: 플레이어 %1").arg(player + 1), 2000);
        });
    }
    QAction * fogAll = fogMenu->addAction(tr("모든 플레이어"));
    fogAll->setCheckable(true);
    fogGroup->addAction(fogAll);
    connect(fogAll, &QAction::triggered, this, [this] {
        mapView_->setFogPlayers(0xFF);
        statusBar()->showMessage(tr("가리개: 모든 플레이어"), 2000);
    });

    QAction * fogErase = toolMenu->addAction(tr("가리개 걷기"));
    fogErase->setCheckable(true);
    connect(fogErase, &QAction::toggled, this, [this](bool on) {
        mapView_->setFogErasing(on);
        statusBar()->showMessage(on ? tr("칠하면 가리개를 걷습니다")
                                    : tr("칠하면 가립니다"), 2000);
    });

    toolMenu->addSeparator();

    for (int size : {1, 2, 4, 8})
    {
        QAction * action = toolMenu->addAction(tr("브러시 %1x%1").arg(size));
        action->setCheckable(true);
        action->setChecked(size == 1);
        connect(action, &QAction::triggered, this, [this, size, toolMenu] {
            mapView_->setBrushSize(size);
            statusBar()->showMessage(tr("브러시 %1x%1").arg(size), 2000);
            // 같은 그룹의 다른 크기는 해제한다
            for (QAction * other : toolMenu->actions())
            {
                if (other->isCheckable() && other->text().startsWith(tr("브러시")))
                    other->setChecked(other->text() == tr("브러시 %1x%1").arg(size));
            }
        });
    }

    toolMenu->addSeparator();

    // 유닛을 어디에 놓을지 — 격자에 맞출지, 겹쳐 놓을 수 있을지.
    QMenu * snapMenu = toolMenu->addMenu(tr("유닛 배치 격자"));
    auto * snapGroup = new QActionGroup(this);
    snapGroup->setExclusive(true);
    const struct { MapView::UnitSnap snap; const char * label; } kSnapChoices[] {
        { MapView::UnitSnap::Tile,     QT_TR_NOOP("한 타일 (32px)") },
        { MapView::UnitSnap::HalfTile, QT_TR_NOOP("반 타일 (16px)") },
        { MapView::UnitSnap::Quarter,  QT_TR_NOOP("1/4 타일 (8px)") },
        { MapView::UnitSnap::Free,     QT_TR_NOOP("자유 배치") },
    };
    for (const auto & choice : kSnapChoices)
    {
        QAction * action = snapMenu->addAction(tr(choice.label));
        action->setCheckable(true);
        action->setChecked(choice.snap == mapView_->unitSnap());
        snapGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, choice] {
            mapView_->setUnitSnap(choice.snap);
            statusBar()->showMessage(tr("유닛 배치 격자: %1").arg(tr(choice.label)), 2000);
        });
    }

    // 대칭 맵을 만들 때 한쪽만 그리면 되도록.
    QMenu * symmetryMenu = toolMenu->addMenu(tr("지형 대칭"));
    auto * symmetryGroup = new QActionGroup(this);
    symmetryGroup->setExclusive(true);
    const struct { MapView::Symmetry value; const char * label; } kSymmetries[] {
        { MapView::Symmetry::None,       QT_TR_NOOP("없음") },
        { MapView::Symmetry::Horizontal, QT_TR_NOOP("좌우") },
        { MapView::Symmetry::Vertical,   QT_TR_NOOP("위아래") },
        { MapView::Symmetry::Both,       QT_TR_NOOP("네 곳") },
    };
    for (const auto & choice : kSymmetries)
    {
        QAction * action = symmetryMenu->addAction(tr(choice.label));
        action->setCheckable(true);
        action->setChecked(choice.value == mapView_->terrainSymmetry());
        symmetryGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, choice] {
            mapView_->setTerrainSymmetry(choice.value);
            statusBar()->showMessage(tr("지형 대칭: %1").arg(tr(choice.label)), 2500);
        });
    }

    QAction * terrainCheckAction = toolMenu->addAction(tr("건물 지형 검사"));
    terrainCheckAction->setCheckable(true);
    terrainCheckAction->setChecked(mapView_->terrainCheckEnabled());
    terrainCheckAction->setToolTip(
        tr("켜면 건물을 지을 수 있는 평지에만 놓습니다. 유즈맵처럼 일부러 "
           "물이나 절벽에 올릴 때는 끄세요."));
    connect(terrainCheckAction, &QAction::toggled, this, [this](bool on) {
        mapView_->setTerrainCheckEnabled(on);
        statusBar()->showMessage(on ? tr("건물은 지을 수 있는 땅에만 놓습니다")
                                    : tr("지형을 따지지 않고 놓습니다"), 2500);
    });

    QAction * groundCheckAction = toolMenu->addAction(tr("지상 유닛 지형 검사"));
    groundCheckAction->setCheckable(true);
    groundCheckAction->setChecked(mapView_->groundUnitCheckEnabled());
    groundCheckAction->setToolTip(
        tr("켜면 지상 유닛을 걸을 수 있는 땅에만 놓습니다. 공중 유닛은 "
           "어디든 놓입니다."));
    connect(groundCheckAction, &QAction::toggled, this, [this](bool on) {
        mapView_->setGroundUnitCheckEnabled(on);
        statusBar()->showMessage(on ? tr("지상 유닛은 갈 수 있는 땅에만 놓습니다")
                                    : tr("지상 유닛은 지형을 따지지 않습니다"), 2500);
    });

    QAction * stackAction = toolMenu->addAction(tr("유닛 겹쳐 놓기 허용"));
    stackAction->setCheckable(true);
    stackAction->setChecked(mapView_->unitStackingAllowed());
    connect(stackAction, &QAction::toggled, this, [this](bool on) {
        mapView_->setUnitStackingAllowed(on);
        statusBar()->showMessage(on ? tr("겹쳐 놓기를 허용합니다")
                                    : tr("겹치는 자리에는 놓지 않습니다"), 2500);
    });

    // 시나리오가 게임 규칙을 어떻게 바꾸는지 — 유닛 능력치, 업그레이드,
    // 기술. StarEdit 의 Scenario 메뉴에 해당한다.
    QMenu * scenarioMenu = menuBar()->addMenu(tr("시나리오(&C)"));

    QAction * unitSettingsAction = scenarioMenu->addAction(tr("유닛 설정(&U)…"));
    connect(unitSettingsAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        UnitSettingsDialog dialog(document(), tileset_, this);
        connect(&dialog, &UnitSettingsDialog::documentEdited, this, [this] { onDocumentEdited(); });
        dialog.exec();
    });

    QAction * upgradeSettingsAction = scenarioMenu->addAction(tr("업그레이드 설정(&G)…"));
    connect(upgradeSettingsAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        UpgradeSettingsDialog dialog(document(), tileset_, this);
        connect(&dialog, &UpgradeSettingsDialog::documentEdited, this, [this] { onDocumentEdited(); });
        dialog.exec();
    });

    QAction * techSettingsAction = scenarioMenu->addAction(tr("기술 설정(&T)…"));
    connect(techSettingsAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        TechSettingsDialog dialog(document(), tileset_, this);
        connect(&dialog, &TechSettingsDialog::documentEdited, this, [this] { onDocumentEdited(); });
        dialog.exec();
    });

    QAction * locationAction = scenarioMenu->addAction(tr("로케이션(&L)…"));
    connect(locationAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        auto * editor = new LocationEditor(document(), this);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(editor, &LocationEditor::documentEdited, this, [this] {
            mapView_->refresh();
            onDocumentEdited();
        });
        connect(editor, &LocationEditor::locationFocused, this, [this](std::size_t index) {
            // 고른 로케이션이 보이도록 화면을 옮기고 표시를 켠다.
            mapView_->setLocationsVisible(true);
            mapView_->focusLocation(index);
        });
        editor->show();
    });

    QAction * soundSettingsAction = scenarioMenu->addAction(tr("소리 설정(&S)…"));
    connect(soundSettingsAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        auto * editor = new SoundEditor(document(), soundPlayer_, this);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(editor, &SoundEditor::documentEdited, this, [this] { onDocumentEdited(); });
        editor->show();
    });

    QAction * briefingAction = scenarioMenu->addAction(tr("미션 브리핑(&B)…"));
    connect(briefingAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        if (!tileset_.isLoaded())
        {
            statusBar()->showMessage(tr("브리핑을 읽으려면 StarCraft 설치 폴더가 필요합니다"), 4000);
            return;
        }
        auto * editor = new BriefingEditor(document(), tileset_, this);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(editor, &BriefingEditor::documentEdited, this, [this] { onDocumentEdited(); });
        editor->show();
    });

    QAction * presetAction = scenarioMenu->addAction(tr("유닛 속성 프리셋(&R)…"));
    connect(presetAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        auto * editor = new PresetEditor(document(), this);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(editor, &PresetEditor::documentEdited, this, [this] { onDocumentEdited(); });
        editor->show();
    });

    scenarioMenu->addSeparator();

    QAction * unprotectAction = scenarioMenu->addAction(tr("보호 해제(&P)…"));
    unprotectAction->setToolTip(
        tr("규격을 벗어나게 만들어 둔 맵을 고쳐 편집·저장할 수 있게 합니다."));
    connect(unprotectAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }

        if (!document().isProtected() && !document().hasPassword())
        {
            QMessageBox::information(this, tr("보호 해제"),
                                     tr("이 맵은 보호되어 있지 않습니다."));
            return;
        }

        const auto answer = QMessageBox::question(this, tr("보호 해제"),
            tr("맵의 보호를 풉니다. 문자열 표를 줄이고 빠진 구역을 채우며, "
               "저장할 때 아카이브를 새로 씁니다.\n\n계속할까요?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;

        std::string report;
        if (!document().unprotect(&report))
        {
            QMessageBox::warning(this, tr("보호 해제 실패"),
                                 QString::fromStdString(document().lastError()));
            return;
        }

        refreshFromDocument();
        QMessageBox::information(this, tr("보호 해제"),
            tr("보호를 풀었습니다.\n\n%1").arg(QString::fromStdString(report)));
    });

    QMenu * triggerMenu = menuBar()->addMenu(tr("트리거(&R)"));

    QAction * switchAction = triggerMenu->addAction(tr("스위치 이름(&W)…"));
    connect(switchAction, &QAction::triggered, this, [this] {
        if (!document().isOpen())
        {
            statusBar()->showMessage(tr("먼저 맵을 여세요"), 3000);
            return;
        }
        auto * editor = new SwitchEditor(document(), this);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(editor, &SwitchEditor::documentEdited, this, [this] { onDocumentEdited(); });
        editor->show();
    });
    QAction * editTriggers = triggerMenu->addAction(tr("트리거 편집기(&E)…"));
    editTriggers->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    connect(editTriggers, &QAction::triggered, this, [this] {
        if (!document().isOpen())
            return;
        if (!tileset_.hasUnitGraphics())
        {
            QMessageBox::information(this, tr("트리거"),
                tr("트리거를 다루려면 StarCraft 설치 폴더가 필요합니다.\n"
                   "유닛과 업그레이드 이름표가 게임 데이터에 들어 있기 때문입니다."));
            return;
        }

        auto * editor = new TriggerEditor(document(), tileset_, this);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(editor, &TriggerEditor::documentEdited, this, [this] {
            mapView_->refresh();
            refreshFromDocument();
        });
        editor->show();
    });

    QAction * showTriggers = triggerMenu->addAction(tr("트리거 텍스트 보기(&V)…"));
    showTriggers->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(showTriggers, &QAction::triggered, this, &MainWindow::onShowTriggers);

    toolMenu->addSeparator();
    QAction * soundAction = toolMenu->addAction(tr("배치 소리(&S)"));
    soundAction->setCheckable(true);
    soundAction->setChecked(true);
    connect(soundAction, &QAction::toggled, this, [this](bool on) {
        if (soundPlayer_ != nullptr)
            soundPlayer_->setEnabled(on);
    });

    QAction * showUnitPalette = toolMenu->addAction(tr("유닛 팔레트(&N)"));
    showUnitPalette->setCheckable(true);
    connect(showUnitPalette, &QAction::toggled, this, [this](bool on) {
        if (unitDock_ != nullptr)
            unitDock_->setVisible(on);
    });
    connect(unitDock_, &QDockWidget::visibilityChanged, showUnitPalette, &QAction::setChecked);

    QAction * showPalette = toolMenu->addAction(tr("타일 팔레트(&P)"));
    showPalette->setCheckable(true);
    connect(showPalette, &QAction::toggled, this, [this](bool on) {
        if (paletteDock_ != nullptr)
            paletteDock_->setVisible(on);
    });
    connect(paletteDock_, &QDockWidget::visibilityChanged, showPalette, &QAction::setChecked);

    QMenu * windowMenu = menuBar()->addMenu(tr("창(&W)"));

    QAction * nextTab = windowMenu->addAction(tr("다음 맵"));
    nextTab->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
    connect(nextTab, &QAction::triggered, this, [this] {
        if (documents_.size() < 2)
            return;
        switchToDocument((currentDocument_ + 1) % static_cast<int>(documents_.size()));
    });

    QAction * previousTab = windowMenu->addAction(tr("이전 맵"));
    previousTab->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
    connect(previousTab, &QAction::triggered, this, [this] {
        const int count = static_cast<int>(documents_.size());
        if (count < 2)
            return;
        switchToDocument((currentDocument_ - 1 + count) % count);
    });

    QMenu * viewMenu = menuBar()->addMenu(tr("보기(&V)"));

    zoomInAction_ = viewMenu->addAction(tr("확대(&I)"));
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction_, &QAction::triggered, mapView_, &MapView::zoomIn);

    zoomOutAction_ = viewMenu->addAction(tr("축소(&O)"));
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction_, &QAction::triggered, mapView_, &MapView::zoomOut);

    zoomResetAction_ = viewMenu->addAction(tr("실제 크기(&A)"));
    zoomResetAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomResetAction_, &QAction::triggered, mapView_, &MapView::zoomReset);

    viewMenu->addSeparator();

    QAction * showUnits = viewMenu->addAction(tr("유닛 표시(&U)"));
    showUnits->setCheckable(true);
    showUnits->setChecked(mapView_->unitsVisible());
    showUnits->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(showUnits, &QAction::toggled, mapView_, &MapView::setUnitsVisible);

    QAction * showLocations = viewMenu->addAction(tr("로케이션 표시(&L)"));
    showLocations->setCheckable(true);
    showLocations->setChecked(mapView_->locationsVisible());
    showLocations->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(showLocations, &QAction::toggled, mapView_, &MapView::setLocationsVisible);

    QAction * showGrid = viewMenu->addAction(tr("격자 표시(&G)"));
    showGrid->setCheckable(true);
    showGrid->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    connect(showGrid, &QAction::toggled, mapView_, &MapView::setGridVisible);

    QAction * showLinks = viewMenu->addAction(tr("유닛 연결 표시(&K)"));
    showLinks->setCheckable(true);
    showLinks->setChecked(mapView_->unitLinksVisible());
    showLinks->setToolTip(
        tr("애드온이 붙은 건물과 이어진 나이더스 굴을 선으로 잇습니다."));
    connect(showLinks, &QAction::toggled, mapView_, &MapView::setUnitLinksVisible);

    QAction * showFog = viewMenu->addAction(tr("시야 가리개 표시(&F)"));
    showFog->setCheckable(true);
    showFog->setChecked(mapView_->fogVisible());
    showFog->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_4));
    connect(showFog, &QAction::toggled, mapView_, &MapView::setFogVisible);

    QAction * showCreep = viewMenu->addAction(tr("크립 표시(&C)"));
    showCreep->setCheckable(true);
    showCreep->setChecked(mapView_->creepVisible());
    showCreep->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    connect(showCreep, &QAction::toggled, mapView_, &MapView::setCreepVisible);
}

void MainWindow::onUndo()
{
    if (!document().undo())
    {
        statusBar()->showMessage(QString::fromStdString(document().lastError()), 3000);
        return;
    }
    mapView_->clearSelection();
    mapView_->refresh();
    refreshFromDocument();
    statusBar()->showMessage(tr("실행 취소"), 2000);
}

void MainWindow::onRedo()
{
    if (!document().redo())
    {
        statusBar()->showMessage(QString::fromStdString(document().lastError()), 3000);
        return;
    }
    mapView_->clearSelection();
    mapView_->refresh();
    refreshFromDocument();
    statusBar()->showMessage(tr("다시 실행"), 2000);
}

void MainWindow::onDeleteSelection()
{
    if (!mapView_->deleteSelectedUnit())
        statusBar()->showMessage(tr("선택된 유닛이 없습니다."), 2000);
}

void MainWindow::onDocumentEdited()
{
    // 편집으로 유닛 목록이 바뀌었으니 뷰의 캐시도 무효가 된다.
    mapView_->refresh();
    refreshFromDocument();
}

void MainWindow::onSelectionChanged(int unitIndex)
{
    if (deleteAction_ != nullptr)
        deleteAction_->setEnabled(unitIndex >= 0);

    if (unitIndex < 0)
    {
        // 유닛이 아니면 로케이션이 잡혔을 수 있다.
        const int locationIndex = mapView_->selectedLocation();
        const auto & locations = document().locations();
        if (locationIndex >= 0 && static_cast<std::size_t>(locationIndex) < locations.size())
        {
            const auto & location = locations[static_cast<std::size_t>(locationIndex)];
            statusBar()->showMessage(
                tr("로케이션 #%1  %2  (%3, %4)-(%5, %6)")
                    .arg(location.index)
                    .arg(location.name.empty() ? tr("(이름 없음)")
                                               : QString::fromStdString(location.name))
                    .arg(location.left).arg(location.top)
                    .arg(location.right).arg(location.bottom));
            return;
        }

        statusBar()->clearMessage();
        return;
    }

    const auto & units = document().units();
    if (static_cast<std::size_t>(unitIndex) >= units.size())
        return;

    const auto & unit = units[static_cast<std::size_t>(unitIndex)];
    statusBar()->showMessage(
        tr("#%1  %2  P%3  (%4, %5)")
            .arg(unitIndex)
            .arg(QString::fromStdString(unit.typeName))
            .arg(unit.owner + 1)
            .arg(unit.x)
            .arg(unit.y));
}

namespace {

/// 타일셋 고르는 콤보를 만든다. 순서는 CHK 의 ERA 값과 같다.
QComboBox * makeTilesetBox(QWidget * parent, std::uint16_t current)
{
    auto * box = new QComboBox(parent);
    for (std::uint16_t id = 0; id < 8; ++id)
        box->addItem(QString::fromStdString(splash::chk::tilesetDisplayName(id)), id);
    box->setCurrentIndex(current % 8);
    return box;
}

} // namespace

void MainWindow::onNewMap()
{
    if (!confirmDiscardChanges())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("새 맵"));

    auto * form = new QFormLayout();

    auto * widthBox = new QSpinBox(&dialog);
    widthBox->setRange(32, 256);
    widthBox->setSingleStep(32);
    widthBox->setValue(64);

    auto * heightBox = new QSpinBox(&dialog);
    heightBox->setRange(32, 256);
    heightBox->setSingleStep(32);
    heightBox->setValue(64);

    // 자주 쓰는 크기를 먼저 고르게 한다. 직접 입력도 그대로 된다.
    auto * presetBox = new QComboBox(&dialog);
    presetBox->addItem(tr("직접 입력"), QPoint(0, 0));
    for (const QPoint & size : {QPoint(64, 64), QPoint(96, 96), QPoint(128, 128),
                                QPoint(128, 96), QPoint(192, 192), QPoint(256, 256),
                                QPoint(256, 128)})
    {
        presetBox->addItem(tr("%1 x %2").arg(size.x()).arg(size.y()), size);
    }
    presetBox->setCurrentIndex(1); // 64x64

    connect(presetBox, &QComboBox::currentIndexChanged, &dialog,
            [presetBox, widthBox, heightBox](int) {
        const QPoint size = presetBox->currentData().toPoint();
        if (size.x() > 0)
        {
            widthBox->setValue(size.x());
            heightBox->setValue(size.y());
        }
    });

    auto * tilesetBox = makeTilesetBox(&dialog, 4); // Jungle

    // 시작 지형 — 타일셋마다 종류가 다르므로 타일셋을 바꾸면 다시 채운다.
    auto * terrainBox = new QComboBox(&dialog);
    const auto fillTerrain = [this, terrainBox](std::uint16_t tilesetId) {
        terrainBox->clear();
        if (!tileset_.isLoaded())
        {
            terrainBox->addItem(tr("기본"), 0);
            return;
        }
        for (const auto & type : tileset_.terrainTypes(tilesetId))
        {
            terrainBox->addItem(QString::fromStdString(type.name),
                                static_cast<qulonglong>(type.brushIndex));
        }
        if (terrainBox->count() == 0)
            terrainBox->addItem(tr("기본"), 0);
    };
    fillTerrain(4);

    connect(tilesetBox, &QComboBox::currentIndexChanged, &dialog,
            [tilesetBox, fillTerrain](int) {
        fillTerrain(static_cast<std::uint16_t>(tilesetBox->currentData().toInt()));
    });

    auto * formatBox = new QComboBox(&dialog);
    formatBox->addItem(tr("브루드워 (.scx)"), int(splash::io::MapFormat::ExpansionScx));
    formatBox->addItem(tr("하이브리드 (.scm)"), int(splash::io::MapFormat::HybridScm));
    formatBox->addItem(tr("리마스터 (.scx)"), int(splash::io::MapFormat::RemasteredScx));

    auto * meleeBox = new QCheckBox(tr("기본 melee 트리거 넣기"), &dialog);
    meleeBox->setChecked(true);

    form->addRow(tr("크기 프리셋"), presetBox);
    form->addRow(tr("가로 (타일)"), widthBox);
    form->addRow(tr("세로 (타일)"), heightBox);
    form->addRow(tr("타일셋"), tilesetBox);
    form->addRow(tr("시작 지형"), terrainBox);
    form->addRow(tr("포맷"), formatBox);
    form->addRow(QString(), meleeBox);

    auto * buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto * layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto format =
        static_cast<splash::io::MapFormat>(formatBox->currentData().toInt());

    // 열어 둔 맵이 있으면 새 탭에 만든다.
    if (document().isOpen())
    {
        const int index = addDocumentTab();
        switchToDocument(index);
    }

    if (!document().createNew(format,
                             static_cast<std::uint16_t>(tilesetBox->currentData().toInt()),
                             static_cast<std::uint16_t>(widthBox->value()),
                             static_cast<std::uint16_t>(heightBox->value()),
                             meleeBox->isChecked(),
                             tileset_.isLoaded() ? &tileset_ : nullptr,
                             static_cast<std::size_t>(terrainBox->currentData().toULongLong())))
    {
        QMessageBox::warning(this, tr("새 맵 실패"),
                             QString::fromStdString(document().lastError()));
        return;
    }

    mapView_->setDocument(&document());
    mapView_->refresh();
    miniMap_->setDocument(&document());

    if (tilePalette_ != nullptr)
        tilePalette_->setTilesetId(document().info().tilesetId);
    if (unitPalette_ != nullptr)
        unitPalette_->setTilesetId(document().info().tilesetId);

    mapView_->clearSelection();
    mapView_->refresh();
    refreshFromDocument();
    statusBar()->showMessage(tr("새 맵을 만들었습니다"), 3000);
}

void MainWindow::onMapProperties()
{
    if (!document().isOpen())
        return;

    const auto & info = document().info();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("맵 속성"));
    dialog.resize(460, 320);

    auto * form = new QFormLayout();

    auto * nameEdit = new QLineEdit(QString::fromStdString(info.name), &dialog);
    auto * descEdit = new QPlainTextEdit(QString::fromStdString(info.description), &dialog);
    descEdit->setMaximumHeight(120);

    auto * tilesetBox = makeTilesetBox(&dialog, info.tilesetId);

    auto * widthBox = new QSpinBox(&dialog);
    widthBox->setRange(32, 256);
    widthBox->setValue(info.width);

    auto * heightBox = new QSpinBox(&dialog);
    heightBox->setRange(32, 256);
    heightBox->setValue(info.height);

    auto * warn = new QLabel(
        tr("크기를 바꾸면 실행 취소 이력이 지워집니다 — 여러 섹션을 한꺼번에 "
           "건드리므로 절반만 되돌리면 맵이 어긋납니다."), &dialog);
    warn->setWordWrap(true);

    form->addRow(tr("이름"), nameEdit);
    form->addRow(tr("설명"), descEdit);
    form->addRow(tr("타일셋"), tilesetBox);
    form->addRow(tr("가로 (타일)"), widthBox);
    form->addRow(tr("세로 (타일)"), heightBox);

    auto * buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto * layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(warn);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    bool changed = false;

    if (nameEdit->text().toStdString() != info.name)
        changed |= document().setScenarioName(nameEdit->text().toStdString());

    if (descEdit->toPlainText().toStdString() != info.description)
        changed |= document().setScenarioDescription(descEdit->toPlainText().toStdString());

    const auto newTileset = static_cast<std::uint16_t>(tilesetBox->currentData().toInt());
    if (newTileset != info.tilesetId)
    {
        changed |= document().setTileset(newTileset);
        if (tilePalette_ != nullptr)
            tilePalette_->setTilesetId(newTileset);
        if (unitPalette_ != nullptr)
            unitPalette_->setTilesetId(newTileset);
    }

    const auto newWidth = static_cast<std::uint16_t>(widthBox->value());
    const auto newHeight = static_cast<std::uint16_t>(heightBox->value());
    if (newWidth != info.width || newHeight != info.height)
        changed |= document().setDimensions(newWidth, newHeight);

    if (changed)
    {
        mapView_->clearSelection();
        mapView_->refresh();
        refreshFromDocument();
        statusBar()->showMessage(tr("맵 속성을 바꿨습니다"), 3000);
    }
}

void MainWindow::rememberRecentFile(const QString & path)
{
    if (path.isEmpty())
        return;

    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();

    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 8)
        recent.removeLast();

    settings.setValue(QStringLiteral("recentFiles"), recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (recentMenu_ == nullptr)
        return;

    recentMenu_->clear();

    const QStringList recent =
        QSettings().value(QStringLiteral("recentFiles")).toStringList();

    if (recent.isEmpty())
    {
        QAction * empty = recentMenu_->addAction(tr("(없음)"));
        empty->setEnabled(false);
        return;
    }

    for (const QString & path : recent)
    {
        // 메뉴에는 파일 이름만 보이고, 전체 경로는 툴팁으로 둔다.
        QAction * action = recentMenu_->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] {
            if (confirmDiscardChanges())
                openPath(path);
        });
    }

    recentMenu_->addSeparator();
    QAction * clear = recentMenu_->addAction(tr("목록 비우기"));
    connect(clear, &QAction::triggered, this, [this] {
        QSettings().remove(QStringLiteral("recentFiles"));
        rebuildRecentMenu();
    });
}

void MainWindow::onStringEditor()
{
    if (!document().isOpen())
        return;

    auto * editor = new StringEditor(document(), this);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    connect(editor, &StringEditor::documentEdited, this, [this] {
        mapView_->refresh();
        refreshFromDocument();
    });
    editor->show();
}

void MainWindow::onPlayerSettings()
{
    if (!document().isOpen())
        return;

    const auto settings = document().playerSettings();
    const auto forces = document().forceNames();
    if (settings.empty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("플레이어 설정"));
    dialog.resize(560, 560);

    auto * grid = new QGridLayout();
    grid->addWidget(new QLabel(tr("플레이어"), &dialog), 0, 0);
    grid->addWidget(new QLabel(tr("종족"), &dialog), 0, 1);
    grid->addWidget(new QLabel(tr("슬롯"), &dialog), 0, 2);
    grid->addWidget(new QLabel(tr("세력"), &dialog), 0, 3);
    grid->addWidget(new QLabel(tr("색"), &dialog), 0, 4);

    // Chk::Race 와 Sc::Player::SlotType 의 값들.
    const std::vector<std::pair<QString, int>> races {
        {tr("저그"), 0}, {tr("테란"), 1}, {tr("프로토스"), 2},
        {tr("독립"), 3}, {tr("중립"), 4}, {tr("선택 가능"), 5},
        {tr("무작위"), 6}, {tr("사용 안 함"), 7},
    };
    // 이름을 slots 로 두면 Qt 의 slots 매크로와 부딪힌다.
    const std::vector<std::pair<QString, int>> slotChoices {
        {tr("사용 안 함"), 0}, {tr("컴퓨터(게임)"), 1}, {tr("사람(게임)"), 2},
        {tr("구조 대상"), 3}, {tr("컴퓨터"), 5}, {tr("열림"), 6},
        {tr("중립"), 7}, {tr("닫힘"), 8},
    };

    std::vector<QComboBox *> raceBoxes(settings.size());
    std::vector<QComboBox *> slotBoxes(settings.size());
    std::vector<QComboBox *> forceBoxes(settings.size());
    std::vector<QComboBox *> colorBoxes(settings.size());

    for (std::size_t player = 0; player < settings.size(); ++player)
    {
        const int row = static_cast<int>(player) + 1;
        grid->addWidget(new QLabel(tr("%1").arg(player + 1), &dialog), row, 0);

        auto * raceBox = new QComboBox(&dialog);
        for (const auto & [name, value] : races)
            raceBox->addItem(name, value);
        raceBox->setCurrentIndex(
            static_cast<int>(std::distance(races.begin(),
                std::find_if(races.begin(), races.end(),
                    [&](const auto & e) { return e.second == settings[player].race; }))) %
            static_cast<int>(races.size()));
        grid->addWidget(raceBox, row, 1);
        raceBoxes[player] = raceBox;

        auto * slotBox = new QComboBox(&dialog);
        for (const auto & [name, value] : slotChoices)
            slotBox->addItem(name, value);
        const auto slotIt = std::find_if(slotChoices.begin(), slotChoices.end(),
            [&](const auto & e) { return e.second == settings[player].slotType; });
        if (slotIt != slotChoices.end())
            slotBox->setCurrentIndex(static_cast<int>(std::distance(slotChoices.begin(), slotIt)));
        grid->addWidget(slotBox, row, 2);
        slotBoxes[player] = slotBox;

        auto * forceBox = new QComboBox(&dialog);
        for (int force = 0; force < 4; ++force)
            forceBox->addItem(tr("세력 %1").arg(force + 1), force);
        forceBox->setCurrentIndex(std::min<int>(settings[player].force, 3));
        // 9~12번은 세력에 속하지 않는다(중립·구조물 자리).
        forceBox->setEnabled(player < 8);
        grid->addWidget(forceBox, row, 3);
        forceBoxes[player] = forceBox;

        // 색 — 목록에 실제 색을 칠해 둔다. 9번부터는 색 구역이 없다.
        auto * colorBox = new QComboBox(&dialog);
        for (const auto & entry : splash::io::playerColors())
        {
            QPixmap swatch(14, 14);
            swatch.fill(QColor(entry.red, entry.green, entry.blue));
            colorBox->addItem(QIcon(swatch), QString::fromStdString(entry.name), entry.value);
        }
        const int colorIndex = colorBox->findData(settings[player].color);
        colorBox->setCurrentIndex(colorIndex >= 0 ? colorIndex : 0);
        colorBox->setEnabled(player < 8);
        grid->addWidget(colorBox, row, 4);
        colorBoxes[player] = colorBox;
    }

    auto * forceGroup = new QGroupBox(tr("세력 이름"), &dialog);
    auto * forceForm = new QFormLayout(forceGroup);
    std::vector<QLineEdit *> forceEdits(4);
    for (int force = 0; force < 4; ++force)
    {
        auto * edit = new QLineEdit(
            force < static_cast<int>(forces.size())
                ? QString::fromStdString(forces[static_cast<std::size_t>(force)]) : QString(),
            forceGroup);
        forceForm->addRow(tr("세력 %1").arg(force + 1), edit);
        forceEdits[static_cast<std::size_t>(force)] = edit;
    }

    auto * buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto * layout = new QVBoxLayout(&dialog);
    layout->addLayout(grid);
    layout->addWidget(forceGroup);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    bool changed = false;
    for (std::size_t player = 0; player < settings.size(); ++player)
    {
        splash::io::PlayerSetting next;
        next.race = static_cast<std::uint8_t>(raceBoxes[player]->currentData().toInt());
        next.slotType = static_cast<std::uint8_t>(slotBoxes[player]->currentData().toInt());
        next.force = static_cast<std::uint8_t>(forceBoxes[player]->currentData().toInt());

        // 색 말고 다른 값은 그대로 이어 간다 (리마스터 색 설정 등).
        next.color = static_cast<std::uint8_t>(colorBoxes[player]->currentData().toInt());
        next.remasteredColors = settings[player].remasteredColors;
        next.colorSetting = settings[player].colorSetting;
        next.customRed = settings[player].customRed;
        next.customGreen = settings[player].customGreen;
        next.customBlue = settings[player].customBlue;

        if (next.race != settings[player].race ||
            next.slotType != settings[player].slotType ||
            (player < 8 && next.force != settings[player].force) ||
            (player < 8 && next.color != settings[player].color))
        {
            changed |= document().setPlayerSetting(player, next);
        }
    }

    for (int force = 0; force < 4; ++force)
    {
        const std::string name = forceEdits[static_cast<std::size_t>(force)]->text().toStdString();
        const std::string before = force < static_cast<int>(forces.size())
            ? forces[static_cast<std::size_t>(force)] : std::string();
        if (name != before)
            changed |= document().setForceName(static_cast<std::size_t>(force), name);
    }

    if (changed)
    {
        refreshFromDocument();
        statusBar()->showMessage(tr("플레이어 설정을 바꿨습니다"), 3000);
    }
}

void MainWindow::onUnitProperties()
{
    if (!document().isOpen() || mapView_ == nullptr)
        return;

    const int index = mapView_->selectedUnit();
    if (index < 0)
    {
        statusBar()->showMessage(tr("유닛을 먼저 고르세요."), 2000);
        return;
    }

    const auto current = document().unitProperties(static_cast<std::size_t>(index));
    if (!current)
        return;

    const auto & units = document().units();
    const auto & unit = units[static_cast<std::size_t>(index)];

    QDialog dialog(this);
    dialog.setWindowTitle(tr("유닛 속성 — %1").arg(QString::fromStdString(unit.typeName)));

    auto * form = new QFormLayout();

    auto * ownerBox = new QComboBox(&dialog);
    for (int player = 1; player <= 12; ++player)
        ownerBox->addItem(tr("플레이어 %1").arg(player), player - 1);
    ownerBox->setCurrentIndex(std::min<int>(current->owner, 11));

    const auto makePercent = [&dialog](int value) {
        auto * box = new QSpinBox(&dialog);
        box->setRange(0, 100);
        box->setSuffix(QStringLiteral(" %"));
        box->setValue(value);
        return box;
    };

    auto * hpBox = makePercent(current->hitpointPercent);
    auto * shieldBox = makePercent(current->shieldPercent);
    auto * energyBox = makePercent(current->energyPercent);

    auto * resourceBox = new QSpinBox(&dialog);
    resourceBox->setRange(0, 999999);
    resourceBox->setValue(static_cast<int>(current->resourceAmount));

    auto * hangarBox = new QSpinBox(&dialog);
    hangarBox->setRange(0, 255);
    hangarBox->setValue(current->hangarAmount);

    // 상태 비트는 Chk::Unit::State 의 값들이다.
    auto * cloakBox = new QCheckBox(tr("클로킹"), &dialog);
    cloakBox->setChecked(current->stateFlags & 0x01);
    auto * burrowBox = new QCheckBox(tr("버로우"), &dialog);
    burrowBox->setChecked(current->stateFlags & 0x02);
    auto * liftedBox = new QCheckBox(tr("떠 있음"), &dialog);
    liftedBox->setChecked(current->stateFlags & 0x04);
    auto * hallucinatedBox = new QCheckBox(tr("환영"), &dialog);
    hallucinatedBox->setChecked(current->stateFlags & 0x08);
    auto * invincibleBox = new QCheckBox(tr("무적"), &dialog);
    invincibleBox->setChecked(current->stateFlags & 0x10);

    form->addRow(tr("소유자"), ownerBox);
    form->addRow(tr("체력"), hpBox);
    form->addRow(tr("방어막"), shieldBox);
    form->addRow(tr("에너지"), energyBox);
    form->addRow(tr("자원"), resourceBox);
    form->addRow(tr("격납고"), hangarBox);
    form->addRow(tr("상태"), cloakBox);
    form->addRow(QString(), burrowBox);
    form->addRow(QString(), liftedBox);
    form->addRow(QString(), hallucinatedBox);
    form->addRow(QString(), invincibleBox);

    auto * buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto * layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    splash::io::UnitProperties next;
    next.owner = static_cast<std::uint8_t>(ownerBox->currentData().toInt());
    next.hitpointPercent = static_cast<std::uint8_t>(hpBox->value());
    next.shieldPercent = static_cast<std::uint8_t>(shieldBox->value());
    next.energyPercent = static_cast<std::uint8_t>(energyBox->value());
    next.resourceAmount = static_cast<std::uint32_t>(resourceBox->value());
    next.hangarAmount = static_cast<std::uint16_t>(hangarBox->value());
    next.stateFlags = static_cast<std::uint16_t>(
        (cloakBox->isChecked() ? 0x01 : 0) |
        (burrowBox->isChecked() ? 0x02 : 0) |
        (liftedBox->isChecked() ? 0x04 : 0) |
        (hallucinatedBox->isChecked() ? 0x08 : 0) |
        (invincibleBox->isChecked() ? 0x10 : 0));

    if (!document().setUnitProperties(static_cast<std::size_t>(index), next))
    {
        QMessageBox::warning(this, tr("유닛 속성 실패"),
                             QString::fromStdString(document().lastError()));
        return;
    }

    mapView_->refresh();
    refreshFromDocument();
    statusBar()->showMessage(tr("유닛 속성을 바꿨습니다"), 3000);
}

void MainWindow::onShowTriggers()
{
    if (!document().isOpen())
        return;

    if (!tileset_.hasUnitGraphics())
    {
        QMessageBox::information(this, tr("트리거"),
            tr("트리거를 글로 옮기려면 StarCraft 설치 폴더가 필요합니다.\n"
               "유닛과 업그레이드 이름표가 게임 데이터에 들어 있기 때문입니다.\n\n"
               "파일 › StarCraft 설치 폴더 지정…"));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto text = document().triggerText(tileset_);
    QApplication::restoreOverrideCursor();

    if (!text)
    {
        QMessageBox::warning(this, tr("트리거"), tr("트리거를 글로 옮기지 못했습니다."));
        return;
    }

    auto * dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("트리거 텍스트 — %1개").arg(document().info().triggerCount));
    dialog->resize(1000, 720);

    auto * layout = new QVBoxLayout(dialog);
    auto * editor = new CodeEditorPane(dialog);
    editor->setVocabulary(document().triggerVocabulary(tileset_));
    editor->setText(QString::fromStdString(*text));

    auto * buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Close, dialog);

    auto * hint = new QLabel(
        tr("고친 뒤 적용하면 트리거 전체가 새 내용으로 바뀝니다. "
           "적용 후에는 실행 취소 이력이 지워집니다."), dialog);
    hint->setWordWrap(true);

    layout->addWidget(editor, 1);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this, editor, dialog] {
        // 문법 검사가 잡은 오류가 있으면 먼저 알린다 — 컴파일러는 실패
        // 이유를 돌려주지 않아, 그냥 넘기면 무엇이 잘못됐는지 알 수 없다.
        if (editor->hasErrors())
        {
            const auto answer = QMessageBox::question(dialog, tr("트리거 적용"),
                tr("편집기가 문법 오류를 찾았습니다. 그래도 적용할까요?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }

        const QString edited = editor->text();

        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = document().applyTriggerText(edited.toStdString(), tileset_);
        QApplication::restoreOverrideCursor();

        if (!ok)
        {
            QMessageBox::warning(dialog, tr("트리거 적용 실패"),
                                 QString::fromStdString(document().lastError()));
            return;
        }

        mapView_->refresh();
        refreshFromDocument();
        statusBar()->showMessage(
            tr("트리거를 적용했습니다 — %1개").arg(document().info().triggerCount), 4000);
        dialog->close();
    });

    dialog->show();
}

void MainWindow::onChooseInstallPath()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("StarCraft 설치 폴더 선택"),
        QSettings().value(kInstallPathKey).toString());

    if (path.isEmpty())
        return;

    loadTilesetFrom(path, /*announce*/ true);
}

void MainWindow::useInstallPath(const QString & installPath)
{
    loadTilesetFrom(installPath, /*announce*/ true);
}

void MainWindow::loadTilesetFrom(const QString & installPath, bool announce)
{
    if (installPath.isEmpty())
        return;

    // CASC 인덱스를 읽느라 몇 초가 걸릴 수 있다.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::string error;
    const bool ok = tileset_.load(installPath.toStdString(), &error);
    QApplication::restoreOverrideCursor();

    if (!ok)
    {
        if (announce)
        {
            QMessageBox::warning(this, tr("타일셋 로드 실패"),
                                 QString::fromStdString(error));
        }
        return;
    }

    QSettings().setValue(kInstallPathKey, installPath);
    mapView_->refresh();
    // 게임 데이터가 이제 막 로드됐다. 팔레트는 그것 없이는 비어 있었으므로
    // 타일셋을 다시 알려 목록을 채우게 한다.
    if (tilePalette_ != nullptr)
    {
        tilePalette_->setTileset(&tileset_);
        if (document().isOpen())
            tilePalette_->setTilesetId(document().info().tilesetId);
    }
    if (unitPalette_ != nullptr)
    {
        unitPalette_->setTileset(&tileset_);
        if (document().isOpen())
            unitPalette_->setTilesetId(document().info().tilesetId);
    }

    if (announce)
        statusBar()->showMessage(tr("타일셋을 읽었습니다: %1").arg(installPath), 4000);
}

void MainWindow::openPath(const QString & path)
{
    if (path.isEmpty())
        return;

    // 지금 탭에 이미 맵이 있으면 새 탭에 연다 — 열어 둔 맵을 밀어내지 않는다.
    if (document().isOpen())
    {
        const int index = addDocumentTab();
        switchToDocument(index);
    }

    if (!document().open(path.toStdString()))
    {
        QMessageBox::warning(this, tr("열기 실패"),
                             QString::fromStdString(document().lastError()));
        refreshFromDocument();
        return;
    }

    if (tilePalette_ != nullptr)
        tilePalette_->setTilesetId(document().info().tilesetId);
    if (unitPalette_ != nullptr)
        unitPalette_->setTilesetId(document().info().tilesetId);

    rememberRecentFile(path);
    mapView_->setDocument(&document());
    mapView_->refresh();
    miniMap_->setDocument(&document());
    refreshFromDocument();
    statusBar()->showMessage(tr("열었습니다: %1").arg(path), 4000);
}

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("맵 열기"), QString(), mapFilter());
    openPath(path);
}

void MainWindow::onSave()
{
    if (!document().isOpen())
        return;

    // 새로 만든 맵은 저장된 적이 없다 — 어디에 쓸지 물어야 한다.
    if (document().filePath().empty())
    {
        onSaveAs();
        return;
    }

    if (!document().save())
    {
        QMessageBox::warning(this, tr("저장 실패"),
                             QString::fromStdString(document().lastError()));
        return;
    }

    refreshFromDocument();
    statusBar()->showMessage(
        tr("저장했습니다: %1").arg(QString::fromStdString(document().filePath())), 4000);
}

void MainWindow::onSaveAs()
{
    if (!document().isOpen())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("다른 이름으로 저장"),
        QString::fromStdString(document().filePath()), mapFilter());
    if (path.isEmpty())
        return;

    if (!document().saveAs(path.toStdString()))
    {
        QMessageBox::warning(this, tr("저장 실패"),
                             QString::fromStdString(document().lastError()));
        return;
    }

    refreshFromDocument();
    statusBar()->showMessage(tr("저장했습니다: %1").arg(path), 4000);
}

void MainWindow::onClose()
{
    if (!confirmDiscardChanges())
        return;

    // 탭을 닫는다. 마지막 하나면 비운 채로 남는다.
    closeDocumentTab(currentDocument_);
}

bool MainWindow::confirmDiscardChanges()
{
    if (!document().isOpen() || !document().isModified())
        return true;

    const auto choice = QMessageBox::question(
        this, tr("저장하지 않은 변경"),
        tr("저장하지 않은 변경이 있습니다. 저장할까요?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Discard)
        return true;

    onSave();
    return !document().isModified();
}

void MainWindow::closeEvent(QCloseEvent * event)
{
    // 탭마다 저장하지 않은 것이 있는지 묻는다.
    const int wasOn = currentDocument_;
    for (int i = 0; i < static_cast<int>(documents_.size()); ++i)
    {
        switchToDocument(i);
        if (!confirmDiscardChanges())
        {
            switchToDocument(wasOn);
            event->ignore();
            return;
        }
    }

    event->accept();
}

void MainWindow::refreshFromDocument()
{
    refreshTabText(currentDocument_);

    const bool open = document().isOpen();

    if (mapView_ != nullptr)
        mapView_->refresh();
    if (miniMap_ != nullptr)
    {
        miniMap_->refresh();
        miniMap_->setViewportRect(mapView_->visibleMapRect());
    }

    saveAction_->setEnabled(open);
    saveAsAction_->setEnabled(open);
    closeAction_->setEnabled(open);

    if (undoAction_ != nullptr)
        undoAction_->setEnabled(open && document().canUndo());
    if (redoAction_ != nullptr)
        redoAction_->setEnabled(open && document().canRedo());
    if (deleteAction_ != nullptr)
        deleteAction_->setEnabled(open && mapView_ != nullptr && mapView_->selectedUnit() >= 0);

    if (!open)
    {
        for (QLabel * label : {nameValue_, sizeValue_, tilesetValue_,
                               versionValue_, countsValue_, pathValue_})
        {
            label->setText(QStringLiteral("—"));
        }
        updateWindowTitle();
        return;
    }

    const auto & info = document().info();

    nameValue_->setText(orDash(info.name));
    sizeValue_->setText(tr("%1 × %2 타일").arg(info.width).arg(info.height));
    tilesetValue_->setText(QStringLiteral("%1 (%2)")
        .arg(QString::fromStdString(info.tilesetName))
        .arg(info.tilesetId));
    versionValue_->setText(QStringLiteral("%1 (%2)")
        .arg(QString::fromStdString(info.versionName))
        .arg(info.versionId));
    countsValue_->setText(tr("유닛 %1 · 로케이션 %2 · 트리거 %3 · 문자열 %4")
        .arg(info.unitCount)
        .arg(info.locationCount)
        .arg(info.triggerCount)
        .arg(info.stringCount));
    pathValue_->setText(orDash(document().filePath()));

    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    if (!document().isOpen())
    {
        setWindowTitle(QString::fromLatin1(kAppName));
        return;
    }

    const QString fileName = QString::fromStdString(document().fileName());
    setWindowTitle(QStringLiteral("%1%2 — %3")
        .arg(fileName)
        .arg(document().isModified() ? QStringLiteral("*") : QString())
        .arg(QString::fromLatin1(kAppName)));
}

} // namespace splash::ui
