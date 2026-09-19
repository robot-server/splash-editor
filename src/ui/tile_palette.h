#pragma once

// 지형 타일을 골라 쓰는 팔레트.
//
// 타일셋 하나에 수천 개의 타일이 있으므로 보이는 것만 그리고 캐시한다.
// MapView 와 같은 방침이다.

#include <QAbstractScrollArea>
#include <QHash>
#include <QPixmap>

#include <cstdint>
#include <vector>

namespace splash::io { class GameGraphics; }

namespace splash::ui {

class TilePalette : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TilePalette(QWidget * parent = nullptr);
    ~TilePalette() override;

    /// 타일셋 그래픽. 소유하지 않는다.
    void setTileset(const io::GameGraphics * tileset);

    /// 어떤 타일셋의 타일을 늘어놓을지 (CHK 의 ERA 원시값).
    void setTilesetId(std::uint16_t tilesetId);

    /// 지금 고른 타일. 없으면 0.
    std::uint16_t selectedTile() const { return selectedTile_; }
    void setSelectedTile(std::uint16_t tileId);

signals:
    /// 팔레트에서 타일을 골랐다.
    void tileSelected(std::uint16_t tileId);

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;

private:
    void rebuild();
    void updateScrollRange();

    int columns() const;
    const QPixmap * tilePixmap(std::uint16_t tileId);

    const io::GameGraphics * tileset_ = nullptr;
    std::uint16_t tilesetId_ = 0;
    std::uint16_t selectedTile_ = 0;

    std::vector<std::uint16_t> tiles_;
    QHash<std::uint16_t, QPixmap> cache_;
};

} // namespace splash::ui
