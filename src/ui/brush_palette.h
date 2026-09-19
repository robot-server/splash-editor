#pragma once

// 지형 브러시 팔레트.
//
// 고른 지형 덩어리(확장 램프 같은 것)를 이름 붙여 두고 다시 꺼내 쓴다.
// 맵을 옮겨 다녀도 남도록 파일로 저장한다.
//
// 파일은 우리 포맷(.splashbrush)이다. SCMDraft 의 .scb 는 공개 스펙이
// 없고 실행 파일 역공학은 프로젝트 제약에서 금지라, 같은 확장자를 써서
// 그쪽 파일인 척하지 않는다.

#include <QWidget>

#include <vector>

#include "ui/map_view.h"

class QListWidget;
class QLabel;

namespace splash::ui {

class BrushPalette : public QWidget
{
    Q_OBJECT

public:
    explicit BrushPalette(QWidget * parent = nullptr);

    /// 담아 둔 지형을 브러시로 더한다. 이름은 사용자에게 묻는다.
    void addFromClipboard(const MapView::TerrainBrush & brush);

signals:
    /// 브러시를 골랐다 — 곧바로 찍을 수 있게 하라는 뜻.
    void brushChosen(const MapView::TerrainBrush & brush);

private:
    void reloadList();
    void removeSelected();
    void saveToFile();
    void loadFromFile();

    /// 지난번에 쓰던 브러시를 자동으로 읽고 쓴다.
    QString defaultPath() const;
    void loadDefault();
    void saveDefault() const;

    bool writeFile(const QString & path) const;
    bool readFile(const QString & path, bool append);

    std::vector<MapView::TerrainBrush> brushes_;

    QListWidget * list_ = nullptr;
    QLabel * status_ = nullptr;
};

} // namespace splash::ui
