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

손으로 옮겨 적지 않는다. MappingCore 의 `chk.cpp` 에서 표를 파싱해
`data/trigger-api.json` 을 만든다. 그리고 실제 맵에서 뽑은 트리거
텍스트로 **인자 개수가 맞는지 대조**한다.

    python3 measure_trigger_api.py [--maps <맵폴더>...]
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scmap  # noqa: E402

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


def observed_arity(map_dirs: list[str], limit: int = 120) -> dict:
    """실제 맵의 트리거 텍스트에서 이름별 인자 개수를 센다."""
    seen: dict[str, collections.Counter] = {}
    paths = []
    for d in map_dirs:
        for f in sorted(os.listdir(d)):
            if f.lower().endswith((".scx", ".scm")):
                paths.append(os.path.join(d, f))
    cli = scmap.find_cli()
    install = scmap.find_install()
    with tempfile.TemporaryDirectory() as tmp:
        txt = os.path.join(tmp, "t.txt")
        for p in paths[:limit]:
            r = subprocess.run([cli, "trigger", "show", p, txt,
                                "--install", install],
                               capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(txt):
                continue
            try:
                body = open(txt, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            for m in re.finditer(r"^\s*([A-Z][A-Za-z ':()/]+?)\((.*)\);",
                                 body, re.M):
                name, args = m.group(1).strip(), m.group(2)
                if name in ("Trigger", "Conditions", "Actions"):
                    continue
                # 괄호·따옴표 안의 쉼표는 세지 않는다
                depth, quoted, n = 0, False, (1 if args.strip() else 0)
                for ch in args:
                    if ch == '"':
                        quoted = not quoted
                    elif not quoted and ch == "(":
                        depth += 1
                    elif not quoted and ch == ")":
                        depth -= 1
                    elif not quoted and ch == "," and depth == 0:
                        n += 1
                seen.setdefault(name, collections.Counter())[n] += 1
    return {k: dict(v) for k, v in seen.items()}


def main(argv=None):
    ap = argparse.ArgumentParser(description="트리거 인자 순서를 정본에서 뽑는다")
    ap.add_argument("--maps", nargs="*", default=[],
                    help="대조할 맵 폴더들 (없으면 표만 뽑는다)")
    ap.add_argument("--doc", help="이 경로에 레퍼런스 문서를 쓴다")
    a = ap.parse_args(argv)

    src = open(find_chk_cpp(), encoding="utf-8", errors="replace").read()
    tables = parse_tables(src)
    if not tables:
        raise SystemExit("인자표를 못 읽었습니다.")

    data = {"_about": "트리거 조건·액션의 인자 순서. measure_trigger_api.py "
                      "가 MappingCore 의 chk.cpp 에서 그대로 뽑는다. "
                      "**text 가 텍스트 트리거(splash-cli trigger apply)의 "
                      "차례**이고 classic 은 에디터 창의 차례다. 둘이 다른 "
                      "것이 있으므로 섞어 쓰면 안 된다.",
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

    if a.maps:
        print("실제 맵에서 인자 개수를 대조합니다...")
        obs = observed_arity(a.maps)
        for kind in ("conditions", "actions"):
            for num, row in data[kind].items():
                counts = obs.get(row["name"])
                if counts:
                    row["observed_arity"] = counts
                    n = len(row["text_args"])
                    # counts 의 키는 숫자다. JSON 을 거치면 문자열이 되므로
                    # 양쪽을 문자열로 맞춰 견준다 — 여기서 한 번 틀렸다.
                    if n and str(n) not in {str(k) for k in counts}:
                        row["arity_mismatch"] = True

    for kind, label in (("conditions", "조건"), ("actions", "액션")):
        rows = data[kind]
        diff = [r for r in rows.values() if r["differs"] and r["text_args"]]
        print(f"\n{label} {len(rows)}가지, "
              f"그중 **두 차례가 다른 것 {len(diff)}가지**")
        for r in diff:
            print(f"  {r['name']}")
            print(f"    text    : {', '.join(r['text_args'])}")
            print(f"    classic : {', '.join(r['classic_args'])}")
        bad = [r for r in rows.values() if r.get("arity_mismatch")]
        if bad:
            print(f"  ! 인자 개수가 실제 맵과 안 맞는 것 {len(bad)}가지: "
                  + ", ".join(r["name"] for r in bad[:8]))
        ok = [r for r in rows.values()
              if r.get("observed_arity") and not r.get("arity_mismatch")]
        if ok:
            total = sum(sum(int(v) for v in r["observed_arity"].values())
                        for r in ok)
            print(f"  실제 맵에서 {len(ok)}가지를 {total:,}번 보았고 "
                  f"인자 개수가 모두 맞는다")

    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")

    if a.doc:
        write_doc(data, a.doc)
        print(f"썼습니다: {a.doc}")
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
    L = []
    A = L.append
    A("# 트리거 API 레퍼런스 — 인자 순서")
    A("")
    A("**이 문서는 생성된 것이다.** `measure_trigger_api.py` 가")
    A("MappingCore 의 `chk.cpp` 에서 정본 표를 뽑아 쓴다. 손으로 고치지")
    A("않는다 — 고치려면 스크립트를 고친다.")
    A("")
    A("## 먼저 알 것 — 인자 순서가 **두 벌**이다")
    A("")
    A("| 표 | 어디서 쓰나 |")
    A("| --- | --- |")
    A("| **text** | **텍스트 트리거.** `splash-cli trigger apply` 가 먹는 것 |")
    A("| classic | 에디터의 Classic Trigger 창에 보이는 차례 |")
    A("")
    A("카페 강좌의 문장(`Player brings comparison quantity units to")
    A("location`)은 **classic 쪽**이다. 그대로 텍스트 트리거에 옮겨 적으면")
    A("틀린다. 내가 여러 번 틀린 자리다.")
    A("")
    nd = sum(1 for k in ("conditions", "actions")
             for r in data[k].values() if r["differs"] and r["text_args"])
    A(f"두 차례가 다른 것이 **{nd}가지**나 된다. 아래 표에서 ⚠ 표시.")
    A("")
    for kind, label in (("conditions", "조건 (Condition)"),
                        ("actions", "액션 (Action)")):
        rows = [r for r in data[kind].values() if r["name"] != "No Action"
                and r["name"] != "No Condition"]
        A(f"## {label}")
        A("")
        A("| # | 이름 | 텍스트 인자 순서 | 실전 |")
        A("| ---: | --- | --- | ---: |")
        for num in sorted(data[kind], key=int):
            r = data[kind][num]
            if r["name"].startswith("No "):
                continue
            args = ", ".join(ARG_KO.get(x, x) for x in r["text_args"]) or "—"
            mark = " ⚠" if r["differs"] and r["text_args"] else ""
            seen = r.get("observed_arity")
            cnt = (f"{sum(int(v) for v in seen.values()):,}" if seen else "—")
            A(f"| {num} | **{r['name']}**{mark} | {args} | {cnt} |")
        A("")
        diff = [r for r in data[kind].values()
                if r["differs"] and r["text_args"]]
        if diff:
            A(f"### ⚠ 두 차례가 다른 {len(diff)}가지")
            A("")
            A("| 이름 | text (쓸 것) | classic (쓰지 말 것) |")
            A("| --- | --- | --- |")
            for r in diff:
                A(f"| {r['name']} | `{', '.join(r['text_args'])}` | "
                  f"`{', '.join(r['classic_args'])}` |")
            A("")
    A("## 대조")
    A("")
    tot = sum(sum(int(v) for v in r["observed_arity"].values())
              for k in ("conditions", "actions") for r in data[k].values()
              if r.get("observed_arity"))
    n = sum(1 for k in ("conditions", "actions") for r in data[k].values()
            if r.get("observed_arity"))
    A(f"실제 유즈맵에서 뽑은 트리거 텍스트로 **{n}가지를 {tot:,}번** 보았고,")
    A("인자 개수가 위 표와 모두 맞았다.")
    A("")
    A("## 관련")
    A("")
    A("- [conditions.md](conditions.md) — 조건이 무엇을 뜻하나")
    A("- [actions.md](actions.md) — 액션이 무엇을 하나")
    A("- [recipes.md](recipes.md) — 바로 쓰는 조각")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
