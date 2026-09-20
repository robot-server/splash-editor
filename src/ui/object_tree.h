#pragma once

#include <QDockWidget>
#include <cstddef>
#include <cstdint>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

namespace splash::chk { class MapDocument; }
namespace splash::io { class GameGraphics; }

namespace splash::ui {

/// 맵에 놓인 것들을 종류별로 늘어놓는 나무.
///
/// 화면에서 눈으로 찾기 어려운 것 — 지형에 묻힌 두들, 겹쳐 놓은 유닛,
/// 맵 구석의 스프라이트 — 을 목록에서 짚어 갈 수 있게 한다.
class ObjectTree : public QDockWidget
{
    Q_OBJECT

public:
    explicit ObjectTree(QWidget * parent = nullptr);

    /// 소유하지 않는다 — 호출부가 수명을 관리한다.
    void setDocument(const chk::MapDocument * document);

    /// 두들 이름을 읽는 데 쓴다. 없어도 목록은 나온다.
    void setTileset(const io::GameGraphics * tileset);

    /// 맵이 바뀌었을 때 다시 채운다.
    void refresh();

signals:
    void unitPicked(std::size_t index);
    void spritePicked(std::size_t index);
    void doodadPicked(std::size_t index);
    void locationPicked(std::size_t index);

private:
    void applyFilter();

    const chk::MapDocument * document_ = nullptr;
    const io::GameGraphics * tileset_ = nullptr;
    QTreeWidget * tree_ = nullptr;
    QLineEdit * search_ = nullptr;
};

} // namespace splash::ui
