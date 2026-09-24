#!/usr/bin/env python3
"""이미 있는 맵에서 구조를 통째로 떠낸다 — 본진·앞마당·가운데 지형.

절차적으로 지형을 만들어 내면 아무리 꾸며도 기계 티가 난다. 타일 변종을
고르게 섞어도, 덩이 모양을 흐트러뜨려도 마찬가지다. 사람이 만든 맵에서
**있는 구조를 그대로 떠다 쓰는 편**이 결과가 낫고, 램프·절벽·두뎃이
한 벌로 따라오므로 이어지지 않는 문제도 같이 사라진다.

떠낸 것은 지형(.tiles)과 물건(.objects) 두 파일이다. 물건에는 유닛·
스프라이트·두뎃·로케이션이 들어간다.

**떠낸 파일은 원본 맵에서 나온 것이다.** 남이 만든 맵에서 떠냈다면 그
맵의 저작물이다. 저장소에 넣지 말고, 쓸 때마다 사용자가 가진 맵에서
떠내 쓴다 — 그래서 이 스크립트는 도구만 주고 결과물은 담지 않는다.

보기:
    # 맵의 본진을 모두 떠낸다 (스타팅마다 하나씩)
    python3 extract_stamps.py bases <원본맵> <떠낼폴더> --size 30

    # 아무 네모나 떠낸다
    python3 extract_stamps.py region <원본맵> <떠낼폴더> --at 40 40 --size 24

    # 떠낸 것을 붙인다
    python3 extract_stamps.py place <대상맵> <떠낼폴더>/main_0 --at 10 10
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError


def save_stamp(cli: Cli, name: str, out_dir: str,
               tile_x: int, tile_y: int, w: int, h: int) -> dict:
    """네모 하나를 지형 + 물건으로 떠낸다."""
    os.makedirs(out_dir, exist_ok=True)
    base = os.path.join(out_dir, name)
    cli.run("terrain", "copy", cli.path, str(tile_x), str(tile_y),
            str(w), str(h), base + ".tiles")
    cli.run("object", "copy", cli.path, str(tile_x), str(tile_y),
            str(w), str(h), base + ".objects",
            "--units", "--sprites", "--doodads")
    info = cli.info()
    meta = {"name": name, "width": w, "height": h,
            "tileset": info["tileset_id"] & 7,
            "from_size": [info["width"], info["height"]],
            "at": [tile_x, tile_y]}
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)
    return meta


def load_stamp(prefix: str) -> dict:
    with open(prefix + ".json", encoding="utf-8") as f:
        return json.load(f)


def place_stamp(cli: Cli, prefix: str, tile_x: int, tile_y: int,
                owner: int | None = None):
    """떠낸 것을 붙인다. 지형을 먼저, 물건을 나중에."""
    meta = load_stamp(prefix)
    cli.edit("terrain", "paste", cli.path, str(tile_x), str(tile_y),
             prefix + ".tiles")
    args = ["object", "paste", cli.path, str(tile_x), str(tile_y),
            prefix + ".objects", "--install", cli.install]
    if owner is not None:
        args += ["--owner", str(owner)]
    cli.edit(*args)
    return meta


def extract_bases(cli: Cli, out_dir: str, size: int) -> list[dict]:
    """스타팅마다 그 둘레를 떠낸다.

    스타팅을 가운데에 두고 네모를 잡되 맵 밖으로 나가지 않게 민다.
    본진 언덕과 램프가 모두 들어가도록 size 를 넉넉히 준다 (30 이상 권장).
    """
    info = cli.info()
    width, height = info["width"], info["height"]
    starts = [(u["x"] // scmap.TILE, u["y"] // scmap.TILE, u["owner"])
              for u in cli.units() if u["type"] == scmap.START_LOCATION]
    if not starts:
        raise CliError("스타팅이 없습니다 — 본진을 찾을 수 없습니다.")

    # 관측자 판본은 스타팅이 겹쳐 있다. 가까운 것은 하나로 본다.
    picked = []
    for (sx, sy, owner) in starts:
        if any(abs(sx - px) <= 8 and abs(sy - py) <= 8 for px, py, _ in picked):
            continue
        picked.append((sx, sy, owner))

    out = []
    cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
    for i, (sx, sy, owner) in enumerate(picked):
        # 스타팅을 가운데가 아니라 **맵 바깥쪽으로 치우쳐** 잡는다.
        # 본진에서 나가는 램프는 맵 가운데 쪽에 있으므로, 그쪽을 더
        # 담아야 램프가 통째로 들어온다.
        toward_x = 1 if sx < cx else -1
        toward_y = 1 if sy < cy else -1
        x0 = sx - size // 2 + toward_x * size // 6
        y0 = sy - size // 2 + toward_y * size // 6
        x0 = max(0, min(width - size, x0))
        y0 = max(0, min(height - size, y0))
        meta = save_stamp(cli, f"main_{i}", out_dir, x0, y0, size, size)
        meta["start_offset"] = [sx - x0, sy - y0]
        meta["owner"] = owner
        with open(os.path.join(out_dir, f"main_{i}.json"), "w",
                  encoding="utf-8") as f:
            json.dump(meta, f, ensure_ascii=False, indent=1)
        out.append(meta)
        print(f"  main_{i}: ({x0},{y0}) {size}x{size}  "
              f"스타팅은 안에서 ({sx - x0},{sy - y0})")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="맵에서 구조를 떠낸다")
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("bases", help="스타팅마다 본진을 떠낸다")
    b.add_argument("source"); b.add_argument("out_dir")
    b.add_argument("--size", type=int, default=30)
    b.add_argument("--install", default=None)

    r = sub.add_parser("region", help="네모 하나를 떠낸다")
    r.add_argument("source"); r.add_argument("out_dir")
    r.add_argument("--at", nargs=2, type=int, required=True, metavar=("X", "Y"))
    r.add_argument("--size", type=int, default=24)
    r.add_argument("--name", default="region")
    r.add_argument("--install", default=None)

    p = sub.add_parser("place", help="떠낸 것을 붙인다")
    p.add_argument("target"); p.add_argument("prefix")
    p.add_argument("--at", nargs=2, type=int, required=True, metavar=("X", "Y"))
    p.add_argument("--owner", type=int, default=None)
    p.add_argument("--install", default=None)

    a = ap.parse_args(argv)

    if a.cmd == "bases":
        cli = Cli(a.source, a.install)
        print(f"{os.path.basename(a.source)} 에서 본진을 떠냅니다...")
        made = extract_bases(cli, a.out_dir, a.size)
        print(f"본진 {len(made)}개 → {a.out_dir}")
    elif a.cmd == "region":
        cli = Cli(a.source, a.install)
        m = save_stamp(cli, a.name, a.out_dir, a.at[0], a.at[1], a.size, a.size)
        print(f"{m['name']}: ({a.at[0]},{a.at[1]}) {a.size}x{a.size} → {a.out_dir}")
    else:
        cli = Cli(a.target, a.install)
        m = place_stamp(cli, a.prefix, a.at[0], a.at[1], a.owner)
        print(f"붙였습니다: {m['name']} @ ({a.at[0]},{a.at[1]})")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
