#!/usr/bin/env python3
"""내가 쓴 트리거를 **실제 맵의 용례와 대조한다.**

**이 도구가 보증하는 것은 좁다.** 인자의 *모양*(문자열/숫자/낱말의
자리)만 보고 값은 보지 않는다. 그래서 잡는 것은 낱말 오타와 없는 동작
이름 정도다. 실제로 이 도구를 통과하면서 시작하자마자 지는 맵을 받은
적이 있다 — "모두 일치" 를 품질 근거로 쓰면 안 된다.

컴파일이 통과한다고 맞는 게 아니다. `Modify Unit Hit Points` 의 인자
차례를 거꾸로 알고 쓴 적이 있는데, 컴파일은 멀쩡히 통과하고 게임에서는
체력을 회복하는 대신 12% 로 깎았다. 실제로 그렇게 만들어 플레이해 보고
지적받았다.

그래서 **내가 내보내는 모든 동작·조건을 실제 맵이 쓰는 꼴과 나란히
놓고 본다.** 사람이 읽고 판단하라고 만든 도구다 — 자동으로 고치지
않는다.

쓰는 법:

    # 먼저 실제 맵에서 용례를 모은다 (한 번만, 좀 걸린다)
    python3 check_trigger_usage.py index <실제맵폴더> <용례.json>

    # 내 맵을 대조한다
    python3 check_trigger_usage.py check <내맵.scx> <용례.json>
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scmap
from scmap import Cli, CliError

CALL = re.compile(r'^\s*([A-Z][A-Za-z0-9 \'/-]*?)\((.*)\);\s*$')


def split_args(s: str) -> list[str]:
    """괄호 안을 쉼표로 가른다. 따옴표 안의 쉼표는 건드리지 않는다."""
    out, cur, q = [], "", False
    for ch in s:
        if ch == '"':
            q = not q
            cur += ch
        elif ch == "," and not q:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def shape(args: list[str]) -> str:
    """인자 모양 — 문자열은 s, 숫자는 n, 낱말은 그대로.

    **숫자는 값이 아니라 자리만 본다.** 값까지 보면 `Deaths(..., 12)` 와
    `Deaths(..., 13)` 이 서로 다른 꼴이 되어 대조가 무의미해진다.
    값 자체는 `arg_values` 로 따로 견준다.
    """
    parts = []
    for a in args:
        if a.startswith('"'):
            parts.append("s")
        elif re.fullmatch(r"-?\d+", a):
            parts.append("n")
        else:
            parts.append(a)
    return "(" + ",".join(parts) + ")"


def dump_calls(path: str, install: str) -> list[tuple[str, list[str]]]:
    tmp = tempfile.NamedTemporaryFile(suffix=".txt", delete=False)
    tmp.close()
    try:
        r = subprocess.run([scmap.find_cli(),
                            "trigger", "show", path, tmp.name,
                            "--install", install],
                           capture_output=True, timeout=600)
        if r.returncode != 0:
            return []
        text = open(tmp.name, encoding="utf-8", errors="replace").read()
    finally:
        os.unlink(tmp.name)
    out = []
    for line in text.splitlines():
        m = CALL.match(line)
        if m:
            out.append((m.group(1).strip(), split_args(m.group(2))))
    return out


def build_index(map_dir: str, install: str, out_path: str, limit: int = 60):
    """실제 맵에서 동작·조건마다 쓰인 인자 모양을 센다."""
    idx: dict[str, collections.Counter] = collections.defaultdict(
        collections.Counter)
    maps = [os.path.join(map_dir, f) for f in sorted(os.listdir(map_dir))
            if f.lower().endswith((".scm", ".scx"))][:limit]
    for i, p in enumerate(maps, 1):
        calls = dump_calls(p, install)
        for name, args in calls:
            idx[name][shape(args)] += 1
        print(f"  {i}/{len(maps)} {os.path.basename(p)[:24]} "
              f"호출 {len(calls)}", flush=True)
    data = {k: dict(v.most_common(12)) for k, v in idx.items()}
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"n_maps": len(maps), "usage": data}, f, ensure_ascii=False,
                  indent=1)
    print(f"\n{out_path}: 동작·조건 {len(data)}종, 맵 {len(maps)}장")


def check(map_path: str, index_path: str, install: str) -> int:
    with open(index_path, encoding="utf-8") as f:
        data = json.load(f)
    usage = data["usage"]
    mine = collections.Counter()
    for name, args in dump_calls(map_path, install):
        mine[(name, shape(args))] += 1

    print(f"== {os.path.basename(map_path)} — 실제 맵 {data['n_maps']}장과 대조 ==\n")
    problems = 0
    for (name, sh), n in sorted(mine.items()):
        real = usage.get(name)
        if real is None:
            print(f"[??] {name}{sh}  x{n}")
            print(f"     실제 맵에서 한 번도 안 쓰는 동작이다. 정말 맞는가?\n")
            problems += 1
            continue
        if sh in real:
            continue
        # 모양이 다르다 — 실제로 쓰는 꼴을 보여 준다
        print(f"[!!] {name}{sh}  x{n}")
        print(f"     실제 맵은 이렇게 쓴다:")
        for rs, rn in list(real.items())[:4]:
            print(f"       {rn:6d}회  {name}{rs}")
        print()
        problems += 1
    if problems:
        print(f"견줘 볼 것 {problems}개. **자동으로 고치지 않는다** — "
              f"인자 차례와 뜻을 직접 확인하라.")
    else:
        print("내가 쓴 **인자 모양**이 모두 실제 맵에도 있다.\n"
              "  ※ 이 도구는 모양만 본다 — 값은 보지 않는다.\n"
              "     숫자는 n 으로 뭉개므로 (…, 12, 0, …) 과 (…, 100, 0, …)\n"
              "     은 같은 꼴로 취급된다. 회복이 깎기가 되어도 통과한다.\n"
              "     **이 문장은 맵이 제대로 돈다는 뜻이 아니다.**\n"
              "     값과 뜻은 verify_map.py 와 사람이 본다.")
    return 1 if problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="트리거 용례를 실제 맵과 대조")
    sub = ap.add_subparsers(dest="cmd", required=True)
    i = sub.add_parser("index", help="실제 맵에서 용례를 모은다")
    i.add_argument("map_dir"); i.add_argument("out")
    i.add_argument("--limit", type=int, default=60)
    i.add_argument("--install", default=None)
    c = sub.add_parser("check", help="내 맵을 대조한다")
    c.add_argument("map"); c.add_argument("index")
    c.add_argument("--install", default=None)
    a = ap.parse_args(argv)
    install = a.install or os.environ.get("SC_INSTALL")
    if not install:
        raise CliError("SC_INSTALL 이 필요합니다.")
    if a.cmd == "index":
        build_index(a.map_dir, install, a.out, a.limit)
        return 0
    return check(a.map, a.index, install)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CliError as e:
        print(f"오류: {e}", file=sys.stderr)
        sys.exit(1)
