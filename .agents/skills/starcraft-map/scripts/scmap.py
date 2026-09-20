#!/usr/bin/env python3
"""splash-cli 를 파이썬에서 두드리는 얇은 껍데기.

맵을 만드는 스크립트들이 함께 쓴다. CLI 출력은 사람이 읽는 한국어라
갈라 읽는 자리를 여기 한 곳에 모아 둔다 — 출력 꼴이 바뀌면 여기만 고친다.
"""
from __future__ import annotations

import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

# --- 유닛 번호 (자주 쓰는 것만) ---
START_LOCATION = 214
MINERAL_1, MINERAL_2, MINERAL_3 = 176, 177, 178
MINERALS = (MINERAL_1, MINERAL_2, MINERAL_3)
VESPENE_GEYSER = 188

# 자원량 기본값 — 공식 리그 맵 56개에서 미네랄 1500 이 54개, 가스 5000 이
# 53개였다. 다른 값을 쓸 까닭이 없으면 이것을 쓴다.
MINERAL_AMOUNT = 1500
GAS_AMOUNT = 5000

TILE = 32  # 타일 한 칸의 픽셀


class CliError(RuntimeError):
    pass


def find_cli() -> str:
    """splash-cli 를 찾는다. 환경변수 > 빌드 폴더 > PATH 차례."""
    env = os.environ.get("SPLASH_CLI")
    if env:
        if not os.path.exists(env):
            raise CliError(f"SPLASH_CLI 가 가리키는 파일이 없습니다: {env}")
        return env

    here = os.path.dirname(os.path.realpath(__file__))
    # .claude/skills/starcraft-map/scripts → 저장소 뿌리
    root = os.path.abspath(os.path.join(here, "..", "..", "..", ".."))
    for build in ("build", "build-cli"):
        candidate = os.path.join(root, build, "src", "cli", "splash-cli")
        if os.path.exists(candidate):
            return candidate

    found = shutil.which("splash-cli")
    if found:
        return found
    raise CliError(
        "splash-cli 를 찾지 못했습니다. 먼저 빌드하세요:\n"
        "  cmake -S . -B build-cli -G Ninja -DSPLASH_BUILD_GUI=OFF -DSPLASH_BUILD_TESTS=OFF\n"
        "  cmake --build build-cli\n"
        "또는 SPLASH_CLI 에 경로를 지정하세요.")


def find_install() -> str:
    """StarCraft 설치 폴더. 지형·트리거 작업에 반드시 필요하다."""
    env = os.environ.get("SC_INSTALL")
    if env:
        return env
    raise CliError(
        "StarCraft 설치 폴더가 필요합니다. SC_INSTALL 에 지정하세요.\n"
        "  예: export SC_INSTALL=/Volumes/X31/StarCraft\n"
        "제대로 잡혔는지는 `splash-cli assets <경로>` 로 봅니다.")


