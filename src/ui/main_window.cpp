#include "ui/main_window.h"

#include "ui/map_view.h"
#include "ui/tile_palette.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QGroupBox>
#include <QSettings>
#include <QSplitter>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
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
    buildCentralWidget();
    buildMenus();
    refreshFromDocument();
    resize(960, 640);

    // 지난번에 지정한 설치 폴더가 있으면 조용히 읽는다.
    loadTilesetFrom(QSettings().value(kInstallPathKey).toString(), /*announce*/ false);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildCentralWidget()
{
    mapView_ = new MapView(this);
    mapView_->setDocument(&document_);
    mapView_->setTileset(&tileset_);

    connect(mapView_, &MapView::documentEdited, this, &MainWindow::onDocumentEdited);
    connect(mapView_, &MapView::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(mapView_, &MapView::brushTileChanged, this, [this](std::uint16_t tileId) {
        // 맵에서 스포이드로 집으면 팔레트 선택도 따라간다.
        if (tilePalette_ != nullptr)
            tilePalette_->setSelectedTile(tileId);
        statusBar()->showMessage(tr("브러시 타일: %1").arg(tileId), 3000);
    });

    auto * side = new QWidget(this);
    auto * outer = new QVBoxLayout(side);

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

    setCentralWidget(splitter);

    // 지형 타일 팔레트는 도크로 둔다 — 지형 작업을 할 때만 열어 두면 된다.
    tilePalette_ = new TilePalette(this);
    tilePalette_->setTileset(&tileset_);

    paletteDock_ = new QDockWidget(tr("타일 팔레트"), this);
    paletteDock_->setWidget(tilePalette_);
    paletteDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock_);
    paletteDock_->hide(); // 지형 도구를 고를 때 열린다

    // 팔레트에서 타일을 고르면 브러시가 되고, 지형 도구로 넘어간다.
    connect(tilePalette_, &TilePalette::tileSelected, this, [this](std::uint16_t tileId) {
        mapView_->setBrushTile(tileId);
        mapView_->setTool(MapView::Tool::Terrain);
        statusBar()->showMessage(tr("브러시 타일: %1").arg(tileId), 2000);
    });

    statusBar();
}

