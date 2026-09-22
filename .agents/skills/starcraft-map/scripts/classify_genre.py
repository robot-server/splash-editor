#!/usr/bin/env python3
"""**장르를 이름이 아니라 트리거로 가른다.**

지금까지 장르 통계는 맵 **이름**에 "디펜스"·"키우기" 같은 말이 들어
있는지로 갈랐다. 두 가지가 잘못이다.

  1. 이름이 장르를 안 밝히는 맵이 많다 — 실측 유즈맵의 32%(182장)가
     어디에도 안 걸렸다.
  2. 이름은 만든 사람이 붙인 광고 문구다. "복권 디펜스" 는 디펜스가
     아니라 도박이고, "OOO 키우기" 가 실은 디펜스인 것도 있다.

장르는 **트리거가 하는 일**이다. 여기서는 이름으로 붙인 표를 정답처럼
쓰지 않고, 트리거 특징으로 얼마나 갈라지는지를 **재고** 그 지문을
남긴다. 갈라지지 않으면 그 장르는 트리거로 구분되지 않는다는 사실
자체가 결과다.

    python3 classify_genre.py <덤프폴더>      # full/*.json 이 있는 곳
"""
from __future__ import annotations

import collections
import glob
import json
import math
import os
import sys

# 이름으로 붙이는 딱지 — **정답이 아니라 출발점**이다.
NAME_TAGS = {
    "defense": ("디펜스", "디팬스", "defense", "defence"),
    "rpg": ("키우기", "키워", "rpg", "육성"),
    "control": ("컨트롤", "control", "대결", "무한"),
    "zombie": ("좀비", "zombie", "감염"),
    "quiz": ("퀴즈", "quiz", "ox", "상식"),
    "escape": ("술래", "술레", "탈출", "escape", "피하기", "잡기"),
    "blood": ("블러드", "blood", "핏빗", "피꽃"),
    "land": ("땅따먹기", "점령", "land"),
    "tag": ("태그", "tag"),
    "sports": ("축구", "야구", "골", "soccer", "baseball"),
}


def label_by_name(name: str) -> str | None:
    low = (name or "").lower()
    hit = [g for g, keys in NAME_TAGS.items() if any(k in low for k in keys)]
    return hit[0] if len(hit) == 1 else None


def features(d: dict) -> dict[str, float]:
    """트리거가 무엇을 쓰는가. 개수가 아니라 **쓰는가**로 본다.

    개수를 쓰면 트리거가 많은 큰 맵이 전부 한 장르로 뭉친다.
    """
    f = {}
    for key, pre in (("actions", "A"), ("conditions", "C")):
        for name in (d.get(key) or {}):
            f[f"{pre}:{name}"] = 1.0
    # 몇 가지는 "얼마나" 가 장르를 가른다
    n = max(1, d.get("n_trigger_blocks") or 1)
    for name, cnt in (d.get("actions") or {}).items():
        if name in ("Create Unit", "Create Unit with Properties",
                    "Modify Unit Hit Points", "Set Deaths", "Move Unit",
                    "Set Alliance Status", "Set Countdown Timer",
                    "Display Text Message", "Give Units to Player",
                    "Kill Unit At Location", "Order", "Remove Unit At Location"):
            f[f"R:{name}"] = min(1.0, cnt / n * 4)
    for name, cnt in (d.get("conditions") or {}).items():
        if name in ("Bring", "Command", "Deaths", "Switch", "Kill",
                    "Countdown Timer", "Elapsed Time", "Accumulate"):
            f[f"R:{name}"] = min(1.0, cnt / n * 4)
    return f


def load(dump_dir: str):
    rows = []
    for jf in sorted(glob.glob(os.path.join(dump_dir, "*.json"))):
        try:
            d = json.load(open(jf, encoding="utf-8"))
        except Exception:
            continue
        info = d.get("info") or {}
        if "use/" not in (info.get("파일") or ""):
            continue
        if not (d.get("actions") or d.get("conditions")):
            continue
        name = (info.get("이름") or "") + " " + os.path.basename(
            info.get("파일") or "")
        rows.append({"id": os.path.basename(jf)[:-5],
                     "name": name,
                     "genre": label_by_name(name),
                     "f": features(d)})
    return rows


def centroids(rows, genres):
    """장르마다 특징의 평균. 가장 단순한 지문이다."""
    out = {}
    for g in genres:
        mine = [r for r in rows if r["genre"] == g]
        if not mine:
            continue
        acc = collections.Counter()
        for r in mine:
            for k, v in r["f"].items():
                acc[k] += v
        out[g] = ({k: v / len(mine) for k, v in acc.items()}, len(mine))
    return out