class Cli:
    """한 맵을 붙들고 명령을 잇달아 보내는 손잡이."""

    def __init__(self, path: str, install: str | None = None, quiet: bool = True):
        self.path = path
        self.cli = find_cli()
        self._install = install
        self.quiet = quiet

    @property
    def install(self) -> str:
        if self._install is None:
            self._install = find_install()
        return self._install

    def run(self, *args: str, timeout: int = 300) -> str:
        """맵을 건드리지 않는 명령. 표준출력을 돌려준다."""
        proc = subprocess.run([self.cli, *args], capture_output=True, timeout=timeout)
        out = proc.stdout.decode("utf-8", "replace")
        if proc.returncode != 0:
            err = proc.stderr.decode("utf-8", "replace").strip()
            raise CliError(f"{' '.join(args[:3])} 실패: {err or out.strip()}")
        return out

    def edit(self, *args: str, timeout: int = 300) -> str:
        """맵을 고치는 명령. 언제나 --in-place 로 제자리에 쓴다.

        --in-place 는 옆에 먼저 쓰고 바꿔치기하므로, 쓰다 멈춰도 원본이
        남는다.
        """
        return self.run(*args, "--in-place", timeout=timeout)

    # --- 읽기 ---

    def info(self) -> dict:
        out = self.run("info", self.path)
        fields = {}
        for line in out.splitlines():
            if ":" in line:
                key, value = line.split(":", 1)
                fields[key.strip()] = value.strip()

        def number(key, default=0):
            m = re.search(r"-?\d+", fields.get(key, ""))
            return int(m.group()) if m else default

        size = re.search(r"(\d+)\s*x\s*(\d+)", fields.get("크기", ""))
        tileset = re.search(r"(.+?)\s*\((\d+)\)", fields.get("타일셋", ""))
        return {
            "name": fields.get("이름", ""),
            "width": int(size.group(1)) if size else 0,
            "height": int(size.group(2)) if size else 0,
            "tileset": tileset.group(1) if tileset else "",
            "tileset_id": int(tileset.group(2)) if tileset else -1,
            "version": fields.get("버전", ""),
            "units": number("유닛"),
            "locations": number("로케이션"),
            "triggers": number("트리거"),
            "strings": number("문자열"),
            "protected": fields.get("보호", "아니오") != "아니오",
        }

    _UNIT_RE = re.compile(
        r"^\s*(\d+)\s+(-?\d+),\s*(-?\d+)\s+P\s*(\d+)\s+(.+?)\s+\((\d+)\)"
        r"(?:\s+자원\s+(\d+))?\s*$")

    def units(self) -> list[dict]:
        out = self.run("unit", "list", self.path, "--limit", "100000")
        units = []
        for line in out.splitlines():
            m = self._UNIT_RE.match(line)
            if m:
                units.append({
                    "index": int(m.group(1)),
                    "x": int(m.group(2)), "y": int(m.group(3)),
                    "owner": int(m.group(4)),
                    "type_name": m.group(5).strip(),
                    "type": int(m.group(6)),
                    "resource": int(m.group(7)) if m.group(7) else None,
                })
        return units

    def tiles(self, x: int = 0, y: int = 0, w: int | None = None, h: int | None = None):
        """지형 타일을 2차원 표로. 기본은 맵 전체."""
        if w is None or h is None:
            info = self.info()
            w, h = w or info["width"], h or info["height"]
        out = self.run("terrain", "show", self.path, str(x), str(y), str(w), str(h))
        rows = {}
        for line in out.splitlines():
            m = re.match(r"^\s*(\d+)\s*\|\s*(.*)$", line)
            if m:
                rows[int(m.group(1))] = [int(t, 16) for t in m.group(2).split()]

        return [rows[k] for k in sorted(rows)]

    def terrain_types(self) -> dict[str, int]:
        """이 맵 타일셋의 ISOM 지형 이름 → 브러시 번호."""
        out = self.run("terrain", "types", self.path, "--install", self.install)
        types = {}
        for line in out.splitlines():
            m = re.match(r"^\s*(\d+)\s+(.+?)\s+\(index \d+\)\s*$", line)
            if m:
                types[m.group(2).strip()] = int(m.group(1))
        return types

    # --- 고치기 ---

    def isom(self, tile_x: int, tile_y: int, terrain: int, brush: int | None = None):
        """ISOM 붓질 한 번. 좌표는 타일, 안에서 픽셀로 바꾼다.

        ISOM 은 마름모 격자 위에서 움직인다 — 가로는 타일 두 칸이 한 칸이라
        가로 좌표는 짝수로 주는 편이 어긋나지 않는다.
        """
        args = ["terrain", "isom", self.path,
                str(tile_x * TILE), str(tile_y * TILE), str(terrain)]
        if brush is not None:
            args.append(str(brush))
        args += ["--install", self.install]
        self.edit(*args)

    def paste_tiles(self, tile_x: int, tile_y: int, rows: list[list[int]]):
        """타일 표를 그대로 찍는다 (.tiles 를 만들어 paste)."""
        with tempfile.NamedTemporaryFile("w", suffix=".tiles", delete=False,
                                         encoding="utf-8") as f:
            f.write("splash-tiles 1\n")
            f.write(f"{len(rows[0])} {len(rows)}\n")
            for row in rows:
                f.write(" ".join(f"{v:x}" for v in row) + "\n")
            tmp = f.name
        try:
            self.edit("terrain", "paste", self.path, str(tile_x), str(tile_y), tmp)
        finally:
            os.unlink(tmp)

    def place(self, unit: int | str, tile_x: int, tile_y: int, owner: int = 1,
              sub_x: int = 0, sub_y: int = 0):
        """유닛을 놓는다. 좌표는 타일, sub_* 로 픽셀 단위 미세 조정."""
        self.edit("unit", "place", self.path, str(unit),
                  str(tile_x * TILE + sub_x), str(tile_y * TILE + sub_y),
                  "--owner", str(owner))

    def set_resource(self, index: int, amount: int):
        self.edit("unit", "set", self.path, str(index), "--resource", str(amount))

    def set_map_name(self, name: str, description: str | None = None):
        args = ["map", "name", self.path, name]
        self.edit(*args)
        if description is not None:
            self.edit("map", "description", self.path, description)

    def apply_triggers(self, text: str):
        """SCMDraft 꼴 트리거 텍스트를 컴파일해 넣는다.

        `trigger apply` 는 --in-place 를 받지 않고 -o 만 받는다. 옆에 쓰고
        바꿔치기한다 — 쓰다 멈춰도 원본이 남는다.
        """
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                         encoding="utf-8") as f:
            f.write(text)
            tmp = f.name
        out = self.path + ".trig.tmp"
        try:
            self.run("trigger", "apply", self.path, tmp,
                     "--install", self.install, "-o", out)
            os.replace(out, self.path)
        finally:
            os.unlink(tmp)
            if os.path.exists(out):
                os.unlink(out)

    def trigger_text(self) -> str:
        """트리거를 텍스트로 뽑는다. 표준출력은 길면 잘리므로 파일로 받는다."""
        with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as f:
            tmp = f.name
        try:
            self.run("trigger", "show", self.path, tmp, "--install", self.install)
            with open(tmp, encoding="utf-8", errors="replace") as f:
                return f.read()
        finally:
            os.unlink(tmp)

    def roundtrip(self) -> bool:
        """열고 다시 저장했을 때 CHK 바이트가 그대로인지."""
        out = self.run("roundtrip", self.path)
        return "같습니다" in out or "동일" in out

    def render(self, out_path: str, units: bool = True, locations: bool = False):
        args = ["render", self.path, self.install, out_path]
        if units:
            args.append("--units")
        if locations:
            args.append("--locations")
        self.run(*args, timeout=600)


