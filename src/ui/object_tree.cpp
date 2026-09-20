#include "ui/object_tree.h"

#include "chk/map_document.h"
#include "io/game_graphics.h"
#include "io/map_archive.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <QWidget>

namespace splash::ui {
namespace {

/// 항목이 무엇을 가리키는지 — 나무 항목에 함께 담아 둔다.
enum Role
{
    KindRole = Qt::UserRole,
    IndexRole = Qt::UserRole + 1,
};

enum class Kind
{
    Group,
    Unit,
    Sprite,
    Doodad,
    Location,
};

QTreeWidgetItem * makeGroup(QTreeWidget * tree, const QString & title)
{
    auto * group = new QTreeWidgetItem(tree);
    group->setText(0, title);
    group->setData(0, KindRole, static_cast<int>(Kind::Group));
    group->setFirstColumnSpanned(true);
    return group;
}

QTreeWidgetItem * makeLeaf(QTreeWidgetItem * parent, Kind kind, std::size_t index,
                           const QString & name, const QString & detail)
{
    auto * item = new QTreeWidgetItem(parent);
    item->setText(0, name);
    item->setText(1, detail);
    item->setData(0, KindRole, static_cast<int>(kind));
    item->setData(0, IndexRole, static_cast<qulonglong>(index));
    return item;
}

QString tileText(int pixelX, int pixelY)
{
    return QObject::tr("%1, %2").arg(pixelX / 32).arg(pixelY / 32);
}

} // namespace

ObjectTree::ObjectTree(QWidget * parent)
    : QDockWidget(tr("오브젝트"), parent)
{
    auto * panel = new QWidget(this);
    auto * layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    search_ = new QLineEdit(panel);
    search_->setPlaceholderText(tr("이름으로 거르기"));
    search_->setClearButtonEnabled(true);

    tree_ = new QTreeWidget(panel);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("이름"), tr("자리(타일)")});
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);

    layout->addWidget(search_);
    layout->addWidget(tree_, 1);
    setWidget(panel);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString &) {
        applyFilter();
    });

    connect(tree_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem * item, int) {
        if (item == nullptr)
            return;

        const auto kind = static_cast<Kind>(item->data(0, KindRole).toInt());
        const auto index = static_cast<std::size_t>(item->data(0, IndexRole).toULongLong());

        switch (kind)
        {
            case Kind::Unit:     emit unitPicked(index); break;
            case Kind::Sprite:   emit spritePicked(index); break;
            case Kind::Doodad:   emit doodadPicked(index); break;
            case Kind::Location: emit locationPicked(index); break;
            case Kind::Group:    item->setExpanded(!item->isExpanded()); break;
        }
    });

    // 한 번 누르면 바로 가는 편이 목록을 훑기에 낫다.
    connect(tree_, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem * item, int column) {
        emit tree_->itemActivated(item, column);
    });
}

void ObjectTree::setDocument(const chk::MapDocument * document)
{
    document_ = document;
    refresh();
}

void ObjectTree::setTileset(const io::GameGraphics * tileset)
{
    tileset_ = tileset;
    refresh();
}

