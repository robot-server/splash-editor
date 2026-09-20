#pragma once

// 맵에 놓을 유닛을 고르는 팔레트.
//
// 유닛 스프라이트는 크기가 제각각이라 칸에 맞춰 줄여 그린다.
// 228종을 한 번에 그리면 느리므로 보이는 것만 만들고 캐시한다.

#include <QAbstractScrollArea>
#include <QString>
#include <QHash>
#include <QPixmap>

#include <cstdint>
#include <vector>

namespace splash::io { class GameGraphics; }

namespace splash::ui {

class UnitPalette : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit UnitPalette(QWidget * parent = nullptr);
    ~UnitPalette() override;

    void setTileset(const io::GameGraphics * tileset);

    /// 미리보기에 쓸 타일셋(팔레트 색이 지형을 따른다).
    void setTilesetId(std::uint16_t tilesetId);

    /// 담아 둔 그림을 버리고 다시 그린다.
    void clearCache();

    /// 팔레트에 무엇을 늘어놓을지.
    enum class Category
    {
        All,
        TerranUnits, TerranBuildings,
        ZergUnits,   ZergBuildings,
        ProtossUnits, ProtossBuildings,
        Neutral,
        Sprites   ///< 맵 장식 스프라이트 (유닛이 아니다)
    };
    void setCategory(Category category);
    Category category() const { return category_; }

    /// 카테고리 이름 (콤보를 채울 때 쓴다).
    static QString categoryName(Category category);

    std::uint16_t selectedUnit() const { return selectedUnit_; }
    std::uint8_t owner() const { return owner_; }
    void setOwner(std::uint8_t owner);

    /// 그 플레이어를 칠할 색 번호 (COLR). 0xFF 면 플레이어 번호를 쓴다.
    void setPlayerColor(std::uint8_t colorIndex);

    /// 지금 고른 것이 스프라이트인지(유닛이 아니라).
    bool spriteMode() const { return category_ == Category::Sprites; }

    /// 이름에 이 글자가 든 것만 보여 준다. 빈 문자열이면 모두 보여 준다.
    void setFilter(const QString & text);

signals:
    void unitSelected(std::uint16_t unitType);
    void spriteSelected(std::uint16_t spriteType);

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;

private:
    void rebuild();
    QString filter_;
    void updateScrollRange();
    int columns() const;
    const QPixmap * unitPixmap(std::uint16_t unitType);

    const io::GameGraphics * tileset_ = nullptr;
    std::uint16_t tilesetId_ = 0;
    std::uint16_t selectedUnit_ = 0;
    std::uint8_t owner_ = 0;
    std::uint8_t colorIndex_ = 0xFF;

    Category category_ = Category::All;
    std::vector<std::uint16_t> units_;
    QHash<std::uint32_t, QPixmap> cache_; ///< (타입<<8 | 소유자)
};

} // namespace splash::ui