def new_map(path: str, width: int, height: int, tileset: int,
            terrain: str | None = None, melee: bool = True,
            install: str | None = None) -> Cli:
    """빈 맵을 만들고 손잡이를 돌려준다.

    **--install 과 --terrain 을 반드시 준다.** 주지 않으면 타일이 0 으로
    남아 ISOM 브러시가 아무것도 놓지 못한다 — 빈 칸 위에는 절벽을 이을
    수 없기 때문이다.
    """
    cli_path = find_cli()
    install = install or find_install()
    args = [cli_path, "new", path, str(width), str(height), str(tileset)]
    if melee:
        args.append("--melee")
    args += ["--install", install]
    if terrain:
        args += ["--terrain", terrain]
    proc = subprocess.run(args, capture_output=True)
    if proc.returncode != 0:
        raise CliError("새 맵 실패: " +
                       proc.stderr.decode("utf-8", "replace").strip())
    return Cli(path, install)


# --- 대칭 ---

def rotate(x: float, y: float, quarter_turns: int, width: int, height: int):
    """타일 좌표를 맵 한가운데를 축으로 90도씩 돌린다 (정사각형 맵 기준)."""
    for _ in range(quarter_turns % 4):
        x, y = (height - 1 - y), x
    return x, y


def mirror(x: float, y: float, axis: str, width: int, height: int):
    """좌우/상하/180도 뒤집기."""
    if axis == "horizontal":
        return width - 1 - x, y
    if axis == "vertical":
        return x, height - 1 - y
    if axis == "rot180":
        return width - 1 - x, height - 1 - y
    raise ValueError(f"모르는 대칭: {axis}")


