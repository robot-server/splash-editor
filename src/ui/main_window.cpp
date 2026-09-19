#include "ui/main_window.h"

#include <QAction>
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
    resize(560, 360);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildCentralWidget()
{
    auto * central = new QWidget(this);
    auto * outer = new QVBoxLayout(central);

    auto * form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    nameValue_    = new QLabel(central);
    sizeValue_    = new QLabel(central);
    tilesetValue_ = new QLabel(central);
    versionValue_ = new QLabel(central);
    countsValue_  = new QLabel(central);
    pathValue_    = new QLabel(central);

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

    setCentralWidget(central);
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

    QAction * quitAction = fileMenu->addAction(tr("종료(&Q)"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
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