void MainWindow::buildMenus()
{
    QMenu * fileMenu = menuBar()->addMenu(tr("파일(&F)"));

    QAction * openAction = fileMenu->addAction(tr("열기(&O)…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);

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

    deleteAction_ = editMenu->addAction(tr("선택 삭제(&D)"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::onDeleteSelection);

    QMenu * toolMenu = menuBar()->addMenu(tr("도구(&T)"));

    auto * toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    QAction * selectTool = toolMenu->addAction(tr("선택(&S)"));
    selectTool->setCheckable(true);
    selectTool->setChecked(true);
    selectTool->setShortcut(QKeySequence(Qt::Key_S));
    toolGroup->addAction(selectTool);
    connect(selectTool, &QAction::triggered, this,
            [this] { mapView_->setTool(MapView::Tool::Select);
                     statusBar()->showMessage(tr("선택 도구"), 2000); });

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

    QMenu * triggerMenu = menuBar()->addMenu(tr("트리거(&R)"));
    QAction * showTriggers = triggerMenu->addAction(tr("트리거 보기(&V)…"));
    showTriggers->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(showTriggers, &QAction::triggered, this, &MainWindow::onShowTriggers);

    toolMenu->addSeparator();
    QAction * showPalette = toolMenu->addAction(tr("타일 팔레트(&P)"));
    showPalette->setCheckable(true);
    connect(showPalette, &QAction::toggled, this, [this](bool on) {
        if (paletteDock_ != nullptr)
            paletteDock_->setVisible(on);
    });
    connect(paletteDock_, &QDockWidget::visibilityChanged, showPalette, &QAction::setChecked);

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

    QAction * showCreep = viewMenu->addAction(tr("크립 표시(&C)"));
    showCreep->setCheckable(true);
    showCreep->setChecked(mapView_->creepVisible());
    showCreep->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    connect(showCreep, &QAction::toggled, mapView_, &MapView::setCreepVisible);
}

void MainWindow::onUndo()
{
    if (!document_.undo())
    {
        statusBar()->showMessage(QString::fromStdString(document_.lastError()), 3000);
        return;
    }
    mapView_->clearSelection();
    mapView_->refresh();
    refreshFromDocument();
    statusBar()->showMessage(tr("실행 취소"), 2000);
}

void MainWindow::onRedo()
{
    if (!document_.redo())
    {
        statusBar()->showMessage(QString::fromStdString(document_.lastError()), 3000);
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
        const auto & locations = document_.locations();
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

    const auto & units = document_.units();
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

void MainWindow::onShowTriggers()
{
    if (!document_.isOpen())
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
    const auto text = document_.triggerText(tileset_);
    QApplication::restoreOverrideCursor();

    if (!text)
    {
        QMessageBox::warning(this, tr("트리거"), tr("트리거를 글로 옮기지 못했습니다."));
        return;
    }

    auto * dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("트리거 — %1개").arg(document_.info().triggerCount));
    dialog->resize(760, 560);

    auto * layout = new QVBoxLayout(dialog);
    auto * editor = new QPlainTextEdit(dialog);
    editor->setPlainText(QString::fromStdString(*text));

    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    editor->setFont(mono);

    auto * buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Close, dialog);

    auto * hint = new QLabel(
        tr("고친 뒤 적용하면 트리거 전체가 새 내용으로 바뀝니다. "
           "적용 후에는 실행 취소 이력이 지워집니다."), dialog);
    hint->setWordWrap(true);

    layout->addWidget(editor);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this, editor, dialog] {
        const QString edited = editor->toPlainText();

        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = document_.applyTriggerText(edited.toStdString(), tileset_);
        QApplication::restoreOverrideCursor();

        if (!ok)
        {
            QMessageBox::warning(dialog, tr("트리거 적용 실패"),
                                 QString::fromStdString(document_.lastError()));
            return;
        }

        mapView_->refresh();
        refreshFromDocument();
        statusBar()->showMessage(
            tr("트리거를 적용했습니다 — %1개").arg(document_.info().triggerCount), 4000);
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
    if (tilePalette_ != nullptr)
        tilePalette_->setTileset(&tileset_);

    if (announce)
        statusBar()->showMessage(tr("타일셋을 읽었습니다: %1").arg(installPath), 4000);
}

void MainWindow::openPath(const QString & path)
{
    if (path.isEmpty())
        return;

    if (!document_.open(path.toStdString()))
    {
        QMessageBox::warning(this, tr("열기 실패"),
                             QString::fromStdString(document_.lastError()));
        refreshFromDocument();
        return;
    }

    if (tilePalette_ != nullptr)
        tilePalette_->setTilesetId(document_.info().tilesetId);

    refreshFromDocument();
    statusBar()->showMessage(tr("열었습니다: %1").arg(path), 4000);
}

void MainWindow::onOpen()
{
    if (!confirmDiscardChanges())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("맵 열기"), QString(), mapFilter());
    openPath(path);
}

void MainWindow::onSave()
{
    if (!document_.isOpen())
        return;

    if (!document_.save())
    {
        QMessageBox::warning(this, tr("저장 실패"),
                             QString::fromStdString(document_.lastError()));
        return;
    }

    refreshFromDocument();
    statusBar()->showMessage(
        tr("저장했습니다: %1").arg(QString::fromStdString(document_.filePath())), 4000);
}

void MainWindow::onSaveAs()
{
    if (!document_.isOpen())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("다른 이름으로 저장"),
        QString::fromStdString(document_.filePath()), mapFilter());
    if (path.isEmpty())
        return;

    if (!document_.saveAs(path.toStdString()))
    {
        QMessageBox::warning(this, tr("저장 실패"),
                             QString::fromStdString(document_.lastError()));
        return;
    }

    refreshFromDocument();
    statusBar()->showMessage(tr("저장했습니다: %1").arg(path), 4000);
}

void MainWindow::onClose()
{
    if (!confirmDiscardChanges())
        return;

    document_.close();
    refreshFromDocument();
}

bool MainWindow::confirmDiscardChanges()
{
    if (!document_.isOpen() || !document_.isModified())
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
    return !document_.isModified();
}

void MainWindow::closeEvent(QCloseEvent * event)
{
    if (confirmDiscardChanges())
        event->accept();
    else
        event->ignore();
}

void MainWindow::refreshFromDocument()
{
    const bool open = document_.isOpen();

    if (mapView_ != nullptr)
        mapView_->refresh();

    saveAction_->setEnabled(open);
    saveAsAction_->setEnabled(open);
    closeAction_->setEnabled(open);

    if (undoAction_ != nullptr)
        undoAction_->setEnabled(open && document_.canUndo());
    if (redoAction_ != nullptr)
        redoAction_->setEnabled(open && document_.canRedo());
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

    const auto & info = document_.info();

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
    pathValue_->setText(orDash(document_.filePath()));

    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    if (!document_.isOpen())
    {
        setWindowTitle(QString::fromLatin1(kAppName));
        return;
    }

    const QString fileName = QString::fromStdString(document_.fileName());
    setWindowTitle(QStringLiteral("%1%2 — %3")
        .arg(fileName)
        .arg(document_.isModified() ? QStringLiteral("*") : QString())
        .arg(QString::fromLatin1(kAppName)));
}

} // namespace splash::ui
