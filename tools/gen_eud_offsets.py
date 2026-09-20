#!/usr/bin/env python3
"""eudplib 의 scdata 에서 이름 붙은 EUD 오프셋 표를 뽑아 C++ 로 옮긴다.

eudplib(armoha/eud plib) 은 MIT 다. 주소·크기 같은 수치는 사실이고, 이름은
eudplib 의 멤버 이름을 그대로 쓴다. 고지문은 만들어지는 파일 머리말에 박는다.

쓰는 법:

    python3 tools/gen_eud_offsets.py <eudplib 소스 폴더> > src/io/eud_offsets_eudplib.inc

`<eudplib 소스 폴더>` 는 `src/eudplib` 을 담고 있는 곳이다. 예:

    git clone https://github.com/armoha/eudplib /tmp/eudplib
    python3 tools/gen_eud_offsets.py /tmp/eudplib

**왜 자동으로 받지 않는가**: 이 표는 1.16.1 의 붙박이 수치라 한 번 뽑으면
바뀌지 않는다. 빌드할 때마다 바깥에서 받아 오면 업스트림이 바뀔 때 조용히
달라질 수 있어, 만들어진 파일을 저장소에 넣고 필요할 때만 다시 뽑는다.
"""

import re
import subprocess
import sys
from pathlib import Path

# 파일마다 무엇을 담는 표인지와, 그 표의 항목 수.
# 항목 수는 1.16.1 의 DAT 표 크기다.
GROUPS = {
    "player.py":    ("플레이어", 12),
    "unit.py":      ("units.dat", 228),
    "weapon.py":    ("weapons.dat", 130),
    "flingy.py":    ("flingy.dat", 209),
    "sprite.py":    ("sprites.dat", 517),
    "image.py":     ("images.dat", 999),
    "tech.py":      ("techdata.dat", 44),
    "upgrade.py":   ("upgrades.dat", 61),
    "unitorder.py": ("orders.dat", 189),
}

BASE_SIZES = {"ByteKind": 1, "WordKind": 2, "DwordKind": 4}


def kind_sizes(memberkind_src: str) -> dict[str, int]:
    """Kind 하나하나가 몇 바이트인지. 상속을 따라간다."""
    parents = dict(re.findall(r"^class (\w+Kind)\((\w+)\)", memberkind_src, re.M))
    sizes = dict(BASE_SIZES)
    for name in parents:
        seen = set()
        cursor = name
        while cursor not in sizes and cursor in parents and cursor not in seen:
            seen.add(cursor)
            cursor = parents[cursor]
        if cursor in sizes:
            sizes[name] = sizes[cursor]
    return sizes