def cos(a: dict, b: dict) -> float:
    if not a or not b:
        return 0.0
    dot = sum(v * b.get(k, 0.0) for k, v in a.items())
    na = math.sqrt(sum(v * v for v in a.values()))
    nb = math.sqrt(sum(v * v for v in b.values()))
    return dot / (na * nb) if na and nb else 0.0


def main():
    dump = sys.argv[1] if len(sys.argv) > 1 else "full"
    rows = load(dump)
    labeled = [r for r in rows if r["genre"]]
    unlabeled = [r for r in rows if not r["genre"]]
    genres = sorted({r["genre"] for r in labeled})
    print(f"유즈맵 {len(rows)}장 — 이름으로 갈린 것 {len(labeled)}장, "
          f"안 갈린 것 {len(unlabeled)}장 ({100*len(unlabeled)//max(1,len(rows))}%)")
    print(f"장르 {len(genres)}종: " + ", ".join(
        f"{g}({sum(1 for r in labeled if r['genre']==g)})" for g in genres))

    # --- 하나 빼고 맞추기 ----------------------------------------------
    print("\n-- 트리거만 보고 이름 딱지를 맞출 수 있는가 (하나 빼고 맞추기) --")
    conf = collections.Counter()
    for i, r in enumerate(labeled):
        rest = labeled[:i] + labeled[i + 1:]
        cen = centroids(rest, genres)
        best = max(cen, key=lambda g: cos(r["f"], cen[g][0]))
        conf[(r["genre"], best)] += 1
    hit = sum(v for (a, b), v in conf.items() if a == b)
    print(f"   전체 정확도 {100 * hit // len(labeled)}% "
          f"({hit}/{len(labeled)}). 아무렇게나 찍으면 "
          f"{100 // len(genres)}% 다.")
    print(f"   {'장르':12s} {'수':>4s} {'맞춤':>5s}  가장 많이 헷갈린 것")
    for g in genres:
        tot = sum(v for (a, _), v in conf.items() if a == g)
        ok = conf[(g, g)]
        wrong = sorted(((v, b) for (a, b), v in conf.items()
                        if a == g and b != g), reverse=True)
        w = f"{wrong[0][1]} {wrong[0][0]}장" if wrong else "-"
        print(f"   {g:12s} {tot:4d} {100*ok//max(1,tot):4d}%  {w}")

    # --- 무엇이 그 장르를 가르는가 --------------------------------------
    print("\n-- 장르를 가르는 트리거 (그 장르에서 쓰는 비율 − 나머지에서) --")
    cen = centroids(labeled, genres)
    prints = {}
    for g in genres:
        mine, n = cen[g]
        others = [r for r in labeled if r["genre"] != g]
        acc = collections.Counter()
        for r in others:
            for k, v in r["f"].items():
                acc[k] += v
        rest = {k: v / len(others) for k, v in acc.items()}
        gap = sorted(((mine.get(k, 0.0) - rest.get(k, 0.0), k)
                      for k in set(mine) | set(rest)), reverse=True)
        top = [(k, round(d, 2)) for d, k in gap[:6] if d > 0.15]
        bot = [(k, round(d, 2)) for d, k in gap[-4:] if d < -0.15]
        prints[g] = {"n": n, "more": top, "less": bot}
        print(f"   {g} ({n}장)")
        for k, d in top:
            print(f"      + {k:34s} {d:+.2f}")
        for k, d in bot:
            print(f"      - {k:34s} {d:+.2f}")

    # --- 이름이 없는 맵은 무엇에 가까운가 --------------------------------
    print("\n-- 이름으로 안 갈린 맵을 트리거로 붙여 보면 --")
    near = collections.Counter()
    weak = 0
    for r in unlabeled:
        scored = sorted(((cos(r["f"], cen[g][0]), g) for g in genres),
                        reverse=True)
        if scored and scored[0][0] - scored[1][0] < 0.02:
            weak += 1          # 1등과 2등이 붙어 있으면 "모르겠다" 다
            continue
        near[scored[0][1]] += 1
    for g, c in near.most_common():
        print(f"   {g:12s} {c:4d}장")
    print(f"   가릴 수 없음 {weak}장 (1등과 2등 차이가 0.02 미만)")

    out = {"_about": "트리거 지문으로 본 장르. classify_genre.py 가 만든다. "
                     "이름 딱지를 정답으로 두고 잰 것이므로 이름 자체가 "
                     "틀린 맵은 오차로 남는다.",
           "accuracy_pct": 100 * hit // len(labeled),
           "n_labeled": len(labeled), "n_unlabeled": len(unlabeled),
           "fingerprints": prints}
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "..", "data", "genre-fingerprints.json")
    json.dump(out, open(os.path.normpath(p), "w", encoding="utf-8"),
              ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {os.path.normpath(p)}")


if __name__ == "__main__":
    main()
