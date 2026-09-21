#!/usr/bin/env python3
"""있는 맵의 조각으로 새 밀리맵을 조립한다.

절차적으로 지형을 만들면 손잡이를 아무리 맞춰도 맵이 되지 않는다.
타일 다양성·두들 개수 같은 수치를 중앙값에 맞춰 봐야, 나오는 것은
네모난 언덕에 흩뿌린 장식이다. 실제로 그렇게 여러 번 만들어 봤다.

사람이 만든 맵에서 **조각을 떠다 쓰면** 다르다. 본진을 뜨면 램프·절벽·
두들·자원이 한 벌로 따라오고, 바닥을 뜨면 지형이 원래 가진 결이 그대로
온다. 조립하는 쪽이 만들어 내는 쪽보다 낫다.

    바닥 깔기  →  본진 놓기  →  이음매 다듬기  →  걸어서 통하는지 확인

**원본 맵의 저작물이다.** 떠낸 조각과 그것으로 만든 맵은 원본에서 나온
것이므로, 남의 맵을 원본으로 썼다면 그대로 배포하지 않는다. 이 스크립트는
저장소에 조각을 담지 않고 쓸 때마다 사용자가 가진 맵에서 떠낸다.

보기:
    python3 compose_melee.py out.scx --from "원본.scx" --players 4
"""
from __future__ import annotations

import argparse
import math
import os
import random
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
import extract_stamps as ex
from scmap import Cli, CliError