void ObjectTree::refresh()
{
    if (tree_ == nullptr)
        return;

    tree_->clear();

    if (document_ == nullptr || !document_->isOpen())
        return;

    const auto & units = document_->units();
    const auto & sprites = document_->sprites();
    const auto doodads = document_->doodads();
    const auto & locations = document_->locations();

    // 유닛은 플레이어별로 묶는다 — 한 맵에 수백 개가 들어가므로 통째로
    // 늘어놓으면 찾을 수가 없다.
    if (!units.empty())
    {
        auto * root = makeGroup(tree_, tr("유닛 (%1)").arg(units.size()));

        QTreeWidgetItem * owners[13] {};
        int counts[13] {};

        for (std::size_t i = 0; i < units.size(); ++i)
        {
            const auto & unit = units[i];
            const int owner = unit.owner < 12 ? unit.owner : 12;

            if (owners[owner] == nullptr)
            {
                owners[owner] = new QTreeWidgetItem(root);
                owners[owner]->setData(0, KindRole, static_cast<int>(Kind::Group));
            }

            ++counts[owner];
            makeLeaf(owners[owner], Kind::Unit, i,
                     QString::fromStdString(io::unitTypeName(unit.type)),
                     tileText(unit.x, unit.y));
        }

        for (int owner = 0; owner < 13; ++owner)
        {
            if (owners[owner] == nullptr)
                continue;

            owners[owner]->setText(0, owner < 12
                ? tr("플레이어 %1 (%2)").arg(owner + 1).arg(counts[owner])
                : tr("그 밖 (%1)").arg(counts[owner]));
        }
    }

    if (!sprites.empty())
    {
        auto * root = makeGroup(tree_, tr("스프라이트 (%1)").arg(sprites.size()));
        for (std::size_t i = 0; i < sprites.size(); ++i)
        {
            const auto & sprite = sprites[i];
            makeLeaf(root, Kind::Sprite, i,
                     tr("스프라이트 %1").arg(sprite.type),
                     tileText(sprite.x, sprite.y));
        }
    }

    if (!doodads.empty())
    {
        auto * root = makeGroup(tree_, tr("두들 (%1)").arg(doodads.size()));

        // 이름은 타일셋이 알려 준다. 타일셋이 없으면 번호로만 적는다.
        const auto known = tileset_ != nullptr
            ? tileset_->doodads(document_->info().tilesetId)
            : std::vector<io::GameGraphics::DoodadInfo>{};

        for (std::size_t i = 0; i < doodads.size(); ++i)
        {
            const auto & doodad = doodads[i];
            const auto info = std::find_if(known.begin(), known.end(),
                [&doodad](const auto & entry) { return entry.id == doodad.type; });

            makeLeaf(root, Kind::Doodad, i,
                     info != known.end()
                         ? QString::fromStdString(info->name)
                         : tr("두들 %1").arg(doodad.type),
                     tileText(doodad.x, doodad.y));
        }
    }

    if (!locations.empty())
    {
        auto * root = makeGroup(tree_, tr("로케이션 (%1)").arg(locations.size()));
        for (std::size_t i = 0; i < locations.size(); ++i)
        {
            const auto & location = locations[i];
            const QString name = location.name.empty()
                ? tr("이름 없음 %1").arg(location.index)
                : QString::fromStdString(location.name);

            makeLeaf(root, Kind::Location, i, name,
                     tileText(static_cast<int>(location.left),
                              static_cast<int>(location.top)));
        }
    }

    applyFilter();
}

void ObjectTree::applyFilter()
{
    if (tree_ == nullptr || search_ == nullptr)
        return;

    const QString needle = search_->text().trimmed();

    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem * root = tree_->topLevelItem(i);

        // 묶음 안에 보일 것이 하나도 없으면 묶음째 감춘다.
        int rootVisible = 0;

        for (int j = 0; j < root->childCount(); ++j)
        {
            QTreeWidgetItem * child = root->child(j);

            if (child->childCount() == 0)
            {
                const bool show = needle.isEmpty() ||
                                  child->text(0).contains(needle, Qt::CaseInsensitive);
                child->setHidden(!show);
                rootVisible += show ? 1 : 0;
                continue;
            }

            int groupVisible = 0;
            for (int k = 0; k < child->childCount(); ++k)
            {
                QTreeWidgetItem * leaf = child->child(k);
                const bool show = needle.isEmpty() ||
                                  leaf->text(0).contains(needle, Qt::CaseInsensitive);
                leaf->setHidden(!show);
                groupVisible += show ? 1 : 0;
            }

            child->setHidden(groupVisible == 0);
            rootVisible += groupVisible;
        }

        root->setHidden(rootVisible == 0);

        // 거르는 중에는 펼쳐 둬야 결과가 보인다.
        if (!needle.isEmpty() && rootVisible > 0)
            root->setExpanded(true);
    }
}

} // namespace splash::ui
