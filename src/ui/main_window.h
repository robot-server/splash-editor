#pragma once

// 단일 QMainWindow. 도킹/멀티맵은 M2 이후.
//
// UI 는 splash::chk::MapDocument 만 안다. MappingCore 타입은 여기 등장하지 않는다.

#include "chk/map_document.h"
#include "io/game_graphics.h"

#include <QMainWindow>

class QDockWidget;
class QLabel;

namespace splash::ui {

class MapView;
class TilePalette;
class UnitPalette;
class MiniMap;
class SoundPlayer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget * parent = nullptr);
    ~MainWindow() override;

    /// 커맨드라인 등으로 지정된 맵을 연다.
    void openPath(const QString & path);

    /// 설치 폴더를 타일셋 소스로 삼고 기억한다.
    void useInstallPath(const QString & installPath);

protected:
    void closeEvent(QCloseEvent * event) override;

private slots:
    void onOpen();
    void onSave();
    void onSaveAs();
    void onClose();
    void onChooseInstallPath();
    void onUndo();
    void onRedo();
    void onDeleteSelection();
    void onDocumentEdited();
    void onSelectionChanged(int unitIndex);
    void onShowTriggers();
    void onNewMap();
    void onMapProperties();
    void onUnitProperties();
    void onPlayerSettings();

private:
    void buildMenus();
    void buildCentralWidget();
    void refreshFromDocument();
    void updateWindowTitle();

    /// 저장된 설치 경로로 타일셋을 읽는다. 경로가 없거나 실패하면 조용히 넘어간다
    /// (지형 대신 안내 문구가 뜬다).
    void loadTilesetFrom(const QString & installPath, bool announce);

    /// 저장되지 않은 변경이 있으면 사용자에게 묻는다.
    /// 계속 진행해도 되면 true.
    bool confirmDiscardChanges();

    chk::MapDocument document_;
    io::GameGraphics tileset_;
    MapView * mapView_ = nullptr;
    TilePalette * tilePalette_ = nullptr;
    QDockWidget * paletteDock_ = nullptr;
    UnitPalette * unitPalette_ = nullptr;
    QDockWidget * unitDock_ = nullptr;
    MiniMap * miniMap_ = nullptr;
    SoundPlayer * soundPlayer_ = nullptr;

    QLabel * nameValue_ = nullptr;
    QLabel * sizeValue_ = nullptr;
    QLabel * tilesetValue_ = nullptr;
    QLabel * versionValue_ = nullptr;
    QLabel * countsValue_ = nullptr;
    QLabel * pathValue_ = nullptr;

    QAction * saveAction_ = nullptr;
    QAction * saveAsAction_ = nullptr;
    QAction * closeAction_ = nullptr;
    QAction * zoomInAction_ = nullptr;
    QAction * zoomOutAction_ = nullptr;
    QAction * zoomResetAction_ = nullptr;
    QAction * undoAction_ = nullptr;
    QAction * redoAction_ = nullptr;
    QAction * deleteAction_ = nullptr;
};

} // namespace splash::ui
