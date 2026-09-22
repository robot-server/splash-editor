#!/usr/bin/env python3
"""**유즈맵이 ISOM 을 쓰는가 사각형을 쓰는가**를 장르별로 잰다.

내가 "밀리=ISOM, 유즈맵=사각형" 으로 못 박아 두고 있었다. 사용자가
바로잡았다 — 유즈맵은 **컨셉과 장르가 정한다.** OX 퀴즈는 지형이
주인공이 아니니 사각형이 맞고, RPG 는 사각형이면 심심하니 ISOM 으로
진짜 같은 지형을 준다. 이 말이 맞는지 실측으로 확인한다.

## 어떻게 재는가

CHK 의 `ISOM` 섹션은 **가로 절반+1 × 세로+1** 개의 마름모를 각 4개의
uint16 으로 담는다. 여기서 보는 것은 **ISOM 값의 다양도**다.

- 맵을 한 지형으로 한 번 채우고 나머지를 전부 사각형으로 찍었다면
  ISOM 은 **한 가지 값**으로 균일하다.
- ISOM 붓질을 했다면 절벽·경계마다 다른 값이 남는다.

    isom_variety = 으뜸값이 아닌 마름모의 비율

0 에 가까우면 사각형 편집, 크면 ISOM 편집이다. MTXM(실제 타일)의
다양도와 **함께** 봐야 한다 — 지형을 아예 안 건드린 맵도 0 이므로.

    python3 measure_isom_usage.py <맵폴더> [<맵폴더>...]
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scmap  # noqa: E402
import classify_genre  # noqa: E402

OUT = os.path.join(HERE, "..", "data", "isom-usage.json")


def chk_sections(path: str, tmp: str) -> dict:
    """CHK 를 뽑아 섹션 이름 → 몸통. 뒤에 나온 것이 이긴다."""
    out = os.path.join(tmp, "x.chk")
    subprocess.run([scmap.find_cli(), "chk", path, out],
                   capture_output=True, check=True)
    buf = open(out, "rb").read()
    secs, i = {}, 0
    while i + 8 <= len(buf):
        # 섹션 이름은 4바이트 고정이라 "DIM " 처럼 뒤에 빈칸이 붙는다.
        # 그대로 키로 쓰면 secs["DIM"] 이 안 잡힌다.
        name = buf[i:i + 4].decode("latin-1").strip()
        ln = struct.unpack_from("<i", buf, i + 4)[0]
        i += 8
        if ln < 0:
            # 음수 길이는 **앞 섹션을 잘라 내는 수법**이다. 몸통은 없다.
            # 앞서 여기서 i 를 뒤로 되돌렸는데, 그러면 같은 자리를
            # 영원히 다시 읽어 무한 루프가 된다. 머리만 넘기고 간다.
            continue
        secs[name] = buf[i:i + ln]
        i += ln
    return secs


def genre_by_triggers(dump_dir: str) -> dict[str, str]:
    """맵 파일 이름 → 장르. **이름이 아니라 트리거로** 가른다.

    이름으로 붙이면 유즈맵 479장 중 44장만 걸린다 — 표본이 너무 작아
    장르별 수치를 믿을 수 없다. `classify_genre.py` 가 쓰는 것과 같은
    특징·중심점으로 나머지를 가장 가까운 장르에 붙인다. 정확도는 이름
    딱지를 정답으로 두고 잰 76% 다 (data/genre-fingerprints.json).
    """
    rows = classify_genre.load(dump_dir)
    labeled = [r for r in rows if r["genre"]]
    genres = sorted({r["genre"] for r in labeled})
    cent = classify_genre.centroids(labeled, genres)
    out = {}
    for r in rows:
        if r["genre"]:
            best = r["genre"]
        else:
            best, score = "?", 0.0
            for g, (c, _) in cent.items():
                sc = classify_genre.cos(r["f"], c)
                if sc > score:
                    best, score = g, sc
            if score < 0.5:
                best = "?"
        # 덤프의 id 는 파일 이름에서 확장자를 뗀 것이다
        out[r["id"] + ".scx"] = best
        out[r["id"] + ".scm"] = best
    return out


def variety(vals) -> float:
    """으뜸값이 아닌 것의 비율."""
    if not vals:
        return 0.0
    c = collections.Counter(vals)
    return 1.0 - c.most_common(1)[0][1] / len(vals)


def measure_map(secs: dict) -> dict | None:
    dim = secs.get("DIM")
    if not dim or len(dim) < 4:
        return None
    w, h = struct.unpack_from("<HH", dim, 0)
    if not w or not h:
        return None

    isom = secs.get("ISOM")
    iv = 0.0
    has_isom = False
    if isom:
        need = (w // 2 + 1) * (h + 1) * 4
        n = min(need, len(isom) // 2)
        if n:
            has_isom = True
            # 마름모마다 첫 값(지형 종류)만 본다
            cells = struct.unpack_from(f"<{n}H", isom, 0)[0::4]
            iv = variety(cells)

    mtxm = secs.get("MTXM")
    mv, groups = 0.0, 0
    if mtxm:
        n = min(w * h, len(mtxm) // 2)
        tiles = struct.unpack_from(f"<{n}H", mtxm, 0)
        mv = variety(tiles)
        groups = len({t >> 4 for t in tiles})

    return {"w": w, "h": h, "has_isom": has_isom,
            "isom_variety": round(iv, 4), "mtxm_variety": round(mv, 4),
            "tile_groups": groups}


def med(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2] if xs else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="장르별 ISOM/사각형 쓰임을 잰다")
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--dumps", help="full/*.json 덤프 폴더. 주면 장르를 "
                                    "이름이 아니라 **트리거 지문**으로 "
                                    "붙인다 — 표본이 열 배로 는다.")
    a = ap.parse_args(argv)

    by_file = genre_by_triggers(a.dumps) if a.dumps else {}

    paths = []
    for d in a.dirs:
        for f in sorted(os.listdir(d)):
            if f.lower().endswith((".scx", ".scm")):
                paths.append(os.path.join(d, f))

    by_genre = collections.defaultdict(list)
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for p in paths:
            try:
                r = measure_map(chk_sections(p, tmp))
            except Exception:
                continue
            if not r:
                continue
            base = os.path.basename(p)
            name = os.path.splitext(base)[0]
            g = (by_file.get(base)
                 or classify_genre.label_by_name(name) or "?")
            r["genre"] = g
            rows.append(r)
            by_genre[g].append(r)

    print(f"맵 {len(rows)}장\n")
    print(f"{'장르':10s} {'수':>4s} {'ISOM있음':>8s} "
          f"{'ISOM다양도중앙':>14s} {'타일다양도중앙':>14s} {'타일그룹중앙':>12s}")
    order = sorted(by_genre, key=lambda g: -len(by_genre[g]))
    summary = {}
    for g in order:
        rs = by_genre[g]
        iv = med([r["isom_variety"] for r in rs])
        mv = med([r["mtxm_variety"] for r in rs])
        tg = med([r["tile_groups"] for r in rs])
        hi = sum(1 for r in rs if r["has_isom"])
        print(f"{g:10s} {len(rs):4d} {100*hi//len(rs):7d}% "
              f"{iv:14.3f} {mv:14.3f} {tg:12d}")
        # ISOM 을 실제로 쓴 맵 = ISOM 다양도가 0.05 를 넘는 맵
        used = sum(1 for r in rs if r["isom_variety"] > 0.05)
        summary[g] = {"n": len(rs), "has_isom_pct": 100 * hi // len(rs),
                      "isom_variety_median": iv, "mtxm_variety_median": mv,
                      "tile_groups_median": tg,
                      "isom_used_pct": 100 * used // len(rs)}

    print(f"\n{'장르':10s} ISOM 을 실제로 붓질한 맵의 비율")
    for g in order:
        print(f"{g:10s} {summary[g]['isom_used_pct']:3d}%   "
              f"(n={summary[g]['n']})")

    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "장르별 ISOM/사각형 쓰임. measure_isom_usage.py "
                             "가 만든다. isom_used_pct 는 ISOM 값이 한 가지가 "
                             "아닌 맵(=실제로 붓질한 맵)의 비율.",
                   "by_genre": summary}, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
