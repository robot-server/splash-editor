#pragma once

// 맵에 놓을 유닛을 고르는 팔레트.
//
// 유닛 스프라이트는 크기가 제각각이라 칸에 맞춰 줄여 그린다.
// 228종을 한 번에 그리면 느리므로 보이는 것만 만들고 캐시한다.

#include <QAbstractScrollArea>
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

    std::uint16_t selectedUnit() const { return selectedUnit_; }
    std::uint8_t owner() const { return owner_; }
    void setOwner(std::uint8_t owner);

signals:
    void unitSelected(std::uint16_t unitType);

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;

private:
    void rebuild();
    void updateScrollRange();
    int columns() const;
    const QPixmap * unitPixmap(std::uint16_t unitType);

    const io::GameGraphics * tileset_ = nullptr;
    std::uint16_t tilesetId_ = 0;
    std::uint16_t selectedUnit_ = 0;
    std::uint8_t owner_ = 0;

    std::vector<std::uint16_t> units_;
    QHash<std::uint32_t, QPixmap> cache_; ///< (타입<<8 | 소유자)
};

} // namespace splash::ui
