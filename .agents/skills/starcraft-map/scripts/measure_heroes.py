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


def resolve_weapon(u: dict, stats: dict) -> tuple[dict, str]:
    """**때리는 값이 어디 있는지** 찾아 준다.

    유닛이 제 무기를 안 들고 있는 일이 셋 있다. 이걸 모르면 영웅
    비교가 통째로 틀린다 — 앨런 셰자르(골리앗)를 "체력만 늘었다" 로
    읽고 있었는데, 실제로는 포탑 공격력이 12→24 로 두 배였다.

    | 어디 | 누가 | 어떻게 찾나 |
    | --- | --- | --- |
    | 제 무기 | 대부분 | 그대로 |
    | **아랫유닛(포탑)** | 골리앗·시즈탱크와 그 영웅 | `subunit1` 을 따라간다 |
    | **따로 나오는 유닛** | 캐리어·리버와 그 영웅 | 인터셉터·스캐럽이 때린다 |

    캐리어·리버는 **영웅도 일반도 무기가 없다.** 때리는 것은 인터셉터
    (피해 6)와 스캐럽(피해 100)인데 **그 둘은 영웅판이 아예 없다.**
    그래서 간트리서와 일반 캐리어는 **공격력이 같다.** 워브링거와 일반
    리버도 같다. "영웅이니 더 세겠지" 가 여기서 틀린다.

    돌려주는 것: (값을 볼 유닛, 어디서 왔나)
    """
    if u.get("ground_weapon", 130) < 130 or u.get("air_weapon", 130) < 130:
        return u, "제 무기"
    sub = u.get("subunit1", 228)
    if sub != 228:
        s2 = stats.get(str(sub))
        if s2 is not None:
            return s2, "아랫유닛"
    return u, "따로 나오는 유닛"