def symmetric_points(x: float, y: float, symmetry: str, count: int,
                     width: int, height: int):
    """한 자리에 대응하는 대칭 자리들을 모두 돌려준다 (자기 자신 포함)."""
    if symmetry == "rot90":
        return [rotate(x, y, k, width, height) for k in range(4)]
    if symmetry == "rot180":
        return [(x, y), mirror(x, y, "rot180", width, height)]
    if symmetry == "horizontal":
        return [(x, y), mirror(x, y, "horizontal", width, height)]
    if symmetry == "vertical":
        return [(x, y), mirror(x, y, "vertical", width, height)]
    if symmetry == "radial":
        # 가운데를 축으로 count 개를 고르게 돌린다 (3인용·6인용).
        cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
        out = []
        for k in range(count):
            a = 2 * math.pi * k / count
            dx, dy = x - cx, y - cy
            out.append((cx + dx * math.cos(a) - dy * math.sin(a),
                        cy + dx * math.sin(a) + dy * math.cos(a)))
        return out
    raise ValueError(f"모르는 대칭: {symmetry}")


# --- 램프 ---
#
# ISOM 브러시에는 램프가 없다. `terrain types` 에 램프 항목이 없고, 고지대를
# 칠하면 절벽만 생긴다 (직접 확인함). 램프는 VF4 램프 비트가 선 타일을 직접
# 찍어야 놓인다.
#
# 램프 한 벌은 "연속한 타일 그룹 몇 줄 x 연속한 서브타일 몇 칸" 의
# 직사각 블록이다:
#     tile(r, c) = (기준그룹 + r) * 16 + (기준서브 + c)
# 아래 값은 공식 리그 맵 56개에서 쓰인 램프를 세어 가장 흔한 것을 골랐다.
# 첫 줄이 4칸인 것은 공식 맵이 그렇게 놓기 때문이다 — 오른쪽 위 두 칸은
# 둘레 지형을 그대로 둔다.
# **크기는 타일셋마다 다르다.** 6x6 인 줄 알고 Twilight 에 찍었더니 블록
# 밖 타일까지 건드려 검게 깨졌다 — 크기를 꼭 함께 쓴다.
#
# (이름, 기준값, 가로, 세로)
RAMPS = {
    0: [("Badlands", 0x4AB0, 6, 6), ("Badlands 다른 갈래", 0x4A50, 6, 6)],
    3: [("Ashworld", 0x4540, 6, 6), ("Ashworld 다른 갈래", 0x44E0, 6, 6)],
    4: [("Jungle", 0x4300, 6, 6), ("Jungle 다른 갈래", 0x4360, 6, 6)],
    7: [("Twilight", 0x4040, 6, 4), ("Twilight 다른 갈래", 0x4000, 6, 4)],
}


def ramp_rows(base: int, width: int, height: int, first_row: int = 0):
    group, sub = base // 16, base % 16
    return [[(group + r) * 16 + sub + c for c in range(width)]
            for r in range(first_row, first_row + height)]


def place_ramp(cli: Cli, tile_x: int, tile_y: int, base: int,
               width: int = 6, height: int = 6):
    """램프를 찍는다. (tile_x, tile_y) 는 램프 블록의 왼쪽 위.

    tile_y 는 **고지대가 끝나고 절벽이 시작되는 줄**에 맞춘다. tile_x 는
    짝수로 둔다 (ISOM 마름모 격자가 타일 두 칸이라 홀수면 어긋난다).

    첫 줄은 두 칸 좁게 찍는다 — 공식 맵이 그렇게 놓는다. 오른쪽 위 두
    칸은 둘레 지형을 그대로 둔다.
    """
    cli.paste_tiles(tile_x, tile_y, ramp_rows(base, max(1, width - 2), 1, 0))
    if height > 1:
        cli.paste_tiles(tile_x, tile_y + 1, ramp_rows(base, width, height - 1, 1))


def default_ramp(tileset_id: int):
    """그 타일셋에서 기본으로 쓸 램프. (기준값, 가로, 세로) 또는 None."""
    entries = RAMPS.get(tileset_id & 7)
    if not entries:
        return None
    _, base, width, height = entries[0]
    return base, width, height


