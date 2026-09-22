#!/usr/bin/env python3
"""**영웅 유닛은 "같은 유닛의 센 판" 이 아니다.** 특성이 다르다.

유즈맵에서 유닛을 고를 때 "센 건 영웅, 약한 건 일반" 으로 가르면 틀린다.
맵의 유닛 설정(UNIS·UNIx)으로 고칠 수 있는 것은 체력·방패·방어력·
생산시간·값 뿐이고, **사거리·공격 주기·시야·이동 속도는 못 고친다.**
그것들은 units.dat·weapons.dat·flingy.dat 에 박혀 있다.

이 스크립트는 `splash-cli unit-stats --json` 이 내는 게임 데이터를 읽어,
**같은 겉모습(flingy)** 을 쓰는 영웅과 일반을 짝지어 무엇이 다른지 낸다.
겉모습이 같다는 것이 "같은 형태의 유닛" 의 가장 믿을 만한 기준이다 —
이름 글자를 맞춰 보는 것보다 낫다.

    python3 measure_heroes.py [--install 경로]
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scmap  # noqa: E402

OUT = os.path.join(HERE, "..", "data", "heroes.json")

# 비교해 낼 값과 이름. **맵이 못 고치는 것에 별표를 붙인다.**
FIELDS = [
    ("ground_range", "지상 사거리*", 32),
    ("air_range", "공중 사거리*", 32),
    ("ground_damage", "지상 피해", 1),
    ("air_damage", "공중 피해", 1),
    ("ground_cooldown", "공격 주기*", 1),
    ("sight", "시야*", 1),
    ("target_range", "먼저 무는 거리*", 1),
    ("speed", "이동 속도*", 1),
    ("hp", "체력", 1),
    ("shields", "방패", 1),
    ("armor", "방어력", 1),
    ("size", "덩치*", 1),
]


def load_stats(install: str) -> dict:
    r = subprocess.run([scmap.find_cli(), "unit-stats", install, "--json"],
                       capture_output=True, text=True, check=True)
    return json.loads(r.stdout)


def main(argv=None):
    ap = argparse.ArgumentParser(description="영웅과 일반 유닛의 차이를 잰다")
    ap.add_argument("--install")
    a = ap.parse_args(argv)
    stats = load_stats(a.install or scmap.find_install())

    # 겉모습(flingy) 로 묶는다. 한쪽은 영웅, 한쪽은 일반이어야 짝이 된다.
    by_flingy: dict[int, list] = {}
    for num, u in stats.items():
        u["id"] = int(num)
        by_flingy.setdefault(u["flingy"], []).append(u)

    pairs = []
    for fl, group in sorted(by_flingy.items()):
        heroes = [u for u in group if u["hero"]]
        plain = [u for u in group if not u["hero"]]
        if not heroes or not plain:
            continue
        base = plain[0]
        for h in heroes:
            pairs.append((base, h))

    print(f"같은 겉모습을 쓰는 영웅·일반 짝 {len(pairs)}개\n")
    print("별표(*) 는 **맵의 유닛 설정으로 못 고치는 값**이다.\n")

    out = []
    for base, h in pairs:
        diffs = []
        for key, label, unit in FIELDS:
            b, v = base.get(key, 0), h.get(key, 0)
            if b == v:
                continue
            if unit != 1:
                diffs.append(f"{label} {b/unit:g}→{v/unit:g}")
            else:
                diffs.append(f"{label} {b}→{v}")
        same = [label for key, label, _ in FIELDS
                if base.get(key, 0) == h.get(key, 0) and "*" in label]
        row = {"hero_id": h["id"], "hero": h["name"],
               "base_id": base["id"], "base": base["name"],
               "flingy": h["flingy"],
               "diff": {k: [base.get(k, 0), h.get(k, 0)]
                        for k, _, _ in FIELDS if base.get(k, 0) != h.get(k, 0)},
               "same_uneditable": same,
               "ground_dmg_upgrade": [base.get("ground_dmg_upgrade"),
                                      h.get("ground_dmg_upgrade")],
               "move_control": [base.get("move_control"), h.get("move_control")]}
        out.append(row)
        print(f"{h['name']}  ←  {base['name']}")
        print(f"    {'; '.join(diffs) if diffs else '차이 없음'}")
        if base.get("ground_dmg_upgrade") == h.get("ground_dmg_upgrade"):
            print(f"    공격력 업그레이드는 **같은 번호**"
                  f"({h.get('ground_dmg_upgrade')}) — 영웅도 업이 먹는다")
        else:
            print(f"    공격력 업그레이드 번호가 다르다: "
                  f"{base.get('ground_dmg_upgrade')} vs "
                  f"{h.get('ground_dmg_upgrade')}")
        print()

    # 영웅 전체에서 공통으로 드러나는 것
    n_same_range = sum(1 for r in out if "지상 사거리*" in r["same_uneditable"])
    n_faster = sum(1 for r in out
                   if r["diff"].get("ground_cooldown")
                   and r["diff"]["ground_cooldown"][1]
                   < r["diff"]["ground_cooldown"][0])
    n_same_speed = sum(1 for r in out if "이동 속도*" in r["same_uneditable"])
    n_same_sight = sum(1 for r in out if "시야*" in r["same_uneditable"])
    n_same_upg = sum(1 for r in out
                     if r["ground_dmg_upgrade"][0] == r["ground_dmg_upgrade"][1])
    print("=" * 62)
    print(f"짝 {len(out)}개 가운데")
    print(f"  지상 사거리가 일반과 **같은** 영웅   {n_same_range}개")
    print(f"  공격 주기가 일반보다 **빠른** 영웅   {n_faster}개")
    print(f"  이동 속도가 일반과 **같은** 영웅     {n_same_speed}개")
    print(f"  시야가 일반과 **같은** 영웅          {n_same_sight}개")
    print(f"  공격력 업그레이드가 **같은** 영웅    {n_same_upg}개")

    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "영웅 유닛과 같은 겉모습(flingy)을 쓰는 일반 "
                             "유닛의 차이. measure_heroes.py 가 게임 데이터"
                             "(units.dat·weapons.dat·flingy.dat)에서 만든다. "
                             "same_uneditable 에 든 값은 **맵이 못 고치는데도 "
                             "영웅과 일반이 같은** 것이다 — 그 값을 노리고 "
                             "영웅을 고르면 헛수고다.",
                   "_summary": {"pairs": len(out),
                                "same_ground_range": n_same_range,
                                "faster_cooldown": n_faster,
                                "same_speed": n_same_speed,
                                "same_sight": n_same_sight,
                                "same_damage_upgrade": n_same_upg},
                   "pairs": out}, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
