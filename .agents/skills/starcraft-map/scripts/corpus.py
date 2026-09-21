#!/usr/bin/env python3
"""실측 분포를 읽어 쓴다 — `data/corpus.json`.

수치를 손으로 적어 넣지 않는다. 표에서 뽑는다.

**전체 중앙값을 그대로 쓰면 안 된다.** 두들 개수만 해도 타일셋에 따라
Space 0개에서 Badlands 208개까지 벌어진다. 전체 중앙값은 어느 타일셋
에도 맞지 않는 수다. 반드시 타일셋별 값을 본다.

보기:
    import corpus
    c = corpus.load()
    corpus.pick_floor_groups(c, "ice", "usemap", 3)   # [2, 3, 13]
    corpus.stat(c, "usemap", "ice", "doodads")        # {'median': 0, ...}
"""
from __future__ import annotations

import json
import os
import random

_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "data", "corpus.json")
_CACHE = None

TILESET_NAMES = {0: "badlands", 1: "space", 2: "installation", 3: "ashworld",
                 4: "jungle", 5: "desert", 6: "ice", 7: "twilight"}


def load() -> dict:
    global _CACHE
    if _CACHE is None:
        with open(os.path.normpath(_PATH), encoding="utf-8") as f:
            _CACHE = json.load(f)
    return _CACHE


def _ts(tileset) -> str:
    if isinstance(tileset, int):
        return TILESET_NAMES[tileset & 7]
    return tileset.lower()


def tileset_block(c: dict, kind: str, tileset) -> dict:
    """kind 는 "melee" 또는 "usemap"."""
    per = c[kind]["per_tileset"]
    name = _ts(tileset)
    if name not in per:                 # 표본이 없는 타일셋
        return {}
    return per[name]


def stat(c: dict, kind: str, tileset, key: str) -> dict:
    """타일셋별 사분위. 표본이 없으면 전체 값으로 물러난다."""
    b = tileset_block(c, kind, tileset)
    if b and key in b:
        return b[key]
    return c[kind]["overall"].get(key, {})


def group_weights(c: dict, kind: str, tileset) -> dict[int, float]:
    b = tileset_block(c, kind, tileset)
    return {int(k): v for k, v in (b.get("group_weights") or {}).items()}


def pick_floor_groups(c: dict, tileset, kind: str = "usemap",
                      n: int = 3, skip_void: bool = True) -> list[int]:
    """그 타일셋에서 실제로 바닥으로 많이 쓰인 그룹을 많은 순으로 준다.

    그룹 0 은 빈 칸(검은 타일)이다. 바닥으로 쓰면 안 되니 뺀다.
    """
    w = group_weights(c, kind, tileset)
    order = sorted(w.items(), key=lambda kv: -kv[1])
    out = [g for g, _ in order if not (skip_void and g == 0)]
    return out[:n]


def weighted_groups(c: dict, tileset, kind: str = "usemap",
                    n: int = 8, skip_void: bool = True) -> list[tuple[int, float]]:
    """(그룹, 확률) 목록. 확률은 다시 1 로 맞춘다."""
    w = group_weights(c, kind, tileset)
    items = [(g, p) for g, p in w.items() if not (skip_void and g == 0)]
    items.sort(key=lambda kv: -kv[1])
    items = items[:n]
    tot = sum(p for _, p in items) or 1.0
    return [(g, p / tot) for g, p in items]


def sample_group(c: dict, tileset, rng: random.Random,
                 kind: str = "usemap", n: int = 8) -> int:
    items = weighted_groups(c, tileset, kind, n)
    if not items:
        return 1
    r = rng.random()
    acc = 0.0
    for g, p in items:
        acc += p
        if r <= acc:
            return g
    return items[-1][0]


def doodad_count(c: dict, tileset, kind: str, rng: random.Random) -> int:
    """그 타일셋에 어울리는 두들 개수를 사분위 사이에서 뽑는다.

    Space 밀리맵의 중앙값은 0 이다. 0 이 답인 타일셋에 억지로 두들을
    뿌리지 않는다.
    """
    q = stat(c, kind, tileset, "doodads")
    if not q:
        return 0
    lo, hi = q.get("q1", 0), q.get("q3", 0)
    if hi <= lo:
        return int(q.get("median", 0))
    return int(rng.triangular(lo, hi, q.get("median", (lo + hi) / 2)))


def doodad_weights(c: dict, kind: str, tileset) -> list[tuple[int, float]]:
    b = tileset_block(c, kind, tileset)
    d = {int(k): v for k, v in (b.get("doodad_weights") or {}).items()}
    d.pop(0, None)                       # 0 은 "두들 없음" 자리다
    tot = sum(d.values()) or 1.0
    return sorted(((g, p / tot) for g, p in d.items()), key=lambda kv: -kv[1])


def describe(kind: str, tileset) -> str:
    """사람이 읽을 한 줄 — 만들기 전에 찍어 두면 좋다."""
    c = load()
    b = tileset_block(c, kind, tileset)
    if not b:
        return f"{_ts(tileset)}: {kind} 표본 없음"
    q = lambda k: b.get(k, {})
    g = pick_floor_groups(c, tileset, kind, 5)
    return (f"{_ts(tileset)} {kind} (표본 {b['n_maps']}장): "
            f"두들 {q('doodads').get('median')}개"
            f"({q('doodads').get('q1')}~{q('doodads').get('q3')}), "
            f"타일그룹 {q('tile_groups_used').get('median')}종, "
            f"서로 다른 타일 {q('distinct_tiles').get('median')}개, "
            f"많이 쓰는 그룹 {g}")


if __name__ == "__main__":
    import sys
    c = load()
    print(c["_about"], "\n")
    print("⚠", c["_caution"], "\n")
    for kind in ("melee", "usemap"):
        o = c[kind]["overall"]
        print(f"=== {kind} (지형 {o['n_maps']}장) ===")
        for k in ("units", "doodads", "locations_named", "triggers",
                  "tile_groups_used", "distinct_tiles"):
            q = o.get(k, {})
            print(f"  {k:18s} 중앙 {q.get('median'):>7} "
                  f"({q.get('q1')}~{q.get('q3')})")
        print(f"  주기성 2칸        중앙 "
              f"{o['periodicity_pct']['2'].get('median'):.1f}%")
        for t in sorted(c[kind]["per_tileset"]):
            print("   ", describe(kind, t))
        print()