def main(argv=None):
    ap = argparse.ArgumentParser(description="영웅과 일반 유닛의 차이를 잰다")
    ap.add_argument("--install")
    a = ap.parse_args(argv)
    stats = load_stats(a.install or scmap.find_install())

    # 영웅과 일반을 짝짓는다. **두 갈래로 찾는다.**
    #
    # 처음에는 겉모습(flingy)만 봤다. 그러면 **사라 케리건이 빠진다** —
    # 케리건은 고스트와 생김새가 달라 flingy 가 77 이고 일반 고스트는
    # 74 다. 그래서 이름의 괄호도 본다: "Sarah Kerrigan (Ghost)" 의
    # 괄호 안이 짝지을 일반 유닛을 가리킨다.
    by_flingy: dict[int, list] = {}
    for num, u in stats.items():
        u["id"] = int(num)
        by_flingy.setdefault(u["flingy"], []).append(u)

    plain_by_name = {u["name"].lower(): u
                     for u in stats.values() if not u["hero"]}

    def base_by_paren(h: dict):
        """이름 괄호에서 일반 유닛을 찾는다. "Fenix (Dragoon)" → Dragoon."""
        if "(" not in h["name"]:
            return None
        inner = h["name"][h["name"].index("(") + 1:].rstrip(")").strip().lower()
        if not inner:
            return None
        for race in ("terran ", "protoss ", "zerg ", ""):
            cand = plain_by_name.get(race + inner)
            if cand is not None:
                return cand
        # "Infested Terran" 처럼 앞뒤가 붙는 것도 훑는다
        for name, u in plain_by_name.items():
            if inner and (name.endswith(inner) or inner.endswith(name)):
                return u
        return None

    pairs, seen = [], set()
    for fl, group in sorted(by_flingy.items()):
        heroes = [u for u in group if u["hero"]]
        plain = [u for u in group if not u["hero"]]
        if not heroes or not plain:
            continue
        for h in heroes:
            pairs.append((plain[0], h, "겉모습"))
            seen.add(h["id"])
    for u in sorted(stats.values(), key=lambda x: x["id"]):
        if not u["hero"] or u["id"] in seen:
            continue
        b = base_by_paren(u)
        if b is not None and b["id"] != u["id"]:
            pairs.append((b, u, "이름"))
            seen.add(u["id"])
    pairs.sort(key=lambda p: p[1]["id"])

    missed = [u["name"] for u in stats.values()
              if u["hero"] and u["id"] not in seen]
    if missed:
        print(f"짝을 못 찾은 영웅 {len(missed)}기: {', '.join(missed[:8])}\n")

    print(f"영웅·일반 짝 {len(pairs)}개\n")
    print("별표(*) 는 **맵의 유닛 설정으로 못 고치는 값**이다.\n")

    out = []
    WEAPON_KEYS = {"ground_range", "air_range", "ground_damage",
                   "air_damage", "ground_cooldown"}
    for base, h, how in pairs:
        # 무기 값은 포탑에 있을 수 있다. 거기서 읽는다.
        bw, bwhere = resolve_weapon(base, stats)
        hw, hwhere = resolve_weapon(h, stats)
        diffs = []
        for key, label, unit in FIELDS:
            src_b = bw if key in WEAPON_KEYS else base
            src_h = hw if key in WEAPON_KEYS else h
            b, v = src_b.get(key, 0), src_h.get(key, 0)
            if b == v:
                continue
            if unit != 1:
                diffs.append(f"{label} {b/unit:g}→{v/unit:g}")
            else:
                diffs.append(f"{label} {b}→{v}")
        same = [label for key, label, _ in FIELDS
                if (bw if key in WEAPON_KEYS else base).get(key, 0)
                == (hw if key in WEAPON_KEYS else h).get(key, 0)
                and "*" in label]
        row = {"hero_id": h["id"], "hero": h["name"], "matched_by": how,
               "base_id": base["id"], "base": base["name"],
               "flingy": h["flingy"],
               "weapon_from": hwhere,
               "diff": {k: [(bw if k in WEAPON_KEYS else base).get(k, 0),
                            (hw if k in WEAPON_KEYS else h).get(k, 0)]
                        for k, _, _ in FIELDS
                        if (bw if k in WEAPON_KEYS else base).get(k, 0)
                        != (hw if k in WEAPON_KEYS else h).get(k, 0)},
               "same_uneditable": same,
               "ground_dmg_upgrade": [bw.get("ground_dmg_upgrade"),
                                      hw.get("ground_dmg_upgrade")],
               "move_control": [base.get("move_control"), h.get("move_control")]}
        out.append(row)
        extra = (f", 무기는 {hwhere}" if hwhere != "제 무기" else "")
        print(f"{h['name']}  ←  {base['name']}  ({how}으로 짝지음{extra})")
        print(f"    {'; '.join(diffs) if diffs else '차이 없음'}")
        if hwhere == "따로 나오는 유닛":
            print("    **때리는 것은 이 유닛이 아니다** — 따로 나오는 "
                  "유닛(인터셉터·스캐럽)이 때리고, 그것은 영웅판이 "
                  "없으므로 **공격력이 일반과 같다**")
        elif bw.get("ground_dmg_upgrade") == hw.get("ground_dmg_upgrade"):
            print(f"    공격력 업그레이드는 **같은 번호**"
                  f"({hw.get('ground_dmg_upgrade')}) — 영웅도 업이 먹는다")
        else:
            print(f"    공격력 업그레이드 번호가 다르다: "
                  f"{bw.get('ground_dmg_upgrade')} vs "
                  f"{hw.get('ground_dmg_upgrade')}")
        print()

    # **무기를 나눠 쓰는 유닛들.** weapons.dat 한 칸을 둘 이상이 쓰면
    # 한쪽을 고칠 수 없다 — 같이 바뀐다. 타사다르와 알다리스가 그렇다.
    shared: dict[int, list] = {}
    for u in stats.values():
        gw = u.get("ground_weapon", 130)
        if gw < 130:
            shared.setdefault(gw, []).append(u["name"])
    shared = {k: v for k, v in shared.items() if len(v) > 1}
    if shared:
        print("=" * 62)
        print("**무기를 나눠 쓰는 유닛** — 한쪽만 고칠 수 없다")
        for gw, names in sorted(shared.items()):
            print(f"  무기 {gw:3d}: {', '.join(names)}")

    # **영웅판이 아예 없는 유닛.** 있는 줄 알고 찾으면 시간만 버린다.
    # 유닛을 고를 때 "영웅으로 바꾸면 되지" 가 안 통하는 자리다.
    paired_base = {b["id"] for b, _, _ in pairs}
    NOT_PLAYABLE = ("turret", "cocoon", "egg", "larva", "scarab",
                    "interceptor", "critter", "spell", "map revealer",
                    "start location", "nuclear", "scanner", "disruption",
                    "dark swarm", "left ", "right ", "floor ", "unused",
                    "cave", "ruins", "khaydarin", "trap", "door", "beacon")
    heroless = []
    for u in sorted(stats.values(), key=lambda x: x["id"]):
        if u["hero"] or u["id"] in paired_base:
            continue
        name = u["name"].lower()
        if any(k in name for k in NOT_PLAYABLE):
            continue
        if u.get("building") or not u.get("can_attack"):
            pass
        if u.get("supply", 0) == 0 and not u.get("can_attack"):
            continue
        # 건물은 뺀다 — units.dat 의 Building 깃발은 flags BIT_0
        if u.get("flags", 0) & 1:
            continue
        heroless.append(u["name"])
    if heroless:
        print("=" * 62)
        print("**영웅판이 없는 유닛** — 영웅으로 바꿔 쓸 수 없다")
        for i in range(0, len(heroless), 3):
            print("  " + " · ".join(f"{n:26s}" for n in heroless[i:i + 3]))

    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"_about": "영웅 유닛과 같은 겉모습(flingy)을 쓰는 일반 "
                             "유닛의 차이. measure_heroes.py 가 게임 데이터"
                             "(units.dat·weapons.dat·flingy.dat)에서 만든다. "
                   "same_uneditable 에 든 값은 **맵이 못 고치는데도 "
                             "영웅과 일반이 같은** 것이다 — 그 값을 노리고 "
                             "영웅을 고르면 헛수고다.",
                   "heroless": heroless,
                   "shared_weapons": {str(k): v
                                      for k, v in sorted(shared.items())},
                   "pairs": out}, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
