#include "ui/main_window.h"

#include "ui/map_view.h"

#include <QAction>
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
#include <QMessageBox>
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