def member_kinds(member_src: str) -> dict[str, str]:
    """XxxMember 가 어떤 Kind 를 쓰는지."""
    out = {}
    for match in re.finditer(
        r"^class (\w+Member)\(.*?\n(.*?)(?=^class |\Z)", member_src, re.M | re.S
    ):
        kind = re.search(r"return (\w+Kind)", match.group(2))
        if kind:
            out[match.group(1)] = kind.group(1)
    return out


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    scdata = root / "src" / "eudplib" / "scdata"
    if not scdata.is_dir():
        print(f"eudplib 소스를 찾지 못했습니다: {scdata}", file=sys.stderr)
        return 1

    sizes = kind_sizes((scdata / "offsetmap" / "memberkind.py").read_text())
    kinds = member_kinds((scdata / "offsetmap" / "member.py").read_text())

    # `name: ClassVar[XMember] = XMember("array", 0x..., stride=N)` 꼴과
    # `name = XEnum("array", 0x...)` 꼴을 함께 받는다.
    pattern = re.compile(
        r'^\s{4}(\w+)(?:\s*:\s*ClassVar(?:\[[^\]]*\])?)?\s*=\s*'
        r'(\w+)\(\s*"(\w+)"\s*,\s*(0x[0-9A-Fa-f]+)([^)]*)',
        re.M,
    )

    entries = []
    for filename, (group, nominal) in GROUPS.items():
        path = scdata / filename
        if not path.is_file():
            continue
        for name, member, scope, address, rest in pattern.findall(path.read_text()):
            addr = int(address, 16)
            if addr < 0x400000:
                continue  # 구조체 안의 상대 오프셋은 주소가 아니다
            if member in ("UnsupportedMember", "NotImplementedMember"):
                continue

            if member.endswith("Enum"):
                size = {"Byte": 1, "Word": 2, "Dword": 4}.get(member[:-4], 0)
            else:
                size = sizes.get(kinds.get(member, ""), 0)
            if size == 0:
                continue

            stride = re.search(r"stride\s*=\s*(\d+)", rest)
            if stride:
                size = int(stride.group(1))

            # eudplib 이 주석으로 개수를 적어 둔 자리가 있다 (`# length=8`).
            # 적혀 있으면 그것이 가장 정확하다.
            declared = re.search(r"#\s*length\s*=\s*(\d+)", rest)
            entries.append((addr, f"{group} · {name}", size,
                            int(declared.group(1)) if declared else nominal))

    entries.sort()

    # 개수는 다음 자리까지의 틈으로 조인다. 표를 너무 길게 잡으면 옆 항목을
    # 삼켜 엉뚱한 이름이 붙는다 — 모자란 쪽이 안전하다.
    #
    # 다만 틈이 넓은 자리는 이래도 실제보다 길게 잡힐 수 있다. 그런 곳은
    # 이름이 조금 넘겨 붙을 뿐이고 주소·크기와 값에는 영향이 없다.
    tightened = []
    for index, (addr, name, size, nominal) in enumerate(entries):
        length = nominal
        if index + 1 < len(entries):
            gap = entries[index + 1][0] - addr
            if gap > 0:
                length = max(1, min(nominal, gap // size))
        tightened.append((addr, name, size, length))

    # 같은 주소를 여러 이름이 가리키는 자리가 있다(유닛 크기의 LT/T/R/B 처럼
    # 한 구조를 쪼개 보는 것들). 좁은 쪽만 남긴다.
    narrowest = {}
    for addr, name, size, length in tightened:
        span = size * length
        if addr not in narrowest or span < narrowest[addr][2] * narrowest[addr][3]:
            narrowest[addr] = (addr, name, size, length)
    tightened = sorted(narrowest.values())

    revision = "알 수 없음"
    try:
        revision = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except Exception:
        pass

    out = sys.stdout
    out.write(f"""\
// 이 파일은 tools/gen_eud_offsets.py 가 만든다. 손으로 고치지 않는다.
//
// 출처: armoha/eudplib 의 src/eudplib/scdata (MIT License)
//       https://github.com/armoha/eudplib
//       뽑아 온 판: {revision}
//
//   Copyright (c) 2014-2017 trgk, (c) 2019- Armoha
//   MIT License. 자세한 것은 위 저장소의 LICENSE 를 보라.
//
// 주소·크기는 1.16.1 의 사실이고, 이름은 eudplib 의 멤버 이름을 그대로 쓴다.
// 개수(length)는 다음 자리까지의 틈으로 조였다 — 표를 길게 잡아 옆 항목을
// 삼키는 것보다 짧게 잡아 못 알아보는 편이 안전하기 때문이다. 틈이 넓은
// 자리는 이래도 실제보다 길게 잡힐 수 있다. 이름이 조금 넘겨 붙을 뿐
// 주소·크기와 값에는 영향이 없다.
//
// 리마스터 지원 여부(scr)는 eudplib 에 없다. 그것까지 보려면 EUD Book 의
// api.json 을 따로 얹는다 (README 의 EUD 절 참고).

// clang-format off
{{ "{tightened[0][1]}", 0x{tightened[0][0]:08X}, {tightened[0][2]}, {tightened[0][3]}, ScrSupport::Unknown, {{}} }},
""")
    for addr, name, size, length in tightened[1:]:
        out.write(f'{{ "{name}", 0x{addr:08X}, {size}, {length}, ScrSupport::Unknown, {{}} }},\n')
    out.write("// clang-format on\n")

    print(f"자리 {len(tightened)}개", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
