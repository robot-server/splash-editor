#pragma once

// 단일 QMainWindow. 도킹/멀티맵은 M2 이후.
//
// UI 는 splash::chk::MapDocument 만 안다. MappingCore 타입은 여기 등장하지 않는다.

#include "chk/map_document.h"

#include <QMainWindow>

class QLabel;

namespace splash::ui {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget * parent = nullptr);
    ~MainWindow() override;

    /// 커맨드라인 등으로 지정된 맵을 연다.
    void openPath(const QString & path);

protected:
    void closeEvent(QCloseEvent * event) override;

private slots:
    void onOpen();
    void onSave();
    void onSaveAs();
    void onClose();

private:
    void buildMenus();
    void buildCentralWidget();
    void refreshFromDocument();
    void updateWindowTitle();

    /// 저장되지 않은 변경이 있으면 사용자에게 묻는다.
    /// 계속 진행해도 되면 true.
    bool confirmDiscardChanges();

    chk::MapDocument document_;

    QLabel * nameValue_ = nullptr;
    QLabel * sizeValue_ = nullptr;
    QLabel * tilesetValue_ = nullptr;
    QLabel * versionValue_ = nullptr;
    QLabel * countsValue_ = nullptr;
    QLabel * pathValue_ = nullptr;

    QAction * saveAction_ = nullptr;
    QAction * saveAsAction_ = nullptr;
    QAction * closeAction_ = nullptr;
};

} // namespace splash::ui
