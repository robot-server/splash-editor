#!/usr/bin/env python3
"""**트리거 조건·액션의 인자 순서를 정본에서 그대로 뽑는다.**

인자 순서를 짐작으로 적었다가 여러 번 틀렸다. 뿌리는 이것이다 —
같은 액션인데 **인자 순서가 두 벌** 있다.

    Chk::Action::classicArguments   에디터 창(Classic Trigger)에서 보이는 차례
    Chk::Action::textArguments      **텍스트 트리거**에서 쓰는 차례

`splash-cli trigger apply` 가 먹는 것은 **textArguments** 쪽이다.
Set Deaths 를 보면 분명하다:

    classic : player, numericMod, amount, unit
    text    : player, unit,       numericMod, amount

손으로 옮겨 적지 않는다. MappingCore 의 `chk.cpp` 에서 인자 정의를 읽어
`data/trigger-api.json` 과 `docs/trigger/api.md` 를 만든다.

    python3 measure_trigger_api.py [--doc <경로>]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

OUT = os.path.join(HERE, "..", "data", "trigger-api.json")


def find_chk_cpp() -> str:
    """MappingCore 의 chk.cpp 를 찾는다."""
    here = HERE
    for _ in range(6):
        cand = os.path.join(here, "build-cli", "_deps", "chkdraft-src",
                            "src", "mapping_core", "chk.cpp")
        if os.path.exists(cand):
            return cand
        here = os.path.dirname(here)
    raise SystemExit("chk.cpp 를 못 찾았습니다. 먼저 CLI 를 빌드하세요.")


TABLE_RE = re.compile(
    r"Chk::(Condition|Action)::Argument Chk::\1::(classic|text)Arguments"
    r"\[Num\1Types\]\[MaxArguments\] = \{(.*?)\n\};", re.S)
ROW_RE = re.compile(r"/\*\*\s*(\d+)\s*=\s*(.+?)\s*-*\s*\*/\s*\{(.*?)\}", re.S)


def parse_tables(src: str) -> dict:
    out = {}
    for kind, order, body in TABLE_RE.findall(src):
        rows = {}
        for num, name, args in ROW_RE.findall(body):
            args = [a.strip().replace("Arg", "")
                    for a in args.split(",") if a.strip()]
            rows[int(num)] = {"name": name.strip(), "args": args}
        out.setdefault(kind.lower(), {})[order] = rows
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="트리거 인자 순서를 정본에서 뽑는다")
    ap.add_argument("--doc", help="이 경로에 레퍼런스 문서를 쓴다")
    a = ap.parse_args(argv)

    src = open(find_chk_cpp(), encoding="utf-8", errors="replace").read()
    tables = parse_tables(src)
    if not tables:
        raise SystemExit("인자표를 못 읽었습니다.")

    data = {"_about": "트리거 조건·액션의 인자 순서. MappingCore chk.cpp의 "
                      "classicArguments와 textArguments에서 생성한다. text는 "
                      "splash-cli 입력 순서이고 classic은 에디터 창 순서다.",
            "conditions": {}, "actions": {}}

    for kind in ("condition", "action"):
        t = tables.get(kind, {})
        classic, text = t.get("classic", {}), t.get("text", {})
        for num in sorted(set(classic) | set(text)):
            c = classic.get(num, {})
            x = text.get(num, {})
            name = x.get("name") or c.get("name") or str(num)
            data[kind + "s"][str(num)] = {
                "name": name,
                "text_args": x.get("args", []),
                "classic_args": c.get("args", []),
                "differs": x.get("args", []) != c.get("args", []),
            }

    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"트리거 인자 정의를 썼습니다: {path}")

    if a.doc:
        write_doc(data, a.doc)
        print(f"API 레퍼런스를 썼습니다: {a.doc}")
    return 0


# 인자 이름을 사람 말로. 표에 쓰이는 것만 적는다.
ARG_KO = {
    "player": "플레이어", "playerCnd": "플레이어", "destPlayer": "받는 플레이어",
    "unit": "유닛", "unitCnd": "유닛",
    "location": "로케이션", "locationCnd": "로케이션",
    "secondaryLocation": "목적지 로케이션",
    "numericComparisonCnd": "견줌(at least/at most/exactly)",
    "numericMod": "고침(Set To/Add/Subtract)",
    "switchMod": "스위치 고침(set/clear/toggle/randomize)",
    "stateMod": "상태(enable/disable/toggle)",
    "allyStatusCnd": "동맹 상태", "allyStatus": "동맹 상태",
    "amount": "값", "amountCnd": "값", "numUnits": "마리 수",
    "unitQuantity": "마리 수 (All 가능)", "percent": "퍼센트",
    "duration": "밀리초", "durationCnd": "게임 초",
    "resource": "자원", "resourceCnd": "자원",
    "score": "점수 갈래", "scoreCnd": "점수 갈래",
    "switch": "스위치", "switchCnd": "스위치",
    "string": "글(스트링)", "sound": "소리(WAV)", "script": "AI 스크립트",
    "order": "명령(Move/Patrol/Attack)", "cuwp": "유닛 속성(64가지 제한)",
    "textFlags": "글 깃발(Always Display)", "number": "수",
    "memoryOffset": "메모리 자리", "memoryOffsetCnd": "메모리 자리",
    "memoryBitmask": "비트마스크", "maskFlag": "마스크 여부",
    "briefingSlot": "브리핑 슬롯", "comparison": "견줌",
    "switchStateCnd": "스위치 상태(set/not set)",
    "allyState": "동맹 상태(Enemy/Ally/Allied Victory)",
}


def write_doc(data: dict, path: str):
    """MappingCore의 인자 정의만 문서화한다. 맵 빈도/요약은 기록하지 않는다."""
    lines = [
        "# 트리거 API 레퍼런스 — 인자 순서", "",
        "명령 번호와 인자 배열은 MappingCore의 `chk.cpp`에서 읽는다.",
        "`text`는 `splash-cli trigger apply` 입력 순서이고 `classic`은 에디터 창 순서다.",
        "원문 강좌의 classic 인자 순서를 텍스트 트리거에 그대로 옮기지 않는다.", "",
    ]
    for kind, label in (("conditions", "조건 (Condition)"),
                        ("actions", "액션 (Action)")):
        lines.extend([f"## {label}", "",
                      "| ID | 이름 | 텍스트 인자 순서 |",
                      "| ---: | --- | --- |"])
        rows = data[kind]
        for num in sorted(rows, key=int):
            row = rows[num]
            if row["name"].startswith("No "):
                continue
            args = ", ".join(ARG_KO.get(x, x) for x in row["text_args"]) or "—"
            mark = " ⚠" if row["differs"] and row["text_args"] else ""
            lines.append(f"| {num} | **{row['name']}**{mark} | {args} |")
        lines.append("")
        diff = [row for row in rows.values()
                if row["differs"] and row["text_args"]]
        if diff:
            lines.extend(["### 인자 순서가 다른 명령", "",
                          "| 이름 | text (splash-cli) | classic (에디터 창) |",
                          "| --- | --- | --- |"])
            for row in diff:
                lines.append(
                    f"| {row['name']} | `{', '.join(row['text_args'])}` | "
                    f"`{', '.join(row['classic_args'])}` |")
            lines.append("")
    lines.extend([
        "## 근거", "",
        "각 인자 배열은 MappingCore `chk.cpp`의 `textArguments` 및 `classicArguments` 정의를 따른다. "
        "예시 호출은 [recipes.md](recipes.md)를, 의미와 동작은 [conditions.md](conditions.md) 및 "
        "[actions.md](actions.md)를 참조한다.",
    ])
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