# --- 자원 ---
#
# 본진 미네랄 배치. 공식 리그 맵 56개에서 스타팅 229곳을 재어 보니 본진
# 미네랄은 9개(144곳)가 가장 흔했고 가스는 1개(219곳)였다. 아래 값은
# 투혼의 본진을 잰 것으로, 스타팅 중심에서의 픽셀 차이다. 미네랄은
# 32픽셀(한 타일) 간격으로 두 줄에 걸쳐 늘어선다.
MAIN_MINERAL_OFFSETS = [
    (-192, -96), (-224, -64), (-192, -32), (-224, 0),
    (-192, 32), (-224, 64), (-192, 96), (-192, 128), (-160, 160),
]
# 가스는 미네랄 줄과 직각으로, 스타팅 바로 위 5.5타일에 둔다. 투혼·서킷
# 브레이커 모두 (0, ±176) 이었다. 미네랄과 너무 떨어뜨리면 게임에서 일꾼
# 동선이 갈라지고, 맵을 재는 쪽에서도 다른 멀티로 잡힌다.
MAIN_GAS_OFFSET = (0, -176)


def place_base(cli: Cli, tile_x: int, tile_y: int, owner: int,
               minerals: int = 9, gas: int = 1,
               facing: int = 0, width: int = 128, height: int = 128,
               mineral_amount: int = MINERAL_AMOUNT,
               gas_amount: int = GAS_AMOUNT,
               start_location: bool = True):
    """스타팅 한 곳을 통째로 놓는다 — 스타팅 표시 + 미네랄 + 가스.

    facing 은 90도 단위 회전 수다. 대칭으로 놓은 스타팅마다 같은 모양을
    돌려 쓰기 위한 것이다.
    """
    if start_location:
        cli.place(START_LOCATION, tile_x, tile_y, owner)

    def turn(dx, dy):
        for _ in range(facing % 4):
            dx, dy = -dy, dx
        return dx, dy

    placed = []
    kinds = (MINERAL_1, MINERAL_2, MINERAL_3)
    for i in range(minerals):
        dx, dy = MAIN_MINERAL_OFFSETS[i % len(MAIN_MINERAL_OFFSETS)]
        dx, dy = turn(dx, dy)
        cli.edit("unit", "place", cli.path, str(kinds[i % 3]),
                 str(tile_x * TILE + dx), str(tile_y * TILE + dy), "--owner", "12")
        placed.append(("mineral", dx, dy))
    for i in range(gas):
        dx, dy = turn(*MAIN_GAS_OFFSET)
        cli.edit("unit", "place", cli.path, str(VESPENE_GEYSER),
                 str(tile_x * TILE + dx), str(tile_y * TILE + dy + i * 96),
                 "--owner", "12")
        placed.append(("gas", dx, dy))

    return placed


def set_all_resources(cli: Cli, mineral_amount: int = MINERAL_AMOUNT,
                      gas_amount: int = GAS_AMOUNT) -> int:
    """맵에 놓인 자원의 남은 양을 한꺼번에 맞춘다.

    자원을 다 놓은 **뒤에 한 번만** 부른다. 놓을 때마다 부르면 맵을 통째로
    다시 읽게 되어 느리다.
    """
    changed = 0
    for unit in cli.units():
        if unit["type"] in MINERALS and unit["resource"] != mineral_amount:
            cli.set_resource(unit["index"], mineral_amount)
            changed += 1
        elif unit["type"] == VESPENE_GEYSER and unit["resource"] != gas_amount:
            cli.set_resource(unit["index"], gas_amount)
            changed += 1
    return changed


if __name__ == "__main__":
    # 손으로 확인할 때 쓴다.
    print("splash-cli :", find_cli())
    try:
        print("설치 폴더  :", find_install())
    except CliError as e:
        print("설치 폴더  : (없음)", e)
    if len(sys.argv) > 1:
        c = Cli(sys.argv[1])
        for key, value in c.info().items():
            print(f"  {key:12} {value}")
