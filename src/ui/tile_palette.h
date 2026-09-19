#pragma once

// 지형 타일을 골라 쓰는 팔레트.
//
// 타일셋 하나에 수천 개의 타일이 있으므로 보이는 것만 그리고 캐시한다.
// MapView 와 같은 방침이다.

#include <QAbstractScrollArea>
#include <QHash>
#include <QPixmap>

#include <cstdint>
#include <utility>
#include <vector>

#include <QString>

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

    /// 지형 종류 목록을 보여 줄지(ISOM 모드), 타일 격자를 보여 줄지.
    void setTerrainTypeMode(bool on);
    bool terrainTypeMode() const { return terrainTypeMode_; }

    /// 두들 목록을 보여 줄지. 켜면 지형 종류 모드보다 우선한다.
    void setDoodadMode(bool on);
    bool doodadMode() const { return doodadMode_; }

signals:
    /// 팔레트에서 타일을 골랐다.
    void tileSelected(std::uint16_t tileId);

    /// ISOM 모드에서 지형 종류를 골랐다 (brushIndex).
    void terrainTypeSelected(std::size_t brushIndex);

    /// 두들을 골랐다 (dddata.bin 번호).
    void doodadSelected(std::uint16_t doodadId);

protected:
    void paintEvent(QPaintEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void mousePressEvent(QMouseEvent * event) override;

private:
    bool listMode() const; ///< 이름을 줄줄이 보여 주는 모드인지
    void rebuild();
    void updateScrollRange();

    int columns() const;
    const QPixmap * tilePixmap(std::uint16_t tileId);

    const io::GameGraphics * tileset_ = nullptr;
    std::uint16_t tilesetId_ = 0;
    std::uint16_t selectedTile_ = 0;

    bool terrainTypeMode_ = false;
    bool doodadMode_ = false;
    int selectedTerrainRow_ = 0;
    struct TerrainEntry
    {
        std::size_t brushIndex = 0;
        QString name;
        std::uint16_t previewTileId = 0;
        bool hasPreview = false;
        std::uint16_t doodadId = 0; ///< 두들 모드에서 쓴다
        QString detail;             ///< "3 x 2" 처럼 크기를 적어 둔다
    };
    std::vector<TerrainEntry> terrainTypes_;
    std::vector<std::uint16_t> tiles_;
    QHash<std::uint16_t, QPixmap> cache_;
};

} // namespace splash::ui