def source_bases(src: Cli):
    """원본에서 스타팅 자리를 찾는다 (관측자 판본은 하나로 묶는다)."""
    out = []
    for u in src.units():
        if u["type"] != scmap.START_LOCATION:
            continue
        t = (u["x"] // scmap.TILE, u["y"] // scmap.TILE)
        if any(abs(t[0] - q[0]) <= 8 and abs(t[1] - q[1]) <= 8 for q in out):
            continue
        out.append(t)
    return out


def floor_patches(src: Cli, work: str, count: int, size: int, avoid, rng):
    """바닥으로 깔 지형 조각을 원본 여기저기에서 떠낸다.

    본진 언저리는 피한다 — 자원과 램프가 딸려 오면 바닥으로 못 쓴다.
    """
    info = src.info()
    W, H = info["width"], info["height"]
    made = []
    tries = 0
    while len(made) < count and tries < count * 12:
        tries += 1
        x = rng.randrange(0, max(1, W - size))
        y = rng.randrange(0, max(1, H - size))
        if any(abs(x + size // 2 - bx) < 22 and abs(y + size // 2 - by) < 22
               for (bx, by) in avoid):
            continue
        name = f"floor_{len(made)}"
        src.run("terrain", "copy", src.path, str(x), str(y),
                str(size), str(size), os.path.join(work, name + ".tiles"))
        made.append(name)
    return made


def main(argv=None):
    ap = argparse.ArgumentParser(description="있는 맵의 조각으로 밀리맵을 조립한다")
    ap.add_argument("out")
    ap.add_argument("--from", dest="source", required=True,
                    help="조각을 떠올 원본 맵 (사용자가 가진 맵)")
    ap.add_argument("--players", type=int, default=4)
    ap.add_argument("--size", default=None, help="128x128 꼴. 기본은 원본과 같게")
    ap.add_argument("--base-size", type=int, default=34,
                    help="본진 조각 한 변 (공식 맵 실측 중앙값 34)")
    ap.add_argument("--patch", type=int, default=24, help="바닥 조각 한 변")
    ap.add_argument("--patches", type=int, default=14, help="떠낼 바닥 조각 수")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--name", default=None)
    ap.add_argument("--install", default=None)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args(argv)

    if os.path.exists(args.out) and not args.force:
        print(f"이미 있습니다: {args.out} (--force 로 덮어씁니다)", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    src = Cli(args.source, args.install)
    sinfo = src.info()
    if args.size:
        W, H = (int(v) for v in args.size.lower().split("x"))
    else:
        W, H = sinfo["width"], sinfo["height"]
    tileset = sinfo["tileset_id"] & 7

    print(f"원본: {sinfo['name']} {sinfo['width']}x{sinfo['height']} "
          f"{sinfo['tileset']}")
    print(f"만들 맵: {W}x{H}, 스타팅 {args.players}개")

    work = tempfile.mkdtemp(prefix="compose_")
    try:
        # 1) 원본에서 본진과 바닥 조각을 떠낸다
        bases = source_bases(src)
        if not bases:
            raise CliError("원본에 스타팅이 없습니다 — 본진을 뜰 수 없습니다.")
        print(f"원본 본진 {len(bases)}곳에서 조각을 뜹니다...")
        base_names = []
        cx, cy = (sinfo["width"] - 1) / 2.0, (sinfo["height"] - 1) / 2.0
        S = args.base_size
        for i, (bx, by) in enumerate(bases):
            # 본진에서 가운데를 보는 쪽을 더 담아야 램프가 통째로 들어온다
            ox = S // 2 - (S // 6 if bx < cx else -S // 6)
            oy = S // 2 - (S // 6 if by < cy else -S // 6)
            x0 = max(0, min(sinfo["width"] - S, bx - ox))
            y0 = max(0, min(sinfo["height"] - S, by - oy))
            m = ex.save_stamp(src, f"base_{i}", work, x0, y0, S, S)
            m["start_in"] = [bx - x0, by - y0]
            base_names.append((f"base_{i}", m))

        print(f"바닥 조각 {args.patches}개를 뜹니다...")
        patches = floor_patches(src, work, args.patches, args.patch, bases, rng)
        if not patches:
            raise CliError("바닥으로 쓸 조각을 뜨지 못했습니다.")

        # 2) 새 맵을 만들고 바닥을 조각으로 덮는다
        types_probe = None
        cli = scmap.new_map(args.out, W, H, tileset, terrain=None,
                            melee=True, install=args.install)
        print("바닥을 깝니다...")
        P = args.patch
        for y in range(0, H, P):
            for x in range(0, W, P):
                name = rng.choice(patches)
                w = min(P, W - x); h = min(P, H - y)
                if w < P or h < P:
                    continue          # 가장자리 자투리는 뒤에서 메운다
                cli.edit("terrain", "paste", cli.path, str(x), str(y),
                         os.path.join(work, name + ".tiles"))
        # 가장자리 자투리
        for y in range(0, H, P):
            for x in range(0, W, P):
                if x + P <= W and y + P <= H:
                    continue
                name = rng.choice(patches)
                cli.edit("terrain", "paste", cli.path,
                         str(min(x, max(0, W - P))), str(min(y, max(0, H - P))),
                         os.path.join(work, name + ".tiles"))

        # 3) 본진을 대칭 자리에 놓는다
        print("본진을 놓습니다...")
        inset = max(2, S // 2 + 2)
        spots = []
        if args.players == 2:
            spots = [(inset, inset), (W - S - inset, H - S - inset)]
        elif args.players == 4:
            spots = [(inset, inset), (W - S - inset, inset),
                     (W - S - inset, H - S - inset), (inset, H - S - inset)]
        else:
            mx, my = (W - S) / 2.0, (H - S) / 2.0
            r = min(mx, my) - inset / 2
            for k in range(args.players):
                a = -math.pi / 2 + 2 * math.pi * k / args.players
                spots.append((int(mx + r * math.cos(a)), int(my + r * math.sin(a))))
        placed = 0
        for i, (x, y) in enumerate(spots):
            x = max(0, min(W - S, x)); y = max(0, min(H - S, y))
            name, meta = base_names[i % len(base_names)]
            ex.place_stamp(cli, os.path.join(work, name), x, y, owner=i + 1)
            placed += 1
        print(f"  본진 {placed}곳")

        # 4) 걸어서 통하는지 본다 — 조각 이음매가 길을 막을 수 있다
        print("연결을 확인합니다...")
        starts = [(u["x"] // 32, u["y"] // 32) for u in cli.units()
                  if u["type"] == scmap.START_LOCATION]
        grid = scmap.walk_grid(cli, tileset, 0, 0, W, H)
        pts = [scmap.nearest_walkable(grid, sx * 4 + 2, sy * 4 + 2, radius=48)
               for (sx, sy) in starts]
        bad = [i + 1 for i, p in enumerate(pts)
               if p is None or not scmap.walk_reachable(grid, pts[0], p)]
        if bad:
            print(f"  {bad} 번 스타팅이 갇혔습니다 — 손으로 길을 내야 합니다")
        else:
            print(f"  스타팅 {len(pts)}곳 모두 이어집니다")

        if args.name:
            cli.set_map_name(args.name)
        info = cli.info()
        print(f"\n만들었습니다: {args.out}")
        print(f"  {info['width']}x{info['height']} {info['tileset']} "
              f"{info['version']}  유닛 {info['units']}")
        print(f"\n  python3 preview.py {args.out} look.png   ← 반드시 그려 볼 것")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
