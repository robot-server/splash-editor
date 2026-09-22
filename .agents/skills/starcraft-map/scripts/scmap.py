#!/usr/bin/env python3
"""splash-cli 를 파이썬에서 두드리는 얇은 껍데기.

맵을 만드는 스크립트들이 함께 쓴다. CLI 출력은 사람이 읽는 한국어라
갈라 읽는 자리를 여기 한 곳에 모아 둔다 — 출력 꼴이 바뀌면 여기만 고친다.
"""
from __future__ import annotations

import itertools
import json
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

# **놓으면 게임이 튕기는 유닛.** 리마스터에서 베타 시절 더미 유닛 대부분은
# 튕기지 않게 고쳐졌지만 아래는 여전히 튕긴다 (스타 에디터 아카데미 실측).
CRASHING_UNITS = {
    "Allan Turret", "Duke Turret type 1", "Duke Turret type 2",
    "Goliath Turret", "Tank Turret type 1", "Tank Turret type 2",
    "Terran Tank Turret", "Terran Goliath Turret",
}


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

    def doodad_catalogue(self) -> list[dict]:
        """이 맵 타일셋의 두뎃 목록. (번호, 가로, 세로, 갈래)"""
        out = self.run("doodad", "list", self.path, "--catalogue",
                       "--install", self.install)
        items = []
        for line in out.splitlines():
            m = re.match(r"^\s*(\d+)\s+(\d+)x\s*(\d+)\s+(.+?)\s*$", line)
            if m:
                items.append({"id": int(m.group(1)), "w": int(m.group(2)),
                              "h": int(m.group(3)), "kind": m.group(4).strip()})
        return items

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

    def isom_batch(self, strokes):
        """ISOM 붓질을 한꺼번에 놓는다. strokes 는 (타일x, 타일y, 지형) 목록.

        붓질마다 명령을 부르면 맵을 열고 저장하는 값이 붓질 값보다 훨씬
        크다 — 천 번 칠하는 데 몇 분이 걸린다. 한 번에 보낸다.
        """
        strokes = list(strokes)
        if not strokes:
            return 0
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                         encoding="utf-8") as f:
            for (tx, ty, terrain) in strokes:
                f.write(f"{tx * TILE} {ty * TILE} {terrain}\n")
            tmp = f.name
        try:
            self.edit("terrain", "isom-batch", self.path, tmp,
                      "--install", self.install, timeout=900)
        finally:
            os.unlink(tmp)
        return len(strokes)

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
        """유닛을 놓는다. 좌표는 타일, sub_* 로 픽셀 단위 미세 조정.

        **맵 밖에 놓으면 게임이 튕긴다.** 생성기가 좌표를 한 칸 잘못
        잡는 일은 흔한데 그 대가가 튕김이다. 여기서 막는다.

        놓으면 튕기는 유닛도 막는다 (포탑 더미 유닛 등).
        """
        if isinstance(unit, str) and unit in CRASHING_UNITS:
            raise CliError(f"'{unit}' 은 배치하면 게임이 튕깁니다. "
                           f"놓지 않습니다.")
        info = self.info()
        w, h = info["width"], info["height"]
        if not (0 <= tile_x < w and 0 <= tile_y < h):
            raise CliError(f"맵 밖에 유닛을 놓으려 했습니다: "
                           f"({tile_x},{tile_y}) — 맵은 {w}x{h} 입니다. "
                           f"그대로 두면 게임이 튕깁니다.")
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

    def apply_briefing(self, text: str):
        """브리핑을 컴파일해 넣는다. 트리거와 마찬가지로 통째로 간다."""
        import tempfile
        tf = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                         encoding="utf-8")
        try:
            tf.write(text); tf.close()
            out = self.path + ".brf.tmp"
            self.run("briefing", "apply", self.path, tf.name,
                     "--install", self.install, "-o", out)
            os.replace(out, self.path)
        finally:
            os.unlink(tf.name)

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

    def append_triggers(self, text: str):
        """있는 트리거 **뒤에** 더한다.

        `trigger apply` 는 텍스트 전체로 갈아 끼운다. 내용을 얹을 때마다
        전체를 다시 쓰면 앞서 쓴 것을 날리기 쉽다 — 실제로 날려 봤다.
        여기서는 지금 든 것을 뽑아 뒤에 붙인 뒤 넣는다.
        """
        current = self.trigger_text().rstrip()
        sep = "\n\n//-----------------------------------------------------------------//\n\n"
        self.apply_triggers((current + sep if current else "") + text)

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
# 방향마다 쓸 램프 블록. **전수 탐색으로 찾아 미니타일 길찾기로 검증했다.**
#
# 찾은 방법: 타일셋마다 고지대를 **맵을 가로지르는 벽**으로 세운 시험 맵을
# 만들고(세로 벽·가로 벽 두 벌), 램프 비트가 선 (그룹, 서브) 을 모두 후보로
# 찍어 본 뒤 벽 양쪽이 걸어서 이어지는지 확인했다.
#
# 벽으로 만드는 것이 중요하다. 고지대를 덩이로 두면 램프를 지나지 않고
# 옆으로 돌아가도 "통했다"가 되어 엉뚱한 값이 뽑힌다 — 실제로 그렇게
# 잘못된 표를 만들었다가 되돌렸다.
#
# **램프는 좌우로 나는 것이 더 흔하다.** 공식 밀리맵 46장에서 램프가 붙은
# 본진 57곳을 세어 보니 왼쪽 26, 오른쪽 23, 위 5, 아래 3 이었다. 위아래만
# 시도하면 대개 자리를 찾지 못한다.
#
# off 는 "고지대가 끝나는 줄/칸" 에서 블록을 몇 칸 밀지다.
# (이름, 블록 기준값, 가로, 세로, off)
RAMPS_BY_DIR = {
    # **타일이 다 있는지 먼저 보고, 미니타일 길찾기로 검증했다.**
    # 앞서 실은 표는 거의 전부 없는 타일을 찍고 있었다 — 그룹만 보고
    # 타일 존재를 안 봐서, 여섯 칸 중 네 칸만 있어도 그 네 칸으로 길이
    # 뚫려 "통과" 로 나왔다. 나머지 두 칸은 화면에 검은 구멍이 된다.
    #
    # 여기 없는 타일셋(Space 1, Desert 5, Ice 6, Twilight 7)은 **통하는
    # 램프를 아직 못 찾았다.** 그 타일셋에서는 본진을 평지에 둔다.
    # 없는 것을 억지로 찍으면 검은 구멍이 난 맵이 나온다.
    0: {   # Badlands
        "down": [("Badlands", 0x4a70, 6, 6, -3), ("Badlands2", 0x4a60, 6, 6, -3), ("Badlands3", 0x4a50, 6, 6, -3)],
        "up": [("Badlands", 0x4a70, 6, 6, -5), ("Badlands2", 0x4a60, 6, 6, -5), ("Badlands3", 0x4a50, 6, 6, -5)],
        "left": [("Badlands", 0x4a70, 6, 6, -7), ("Badlands2", 0x4a60, 6, 6, -7), ("Badlands3", 0x4a50, 6, 6, -7)],
        "right": [("Badlands", 0x4a70, 6, 6, 0), ("Badlands2", 0x4a60, 6, 6, 0), ("Badlands3", 0x4a50, 6, 6, 0)],
    },
    3: {   # Ashworld — 좌우만 찾았다
        "down": [],
        "up": [],
        "left": [("Ashworld", 0x4560, 6, 4, -7), ("Ashworld2", 0x4550, 6, 4, -7), ("Ashworld3", 0x4540, 6, 6, -7)],
        "right": [("Ashworld", 0x4560, 6, 4, 0), ("Ashworld2", 0x4550, 6, 4, 0), ("Ashworld3", 0x4540, 6, 6, 0)],
    },
    4: {   # Jungle
        "down": [("Jungle", 0x4320, 6, 6, -3), ("Jungle2", 0x4310, 6, 6, -3), ("Jungle3", 0x4300, 6, 6, -3)],
        "up": [("Jungle", 0x4320, 6, 6, -5), ("Jungle2", 0x4310, 6, 6, -5), ("Jungle3", 0x4300, 6, 6, -5)],
        "left": [("Jungle", 0x4320, 6, 6, -7), ("Jungle2", 0x4310, 6, 6, -7), ("Jungle3", 0x4300, 6, 6, -7)],
        "right": [("Jungle", 0x4320, 6, 6, 0), ("Jungle2", 0x4310, 6, 6, 0), ("Jungle3", 0x4300, 6, 6, 0)],
    },
}

# 옛 이름 — 방향을 가리지 않는다. 새 코드는 RAMPS_BY_DIR 을 쓴다.
RAMPS = {ts: [(n, b, w, h) for (n, b, w, h, _o) in d["down"]]
         for ts, d in RAMPS_BY_DIR.items()}


def ramp_rows(base: int, width: int, height: int, first_row: int = 0):
    group, sub = base // 16, base % 16
    return [[(group + r) * 16 + sub + c for c in range(width)]
            for r in range(first_row, first_row + height)]


def ramp_tiles_valid(cli: Cli, tileset_id: int, base: int, w: int, h: int) -> bool:
    """램프 블록의 타일이 **타일셋에 다 있는지** 본다.

    램프는 `(기준그룹+r)*16 + (기준서브+c)` 로 펼친 직사각형인데, 그룹
    마다 있는 변종 수가 다르다. 없는 변종을 찍으면 화면에 검은 구멍이
    난다. 길찾기만으로는 못 걸러진다 — 반드시 먼저 이걸 본다.
    """
    tiles = tileset_tiles(cli, tileset_id)
    g0, s0 = base >> 4, base & 15
    for r in range(h):
        for c in range(w):
            if ((g0 + r) * 16 + (s0 + c)) not in tiles:
                return False
    return True


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


def ramp_candidates(tileset_id: int, direction="down"):
    """그 타일셋·방향에서 시도해 볼 램프 목록.

    direction 은 "down"/"up"/"left"/"right", 또는 옛 코드를 위해 참/거짓
    (참이면 down, 거짓이면 up).
    """
    if isinstance(direction, bool):
        direction = "down" if direction else "up"
    d = RAMPS_BY_DIR.get(tileset_id & 7)
    if not d:
        return []
    return d.get(direction, [])


def default_ramp(tileset_id: int):
    """옛 이름. (기준값, 가로, 세로) 또는 None."""
    entries = ramp_candidates(tileset_id, True)
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
# 본진 자원 배치. **공식 밀리맵 46장의 본진 183곳을 전수로 재어** 얻었다.
# 미네랄 9개 + 가스 1개인 표준형 136곳 가운데 가장 흔한 배치(26곳)를 그대로
# 쓴다. 두 번째로 흔한 배치(22곳)는 이것의 상하 거울이라 out_y 로 뒤집으면
# 나온다.
#
# 스타팅 중심에서의 픽셀 차이다. 미네랄은 왼쪽 6~7타일에 세로줄로 서고,
# 가스는 스타팅 바로 위아래 **정확히 5.0타일**(±160)에 놓인다 — 가스
# 거리는 4분위가 5.0~5.0 으로 편차가 없었다.
#
# (앞서 투혼 한 장만 보고 가스를 -176 으로 잡았는데 0.5타일 어긋난 값이었다.)
MAIN_MINERAL_OFFSETS = [
    (-224, -48), (-224, 16), (-224, 80),
    (-192, -80), (-192, -16), (-192, 48), (-192, 112), (-192, 144),
    (-160, 176),
]
MAIN_GAS_OFFSET = (0, -160)

# 본진 언덕 크기 — 전수 중앙값은 34x34 타일, 넓이 720칸이다.
# 앞서 쓰던 23x15 는 절반도 안 되어 건물 지을 자리가 나오지 않았다.
MAIN_PLATEAU_HALF_W = 17
MAIN_PLATEAU_HALF_H = 17

# 스타팅에서 램프까지 중앙값 18.6타일. 그리고 **램프는 좌우로 난다** —
# 램프가 붙은 본진 57곳 중 왼쪽 26, 오른쪽 23, 위 5, 아래 3 이었다.
# 위아래만 시도하면 대개 찾지 못한다.
MAIN_RAMP_DISTANCE = 18

# 본진이 반드시 언덕인 것도 아니다: 183곳의 높이는
# 중지대 94 / 저지대 52 / 고지대 37 이고, 램프가 아예 없는 본진이 126곳이다.
# 좁은 길목으로 나가는 본진이 램프 있는 본진보다 흔하다.


def place_base(cli: Cli, tile_x: int, tile_y: int, owner: int,
               minerals: int = 9, gas: int = 1,
               out_x: int = -1, out_y: int = -1,
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
        """자원을 본진 **바깥쪽**으로 보낸다.

        돌리지 않고 뒤집기만 한다. 90도로 돌리면 자원이 램프 쪽으로
        가서 입구를 막는다 — 실제로 베스핀이 램프 위에 얹힌 적이 있다.
        """
        return dx * (1 if out_x < 0 else -1), dy * (1 if out_y < 0 else -1)

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


def setup_melee_players(cli: Cli, players: int):
    """밀리맵 플레이어 슬롯을 스타팅 수에 맞춘다.

    **스타팅보다 슬롯이 많으면 안 된다.** 대기실에서 자리를 받았는데
    스타팅이 없는 사람이 생긴다. 공식 리그 맵은 예외 없이 쓰는 만큼만
    열어 두고 나머지를 "사용 안 함" 으로 막는다 (투혼 4인용: P1~4 열림
    + 선택 가능, P5~8 사용 안 함).
    """
    for p in range(1, 9):
        if p <= players:
            cli.edit("player", "set", cli.path, str(p),
                     "--race", "userselect", "--slot", "open", "--force", "1")
        else:
            cli.edit("player", "set", cli.path, str(p),
                     "--race", "inactive", "--slot", "inactive")
    # 공식 맵은 세력 1 에 "시작 위치 섞기" 를 켜 둔다.
    cli.edit("force", "set", cli.path, "1", "--name", "Players",
             "--randomize-start", "on")


def setup_usemap_players(cli: Cli, humans: int, computers: list[int],
                         race: str = "terran"):
    """유즈맵 플레이어 슬롯을 정한다.

    **종족을 반드시 못 박는다.** "선택 가능"(userselect) 으로 두면 그
    슬롯이 밀리처럼 취급되어, 스타팅 포인트에서 **본진 + 일꾼 네 기**가
    나온다. 유즈맵에서는 치명적이다 — 건물 짓는 사이에 게임이 끝난다.
    실제로 그렇게 만들어 플레이해 보고 지적받았다.

    실측 유즈맵 329장의 사람 슬롯 종족:
        테란 72%, 저그 14%, 프로토스 11%, 선택 가능 **1%**

    유즈맵에서 트리거가 적으로 쓰는 플레이어는 **컴퓨터**여야 한다.
    "열림" 으로 두면 사람이 앉을 수 있는 빈 자리가 되고, 아무도 앉지
    않으면 그 플레이어의 유닛이 아예 생기지 않는다.
    """
    for p in range(1, 9):
        if p <= humans:
            cli.edit("player", "set", cli.path, str(p),
                     "--race", race, "--slot", "open", "--force", "1")
        elif p in computers:
            cli.edit("player", "set", cli.path, str(p),
                     "--race", "zerg", "--slot", "computer", "--force", "2")
        else:
            cli.edit("player", "set", cli.path, str(p),
                     "--race", "inactive", "--slot", "inactive")
    # 사람끼리는 한 편 — 협동 유즈맵의 기본이다.
    #
    # **시작 위치 섞기는 끈다.** 켜 두면 플레이어가 스타팅에 무작위로
    # 배정되어, P1 을 가리키는 트리거가 엉뚱한 자리를 본다. 자리마다
    # 트리거가 다른 유즈맵(디펜스 경기장 등)은 그대로 망가진다.
    # 실측: 1번 세력에 이 깃발을 켠 맵은 329장 중 10% 뿐이다.
    for f, name in ((1, "Players"), (2, "Enemy")):
        cli.edit("force", "set", cli.path, str(f), "--name", name,
                 "--allied-victory", "on", "--shared-vision", "on",
                 "--randomize-start", "off")


def reveal_for_all(cli: Cli, humans: int, spacing: int = 16):
    """사람 플레이어 **모두**에게 시야를 연다.

    한 사람 것만 깔면 나머지는 깜깜하다. 실측 유즈맵 329장 중 74% 가
    Map Revealer 를 쓰고, 쓰는 맵의 **주인 수 중앙값이 5명**이다
    (개수 중앙값 50개). 한 명만 깔아 두는 맵은 표본에 거의 없다.
    """
    for p in range(1, humans + 1):
        cli.edit("scenario", "revealers", cli.path,
                 "--owner", str(p), "--spacing", str(spacing))


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


# --- 높이로 지형 읽기 ---
#
# 타일 값만으로는 그 자리가 고지대인지 알 수 없다. 타일 그룹(값/16)마다
# 높이·걷기가 정해져 있고, `splash-cli tileset-groups` 가 그 표를 낸다.
# 절벽 줄을 "타일 값이 크게 바뀌는 곳" 으로 추측했다가 램프가 고지대
# 한가운데 파묻힌 적이 있다. 반드시 높이로 판정한다.

_GROUP_CACHE: dict[int, dict[int, tuple]] = {}


def terrain_groups(cli: Cli, tileset_id: int) -> dict[int, tuple]:
    """타일 그룹 → (높이, 걷기, 모두걷기, 짓기, 램프)."""
    key = tileset_id & 7
    if key in _GROUP_CACHE:
        return _GROUP_CACHE[key]
    out = cli.run("tileset-groups", cli.install, str(key))
    table = {}
    for line in out.splitlines():
        if line.startswith("#"):
            continue
        f = line.split()
        if len(f) == 7:
            table[int(f[0])] = (int(f[1]), int(f[2]), int(f[3]),
                                int(f[4]), int(f[5]), int(f[6], 16))
        elif len(f) == 6:      # 걷기비트가 없던 옛 형식
            table[int(f[0])] = tuple(int(v) for v in f[1:]) + (0xFFFF,)
    _GROUP_CACHE[key] = table
    return table


class Terrain:
    """맵 한 구역의 타일과 그 높이를 함께 들고 있는 것."""

    def __init__(self, cli: Cli, x: int, y: int, w: int, h: int, tileset_id: int):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.tiles = cli.tiles(x, y, w, h)
        self.groups = terrain_groups(cli, tileset_id)

    def _prop(self, tx: int, ty: int, index: int, default=0):
        ix, iy = tx - self.x, ty - self.y
        if not (0 <= ix < self.w and 0 <= iy < self.h):
            return default
        return self.groups.get(self.tiles[iy][ix] >> 4, (0, 0, 0, 0, 0))[index]

    def elevation(self, tx: int, ty: int) -> int:
        return self._prop(tx, ty, 0)

    def walkable(self, tx: int, ty: int) -> int:
        return self._prop(tx, ty, 1)


def find_elevation_edge(cli: Cli, tileset_id: int, tx: int, y_from: int, y_to: int,
                        vertical: bool = True):
    """높은 땅이 끝나고 낮은 땅이 되는 첫 줄(또는 칸)을 찾는다.

    (tx, y_from) 에서 y_to 쪽으로 훑는다. vertical=False 면 tx 가 세로
    좌표이고 y_from~y_to 가 가로 범위다.
    """
    lo, hi = (y_from, y_to) if y_from <= y_to else (y_to, y_from)
    if vertical:
        t = Terrain(cli, tx, lo, 1, hi - lo + 1, tileset_id)
        seq = range(y_from, y_to + (1 if y_to >= y_from else -1),
                    1 if y_to >= y_from else -1)
        prev = None
        for ty in seq:
            e = t.elevation(tx, ty)
            if prev is not None and prev >= 1 and e == 0:
                return ty
            prev = e
        return None
    t = Terrain(cli, lo, tx, hi - lo + 1, 1, tileset_id)
    seq = range(y_from, y_to + (1 if y_to >= y_from else -1),
                1 if y_to >= y_from else -1)
    prev = None
    for cx in seq:
        e = t.elevation(cx, tx)
        if prev is not None and prev >= 1 and e == 0:
            return cx
        prev = e
    return None


def place_ramp_checked(cli: Cli, tileset_id: int, edge: int, fixed: int,
                       high_point, low_point, direction: str = "down",
                       candidates=None, shifts=range(-7, 8)):
    """램프를 찍고 **실제로 걸어서 통하는지** 확인한다. 안 되면 되돌린다.

    direction 이 down/up 이면 `edge` 는 고지대가 끝나는 **줄**이고 `fixed`
    는 가로 자리다. left/right 면 `edge` 가 **칸**이고 `fixed` 가 세로 자리다.

    타일 단위 "걸을 수 있는 칸이 하나라도 있는가" 로는 판정할 수 없다.
    게임은 미니타일 단위로 길을 찾으므로 그 격자에서 high_point 에서
    low_point 까지 따라가 본다.
    """
    horizontal = direction in ("left", "right")
    if candidates is None:
        candidates = ramp_candidates(tileset_id, direction)
    if not candidates:
        return None

    info = cli.info()
    mw, mh = info["width"], info["height"]
    hx, hy = high_point
    lx, ly = low_point
    rx0 = max(0, min(hx, lx) - 12)
    ry0 = max(0, min(hy, ly) - 12)
    rw = min(abs(hx - lx) + 26, mw - rx0)
    rh = min(abs(hy - ly) + 26, mh - ry0)

    width = max(c[2] for c in candidates)
    height = max(c[3] for c in candidates)

    tries = []
    # 타일이 실제로 다 있는 후보만 남긴다. 없는 변종을 찍으면 화면에
    # 검은 구멍이 나는데, 길찾기만으로는 그걸 못 걸러낸다.
    candidates = [c for c in candidates
                  if ramp_tiles_valid(cli, tileset_id, c[1], c[2], c[3])]
    if not candidates:
        return None

    for entry in candidates:
        name, base, w, h = entry[0], entry[1], entry[2], entry[3]
        own = entry[4] if len(entry) > 4 else None
        if own is not None:
            tries.append((edge + own, name, base, w, h))
    for shift in shifts:
        for entry in candidates:
            tries.append((edge + shift, entry[0], entry[1], entry[2], entry[3]))

    seen = set()
    for pos, name, base, w, h in tries:
        if horizontal:
            x, y = pos, fixed
        else:
            x, y = fixed, pos
        if x < 0 or y < 0 or x + w > mw or y + h > mh:
            continue
        if (x, y, base, w, h) in seen:
            continue
        seen.add((x, y, base, w, h))
        before = cli.tiles(x, y, width, height)
        try:
            place_ramp(cli, x, y, base, w, h)
        except CliError:
            continue
        grid = walk_grid(cli, tileset_id, rx0, ry0, rw, rh)
        a_ = nearest_walkable(grid, (hx - rx0) * 4 + 2, (hy - ry0) * 4 + 2)
        b_ = nearest_walkable(grid, (lx - rx0) * 4 + 2, (ly - ry0) * 4 + 2)
        if a_ and b_ and walk_reachable(grid, a_, b_):
            return base, x, y
        cli.paste_tiles(x, y, before)      # 되돌린다

    return None


# --- 미니타일 단위 길찾기 ---
#
# 게임은 타일이 아니라 **미니타일**(타일당 4x4) 단위로 길을 찾는다.
# "이 타일에 걸을 수 있는 칸이 하나라도 있는가" 로는 연결성을 판정할 수
# 없다 — 그 잣대로 램프가 이어졌다고 잘못 판단한 적이 있다.

def walk_grid(cli: Cli, tileset_id: int, x: int, y: int, w: int, h: int):
    """구역을 미니타일 격자(4배 해상도)의 걷기 여부로 편다.

    **타일마다** 걷기 비트를 본다. 그룹 단위로 보면 안 된다 — 그룹
    하나에 변종이 열여섯인데 램프는 변종마다 걷기 비트가 다르다.
    그룹 비트를 쓰면 램프가 통째로 막힌 것으로 잡혀, 멀쩡한 맵이
    "스타팅이 갇혔다" 고 나온다 (실제로 그렇게 틀렸었다).
    """
    t = Terrain(cli, x, y, w, h, tileset_id)
    per_tile = tileset_tiles(cli, tileset_id)
    grid = [[0] * (w * 4) for _ in range(h * 4)]
    for ty in range(h):
        row = t.tiles[ty]
        for tx in range(w):
            v = row[tx]
            p = per_tile.get(v)
            if p is None:
                # **표에 없는 타일은 깨진 타일이다.** 화면에 검게 나오고
                # 게임도 제대로 다루지 못한다. 그룹 비트로 물러서면
                # 무효한 램프가 "길찾기 통과" 로 나온다 — 실제로 그랬다.
                continue
            mask = p[4]
            if not mask:
                continue
            base = ty * 4
            for my in range(4):
                g = grid[base + my]
                for mx in range(4):
                    if mask & (1 << (my * 4 + mx)):
                        g[tx * 4 + mx] = 1
    return grid


def walk_reachable(grid, start, goal) -> bool:
    """미니타일 격자에서 start 에서 goal 까지 걸어갈 수 있는지 (4방향)."""
    from collections import deque
    H, W = len(grid), len(grid[0])
    sx, sy = start
    gx, gy = goal
    if not (0 <= sx < W and 0 <= sy < H and grid[sy][sx]):
        return False
    if not (0 <= gx < W and 0 <= gy < H and grid[gy][gx]):
        return False
    seen = [[False] * W for _ in range(H)]
    q = deque([(sx, sy)])
    seen[sy][sx] = True
    while q:
        cx, cy = q.popleft()
        if (cx, cy) == (gx, gy):
            return True
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cx + dx, cy + dy
            if 0 <= nx < W and 0 <= ny < H and not seen[ny][nx] and grid[ny][nx]:
                seen[ny][nx] = True
                q.append((nx, ny))
    return False


def nearest_walkable(grid, mx: int, my: int, radius: int = 24):
    """(mx, my) 근처에서 걸을 수 있는 미니타일을 찾는다."""
    H, W = len(grid), len(grid[0])
    for r in range(radius):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if max(abs(dx), abs(dy)) != r:
                    continue
                nx, ny = mx + dx, my + dy
                if 0 <= nx < W and 0 <= ny < H and grid[ny][nx]:
                    return (nx, ny)
    return None


# --- 불규칙한 덩이 ---
#
# 같은 직사각형을 일정 간격으로 찍으면 한눈에 "기계가 만들었다" 가 보인다.
# 실제 맵의 지형은 가장자리가 들쭉날쭉하고 크기도 제각각이다. 무작위
# 걸음으로 덩이를 키워 모양을 흐트러뜨린다.

def organic_blob(rng, area: int, elongate: float = 1.0):
    """가운데에서 무작위로 번져 나가는 덩이. (dx, dy) 오프셋 집합.

    area 는 대략의 칸 수, elongate 가 1 보다 크면 가로로 길어진다.
    ISOM 은 가로 두 칸이 한 걸음이라 dx 는 짝수만 쓴다.
    """
    cells = {(0, 0)}
    frontier = [(0, 0)]
    while len(cells) < area and frontier:
        cx, cy = rng.choice(frontier)
        # 가로로 늘이고 싶으면 가로 이웃을 더 자주 고른다
        steps = [(2, 0), (-2, 0)] * max(1, int(round(elongate * 2))) + [(0, 1), (0, -1)]
        dx, dy = rng.choice(steps)
        cell = (cx + dx, cy + dy)
        if cell in cells:
            continue
        cells.add(cell)
        frontier.append(cell)
        if len(frontier) > area:
            frontier.pop(0)
    # 가장자리를 한 번 더 흐트러뜨린다 — 튀어나온 칸을 몇 개 지운다
    edge = [c for c in cells
            if sum(((c[0] + dx, c[1] + dy) in cells)
                   for dx, dy in ((2, 0), (-2, 0), (0, 1), (0, -1))) <= 2]
    for c in edge:
        if len(cells) > area * 0.6 and rng.random() < 0.45:
            cells.discard(c)
    return cells


def rotate_offset(dx: float, dy: float, quarter_turns: int):
    """덩이 모양을 90도 단위로 돌린다 (대칭 자리에 같은 모양을 놓으려고)."""
    for _ in range(quarter_turns % 4):
        dx, dy = -dy, dx
    return dx, dy


# --- 타일 변종 흩기 ---
#
# ISOM 솔버는 같은 지형을 늘 같은 변종으로 채운다. 그 결과 맵 전체가
# 한 패턴으로 깔려, 여덟 칸 떨어진 자리에도 같은 타일이 되풀이된다.
# 공식 맵을 재어 보면 여덟 칸 주기 반복이 1~2% 인데 그렇게 만든 맵은
# 6% 를 넘는다 — 눈에 바둑판처럼 보이는 까닭이다.
#
# 같은 그룹 안에서 **높이·걷기·짓기가 똑같은** 변종끼리만 바꿔치기하면
# 그림만 달라지고 게임 동작은 그대로다.

_TILE_CACHE: dict[int, dict[int, tuple]] = {}


def tileset_tiles(cli: Cli, tileset_id: int) -> dict[int, tuple]:
    """타일 번호 → (높이, 걷기, 짓기, 램프, 걷기비트)."""
    key = tileset_id & 7
    if key in _TILE_CACHE:
        return _TILE_CACHE[key]
    out = cli.run("tileset-tiles", cli.install, str(key), timeout=600)
    table = {}
    for line in out.splitlines():
        if line.startswith("#"):
            continue
        f = line.split()
        if len(f) == 6:
            table[int(f[0])] = (int(f[1]), int(f[2]), int(f[3]),
                                int(f[4]), int(f[5], 16))
    _TILE_CACHE[key] = table
    return table


def interchangeable_variants(cli: Cli, tileset_id: int) -> dict[int, list[int]]:
    """타일 번호 → 바꿔 써도 되는 같은 그룹 변종들."""
    tiles = tileset_tiles(cli, tileset_id)
    buckets: dict[tuple, list[int]] = {}
    for tile_id, props in tiles.items():
        buckets.setdefault((tile_id >> 4,) + props, []).append(tile_id)
    return {t: buckets[(t >> 4,) + p] for t, p in tiles.items()}


def scatter_tile_variants(cli: Cli, tileset_id: int, rng, chance: float = 0.6,
                          region=None) -> int:
    """지형 그림을 흩는다. 게임 동작은 그대로 두고 되풀이만 깬다.

    chance 는 바꿔 볼 타일의 비율이다. 바꿀 수 있는 변종이 하나뿐이면
    그대로 둔다.
    """
    info = cli.info()
    x0, y0, w, h = region or (0, 0, info["width"], info["height"])
    grid = cli.tiles(x0, y0, w, h)
    swap = interchangeable_variants(cli, tileset_id)

    changed = 0
    for y in range(h):
        row = grid[y]
        for x in range(w):
            if rng.random() > chance:
                continue
            options = swap.get(row[x])
            if not options or len(options) < 2:
                continue
            pick = rng.choice(options)
            if pick != row[x]:
                row[x] = pick
                changed += 1
    if changed:
        cli.paste_tiles(x0, y0, grid)
    return changed


def paint_floor_mixed(cli: Cli, tileset_id: int, rng, regions, groups,
                      need_build: bool = False, elevation: int | None = None) -> int:
    """바닥을 **여러 지형 그룹을 덩이로 섞어** 칠한다.

    한 그룹만 쓰면 서로 다른 타일이 16개를 못 넘는다. 실측 유즈맵의
    아래 사분위가 141개다 — 그룹을 여럿 써야 닿는다.

    덩이(가장 가까운 씨앗)로 칠해야 얼룩덜룩하지 않고 결이 생긴다.
    `need_build` 를 켜면 건물을 지을 수 있는 타일만 쓴다. 바닥 대부분은
    걷기만 되면 되므로 기본은 끈다 — 켜면 쓸 수 있는 변종이 확 줄어
    타일 가짓수가 모자란다.

    **고도는 반드시 하나로 묶는다.** 스타크래프트는 낮은 곳에서 높은
    곳을 치면 빗나간다. 싸움터 바닥에 고도가 섞이면 같은 자리에서도
    명중률이 들쭉날쭉해진다. `elevation` 을 주지 않으면 첫 그룹의
    고도를 따르고 다른 고도의 그룹은 버린다.
    """
    tiles = tileset_tiles(cli, tileset_id)
    pool = {}
    for g in groups:
        good = [t for t in range(g * 16, g * 16 + 16)
                if t in tiles and tiles[t][1] and (tiles[t][2] or not need_build)]
        if not good:
            continue
        lvl = tiles[good[0]][0]
        if elevation is None:
            elevation = lvl                 # 첫 그룹의 고도로 맞춘다
        if lvl != elevation:
            continue
        pool[g] = good
    if not pool:
        raise CliError("바닥으로 쓸 타일을 찾지 못했습니다.")
    keys = list(pool)
    painted = 0
    for (rx, ry, rw, rh) in regions:
        if rw <= 0 or rh <= 0:
            continue
        grid = cli.tiles(rx, ry, rw, rh)
        seeds = [(rng.randrange(rw), rng.randrange(rh), rng.choice(keys))
                 for _ in range(max(4, rw * rh // 70))]
        for y in range(rh):
            for x in range(rw):
                best, bg = None, keys[0]
                for (sx, sy, g) in seeds:
                    d = (sx - x) ** 2 + (sy - y) ** 2
                    if best is None or d < best:
                        best, bg = d, g
                grid[y][x] = rng.choice(pool[bg])
        cli.paste_tiles(rx, ry, grid)
        painted += rw * rh
    return painted



# ------------------------------------------------------------ 지형 팔레트
#
# **왜 있는가.** 만든 맵 여섯 장을 실제 인기 유즈맵과 나란히 그려 보고
# 알았다. 트리거가 아니라 **바닥**이 달랐다. 실측 인기 유즈맵 86장:
#
#     1% 넘게 쓰는 타일 그룹   중앙 10개 (사분위 6~12)
#     검은 칸(그룹 0)          중앙 0.0% (사분위 0.0~0.0)
#     비콘이 둘레와 다른 지형 위에 놓인 비율   624개 중 94%
#
# 내가 만든 여섯 장은 전부 **그룹 1개 · 검은 칸 55~89% · 발판 0%** 였다.
# 한 가지 바닥을 깔고 벽 자리를 검게 뚫는 식이었다. 그러면 게임에서
# 맵에 구멍이 난 것처럼 보이고, 어디가 길이고 어디가 발판인지 읽히지
# 않는다. 실제 맵은 **지형을 맵 전체에 깔고** 못 걷는 지형(물·용암)으로
# 막으며, 방·통로·발판을 서로 다른 그룹으로 나눈다.
#
# 여기서 그 네 가지 몫을 타일셋마다 실측 분포에서 고른다.

PALETTE_ROLES = ("floor", "path", "rim", "pad", "wall")

_COLOR_CACHE: dict | None = None
_TS_NAMES = {0: "badlands", 1: "space", 2: "installation", 3: "ashworld",
             4: "jungle", 5: "desert", 6: "ice", 7: "twilight"}


def _tile_colors(tileset_id: int) -> dict[int, tuple[int, int, int]]:
    """타일 그룹 → 렌더러로 잰 평균 RGB. `measure_tile_colors.py` 가 만든다."""
    global _COLOR_CACHE
    if _COLOR_CACHE is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "data", "tile-colors.json")
        try:
            with open(os.path.normpath(path), encoding="utf-8") as f:
                raw = json.load(f)["colors"]
        except Exception:
            raw = {}
        _COLOR_CACHE = {n: {int(k): tuple(v) for k, v in d.items()}
                        for n, d in raw.items()}
    return _COLOR_CACHE.get(_TS_NAMES.get(tileset_id & 7, ""), {})


def _color_dist(a, b) -> float:
    """두 색이 얼마나 다른가. 하나라도 모르면 '아주 멀다' 로 친다."""
    if not a or not b:
        return 999.0
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


class Palette:
    """방·통로·발판·벽에 쓸 지형 한 벌.

    - `floor` 방 바닥, `path` 통로, `pad` 비콘·시작 발판 — 셋 다 걷을 수
      있고 **고도가 같다.** (낮은 데서 높은 데를 치면 46.9% 빗나간다.)
    - `wall` 은 걸을 수 없는 그룹이다. **검은 칸(그룹 0)이 아니다.**

    고르는 차례: 실측 유즈맵이 그 타일셋에서 많이 쓴 그룹 순으로 보고,
    타일 표에서 실제로 걷을 수 있는지·고도가 같은지 확인해 담는다.
    """

    def __init__(self, cli: "Cli", tileset_id: int, rng,
                 kind: str = "usemap", elevation: int | None = None):
        import corpus as _corpus
        self.rng = rng
        self.tileset_id = tileset_id & 7
        tiles = tileset_tiles(cli, self.tileset_id)
        try:
            order = _corpus.pick_floor_groups(
                _corpus.load(), self.tileset_id, kind, 64)
        except Exception:
            order = []
        try:
            wgt0 = _corpus.group_weights(_corpus.load(), kind, self.tileset_id)
        except Exception:
            wgt0 = {}
        # **실측이 넓게 깐 그룹만 쓴다.** 그러지 않으면 전이 타일이나
        # 두뎃 밑그림 그룹이 바닥으로 뽑혀 통짜로 깔았을 때 깨져 보인다.
        order = [g for g in order if wgt0.get(g, 0.0) >= 0.005] or order
        seen = set(order)
        order += [g for g in sorted({t >> 4 for t in tiles}) if g not in seen]

        # **물·용암 그룹은 타일이 여섯 개뿐이다.** 앞서 "변종 여덟 개
        # 이상" 을 걸었더니 벽으로 쓸 그룹이 하나도 안 잡혀 검은 칸으로
        # 물러섰다. 걷는 바닥은 결이 필요하니 넷 이상, 벽은 하나면 된다.
        def variants(g, want_walk, least):
            out = [t for t in range(g * 16, g * 16 + 16)
                   if t in tiles and bool(tiles[t][1]) == want_walk]
            return out if len(out) >= least else []

        # 걷는 그룹을 고도별로 모은다
        by_lvl: dict[int, list[tuple[int, list[int]]]] = {}
        blocked: list[tuple[int, list[int]]] = []
        for g in order:
            # 그룹 0 은 검은 칸. 그룹 1 은 어느 타일셋에서나 같은 갈회색
            # 이 나오는 빈 자리용이다 — 바닥으로 쓰면 안 된다.
            # 1024 부터는 두뎃 밑그림이라 통짜로 깔면 깨져 보인다.
            if g <= 1 or g >= 1024:
                continue
            w = variants(g, True, 4)
            if w:
                by_lvl.setdefault(tiles[w[0]][0], []).append((g, w))
                continue
            b = variants(g, False, 1)
            if b:
                blocked.append((g, b))
        if not by_lvl:
            raise CliError(f"타일셋 {self.tileset_id} 에서 걸을 수 있는 "
                           f"그룹을 찾지 못했습니다.")
        # **고도는 실측이 많이 쓴 쪽으로 고른다.** 그룹 개수로 고르면
        # Badlands 가 고지대(고도 2)로 잡힌다 — 실제 유즈맵 바닥은
        # 낮은 흙이다.
        try:
            wgt = _corpus.group_weights(_corpus.load(), kind, self.tileset_id)
        except Exception:
            wgt = {}
        if elevation is None or elevation not in by_lvl:
            elevation = max(by_lvl, key=lambda l: (
                sum(wgt.get(g, 0.0) for g, _ in by_lvl[l]), len(by_lvl[l])))
        self.elevation = elevation
        walkable = by_lvl[elevation]

        # 몫을 나눈다. **겹치면 안 된다** — 겹치면 눈으로 구분이 안 된다.
        # 몫 하나에 그룹 하나다. 여럿 담아 칸마다 뽑으면 소금후추처럼
        # 얼룩진다 (그렇게 만들어 보고 지웠다). 실제 맵은 구획마다 한
        # 그룹을 쭉 깔고, 구획이 바뀔 때 그룹이 바뀐다.
        #
        # **많이 쓰는 순서대로 넷을 집으면 안 된다.** Ice 의 그룹 2 와 3 은
        # 둘 다 눈밭이라 화면에서 한 덩어리로 보인다. 렌더러로 잰 색
        # (`data/tile-colors.json`) 을 보고 **서로 먼 색**을 고른다.
        order4 = [g for g, _ in walkable]
        col = _tile_colors(self.tileset_id)
        head = order4[:14]                   # 실측이 넓게 쓴 것 안에서만 고른다
        # **바닥은 벽보다 밝아야 한다.** 가장 많이 쓴 그룹을 그냥 바닥으로
        # 삼으니 Badlands 에서 (28,27,28) 짜리 거의 검은 지형이 뽑혔다.
        # 지형은 제대로 깔렸는데 그림은 여전히 구멍처럼 보였다. 실제
        # 인기 맵은 방 바닥이 둘레보다 밝다 — 그래야 방으로 읽힌다.
        lum = lambda g: sum(col.get(g, (90, 90, 90))) / 3.0

        # **벽을 먼저 고른다.** 걷는 네 몫은 벽보다 밝아야 한다 — 통로가
        # 벽보다 어두우면 길이 구멍처럼 보인다 (Badlands 에서 통로 밝기
        # 27, 벽 44 가 나와 실제로 그렇게 그려졌다). 벽을 나중에 고르면
        # 그 검사를 할 수가 없다.
        blocked_head = [g for g, _ in blocked[:8]]
        top_walk = max((lum(g) for g in head[:8]), default=90.0)
        dark = [g for g in blocked_head if lum(g) < top_walk - 8]
        self.wall = (dark or blocked_head)[:1]
        wall_lum = lum(self.wall[0]) if self.wall else 0.0

        # 걷는 몫 후보: 벽보다 밝은 것. 넷이 안 되면 밝은 순으로 채운다.
        bright = [g for g in head if lum(g) > wall_lum + 6]
        if len(bright) < 4:
            bright = sorted(head, key=lum, reverse=True)[:max(4, len(bright))]
        # **색이 같은 그룹은 하나만 남긴다.** Jungle 의 그룹 14 와 15 는
        # 잰 색이 똑같다 — 둘을 다른 몫에 넣으면 나눈 셈만 되고 화면은
        # 그대로다. 색을 굵게 묶어 대표만 남기고, 실측을 많이 쓴 쪽을
        # 대표로 둔다.
        seen_col, uniq = set(), []
        for g in bright:
            c = col.get(g)
            key = tuple(v // 12 for v in c) if c else ("?", g)
            if key in seen_col:
                continue
            seen_col.add(key)
            uniq.append(g)
        # 넷이 안 남으면 **묶은 것을 풀어서** 채운다. 색이 좀 가까운 것이
        # 같은 그룹을 두 몫에 넣는 것보다 낫다 (그렇게 해서 테두리와
        # 바닥이 같은 그룹으로 나온 판이 있었다).
        for extra in (bright, sorted(head, key=lum, reverse=True)):
            for g in extra:
                if len(uniq) >= 4:
                    break
                if g not in uniq:
                    uniq.append(g)
        bright = uniq

        # **넷을 한꺼번에 고른다.** 하나씩 탐욕적으로 집으면 앞의 몫만
        # 대비가 좋고 뒤로 갈수록 남은 것끼리 비슷해진다 — 섬과 통로가
        # 똑같은 눈밭으로 나왔다. 후보가 열두 개를 넘지 않으니 네 개
        # 조합을 다 훑어 **가장 가까운 두 색의 거리가 가장 큰** 조합을
        # 쓴다 (495가지, 셈이 가볍다).
        pool = bright[:12]
        best_set, best_gap = None, -1.0
        if len(pool) >= 4:
            for combo in itertools.combinations(pool, 4):
                gap = min(_color_dist(col.get(x), col.get(y))
                          for x, y in itertools.combinations(combo, 2))
                if gap > best_gap:
                    best_gap, best_set = gap, combo
        if best_set is None:
            best_set = tuple((pool * 4)[:4]) if pool else (1, 1, 1, 1)
            best_gap = 0.0

        # 몫을 나눈다. 바닥은 가장 밝은 것 — 방이 둘레보다 밝아야 방으로
        # 읽힌다. 나머지 셋은 **맞닿는 짝**을 보고 배치한다. 화면에서
        # 서로 붙는 짝은 셋뿐이다:
        #
        #     바닥–발판 (발판은 바닥 위에 놓인다)
        #     바닥–테두리 (테두리가 방을 두른다)
        #     통로–테두리 (통로가 방 테두리와 만난다)
        #
        # "바닥과 먼 순서" 로 그냥 꽂으면 남은 둘이 통로와 테두리가 되어
        # 서로 똑같은 색이 될 수 있다 (Jungle 그룹 14·15 는 잰 색이 같다).
        # 여섯 가지 배치를 다 보고 맞닿는 짝의 최솟값이 큰 쪽을 쓴다.
        rest = list(best_set)
        floor_g = max(rest, key=lum)
        rest.remove(floor_g)
        d = lambda x, y: _color_dist(col.get(x), col.get(y))
        best_asg, best_score = None, -1.0
        for pad_g, path_g, rim_g in itertools.permutations(rest):
            score = min(d(floor_g, pad_g), d(floor_g, rim_g), d(path_g, rim_g))
            if score > best_score:
                best_score, best_asg = score, (pad_g, path_g, rim_g)
        chosen = [floor_g] + list(best_asg)
        best_gap = min(best_gap, best_score) if best_gap >= 0 else best_score
        self.floor, self.pad, self.path, self.rim = ([g] for g in chosen[:4])
        self.color_gap = best_score
        self.void_wall = not self.wall      # 물러설 곳이 없으면 검은 칸
        self._pool = {g: v for g, v in walkable}
        self._pool.update({g: v for g, v in blocked})

    # -- 쓰는 쪽 ---------------------------------------------------------
    def groups(self, role: str) -> list[int]:
        return getattr(self, role)

    def tile(self, role: str) -> int:
        """그 몫의 타일 하나 — **그룹은 하나로 고정**, 변종만 바뀐다."""
        gs = self.groups(role)
        if not gs:
            return 0
        return self.rng.choice(self._pool[gs[0]])

    def fill(self, cli: "Cli", role: str, x: int, y: int, w: int, h: int):
        """네모 한 칸을 그 몫으로 칠한다.

        **한 구획은 한 그룹이다.** 칸마다 그룹을 다시 뽑으면 눈밭·풀밭·
        흙바닥이 뒤섞인 소금후추 무늬가 된다 — 실제로 그렇게 그려 보고
        걷어냈다. 그룹은 고정하고 **같은 그룹 안의 변종만** 흩어서 결을
        낸다 (실측 유즈맵의 서로 다른 타일 수 중앙값이 141개다).
        """
        if w <= 0 or h <= 0:
            return
        gs = self.groups(role)
        if not gs and role == "wall":
            cli.edit("terrain", "fill", cli.path, str(x), str(y),
                     str(w), str(h), "0")
            return
        rows = [[self.tile(role) for _ in range(w)] for _ in range(h)]
        cli.paste_tiles(x, y, rows)

    def describe(self) -> str:
        return (f"바닥 {self.floor[0]} · 통로 {self.path[0]} · "
                f"테두리 {self.rim[0]} · 발판 {self.pad[0]} · "
                f"벽 {self.wall[0] if self.wall else '검은 칸(물러섬)'} "
                f"(고도 {self.elevation}, 색 차 {self.color_gap:.0f})")


def cover_map(cli: "Cli", pal: Palette, width: int, height: int):
    """**맵 전체를 벽 지형으로 덮는다.**

    앞서 `terrain fill … 0` 으로 검게 덮고 방만 뚫었다. 실측 유즈맵
    485장의 검은 칸 중앙값은 0.0% 다 — 아무도 그렇게 하지 않는다.
    """
    pal.fill(cli, "wall", 0, 0, width, height)


def room(cli: "Cli", pal: Palette, x: int, y: int, w: int, h: int,
         rim: int = 1, role: str = "floor"):
    """방 하나를 판다 — 바닥을 깔고 **테두리를 다른 지형으로** 두른다.

    테두리가 있어야 방이 방으로 읽힌다. 실제 인기 디펜스 맵은 예외 없이
    방마다 테두리를 둘렀다.
    """
    pal.fill(cli, role, x, y, w, h)
    if rim > 0 and w > 2 * rim and h > 2 * rim:
        pal.fill(cli, "rim", x, y, w, rim)
        pal.fill(cli, "rim", x, y + h - rim, w, rim)
        pal.fill(cli, "rim", x, y + rim, rim, h - 2 * rim)
        pal.fill(cli, "rim", x + w - rim, y + rim, rim, h - 2 * rim)


def pad(cli: "Cli", pal: Palette, tile_x: int, tile_y: int,
        w: int = 3, h: int = 3):
    """비콘·시작 자리 밑에 **발판**을 깐다.

    실측 비콘 624개 중 590개(94%)가 둘레와 다른 지형 위에 놓여 있었다.
    발판이 없으면 "여기 서라" 가 화면에서 안 보인다.
    """
    pal.fill(cli, "pad", tile_x - w // 2, tile_y - h // 2, w, h)

def hyper_trigger(owner: str, waits: int = 63) -> str:
    """하이퍼(터보) 트리거 한 벌 — **유즈맵에 거의 필수다.**

    기본 트리거는 한 바퀴에 Fastest 기준 약 1.26초(30프레임)가 걸린다.
    그대로 두면 비콘을 밟아도 한 박자 늦고, 접촉 판정이 스쳐 지나가고,
    유닛이 뚝뚝 끊겨 죽는다.

    **반드시 `Wait` 를 달리 쓰지 않는 플레이어에게 건다.** 보통 시스템
    컴퓨터(P8)다. 한 플레이어는 동시에 웨이트 트리거를 하나만 쓸 수
    있어서, 사람 플레이어에게 걸면 그 사람의 다른 트리거가 전부
    먹통이 된다. `"All players"` 에 거는 것은 그래서 틀렸다.

    실행부 상한이 64개라 `Wait` 는 63개가 최대다. 보통 **세 벌**을
    깐다 — `hyper_triggers()` 를 쓴다.
    """
    body = "\n".join("\tWait(0);" for _ in range(waits))
    return (f'Trigger("{owner}"){{\n'
            f'Conditions:\n\tAlways();\n\n'
            f'Actions:\n{body}\n\tPreserve Trigger();\n}}')


def hyper_triggers(owner: str, copies: int = 3) -> list[str]:
    """하이퍼 트리거 세 벌. 한 벌로는 매 프레임에 못 미친다."""
    return [hyper_trigger(owner) for _ in range(copies)]


def kill_bounty(player: str, amount: int, per_score: int = 50,
                resource: str = "ore") -> str:
    """잡은 만큼만 돈을 주는 관용구 — **킬 스코어를 깎는다.**

    `Kill(..., At least, 1)` 은 "지금까지 몇 기 죽였나" 라 한 번 참이
    되면 계속 참이다. `Preserve` 와 함께 쓰면 매 주기 돈이 들어온다.

    죽은 수를 소비하는 방법은 누가 잡았는지 못 가린다. **킬 스코어**는
    플레이어별로 쌓이므로 그걸 깎으면 잡은 사람에게만 준다.

    기본 킬 점수는 미네랄x2 + 가스x4 다. **영웅은 그 두 배**다.

        저글링 50 · 마린 100 · 질럿 200 · 히드라 350 · 골리앗 400
        드라군 500 · 시즈탱크 700 · 울트라 1300 · 아콘 1400 · 배틀 2400
        짐 레이너(마린) 200 · 토라스크(울트라) 2600

    `per_score` 를 잡몹 한 마리 값으로 두면 한 마리에 `amount` 만큼
    준다. 센 유닛은 그만큼 여러 번 나눠 들어온다.
    """
    return (f'Trigger("{player}"){{\n'
            f'Conditions:\n\tScore("{player}", Kills, At least, {per_score});\n\n'
            f'Actions:\n'
            f'\tSet Score("{player}", Subtract, {per_score}, Kills);\n'
            f'\tSet Resources("{player}", Add, {amount}, {resource});\n'
            f'\tPreserve Trigger();\n}}')


TRIGGER_SEP = "\n\n//-----------------------------------------------------------------//\n\n"



# ------------------------------------------------- 맵에서 나눠 쓰는 것들
#
# **왜 있는가.** 죽음 수 칸·스위치 번호·카운트다운·순위표는 맵 하나에
# 하나씩뿐인 자원이다. 부품마다 제 마음대로 고르면 두 부품이 같은 칸을
# 써서 서로의 값을 망가뜨린다. 실제로 그렇게 될 자리가 여러 곳 있었다.
#
# 게다가 죽음 수 칸으로 아무 유닛이나 쓰면 안 된다. **게임이 저절로
# 만드는 유닛**을 칸으로 쓰면 그게 죽을 때마다 값이 틀어진다. 캐리어가
# 있는 맵에서 `Protoss Interceptor` 를 카운터로 쓰던 것이 그 예다 —
# 인터셉터는 캐리어가 계속 만들고 계속 죽는다.
#
# 아래 표는 "이 칸을 쓰면 안 되는 조건" 이다. 맵이 쓰는 유닛을 알려
# 주면 걸리는 칸을 건너뛴다.

COUNTER_SLOTS = [
    # (죽음 수 칸으로 쓸 유닛, 이것을 만들어 내는 것들)
    ("Dark Swarm",          ("Zerg Defiler", "Zerg Defiler Mound")),
    ("Disruption Web",      ("Protoss Corsair",)),
    ("Scanner Sweep",       ("Terran Comsat Station", "Terran Command Center")),
    ("Protoss Scarab",      ("Protoss Reaver", "Protoss Robotics Support Bay")),
    ("Protoss Interceptor", ("Protoss Carrier", "Gantrithor", "Protoss Stargate")),
    ("Spider Mine",         ("Terran Vulture", "Jim Raynor (Vulture)",
                             "Terran Machine Shop")),
    ("Nuclear Missile",     ("Terran Ghost", "Sarah Kerrigan",
                             "Terran Nuclear Silo")),
    ("Zerg Cocoon",         ("Zerg Mutalisk", "Kukulza (Mutalisk)",
                             "Zerg Greater Spire")),
    ("Zerg Lurker Egg",     ("Zerg Hydralisk", "Hunter Killer",
                             "Zerg Hydralisk Den")),
    ("Zerg Egg",            ("Zerg Larva", "Zerg Hatchery", "Zerg Lair",
                             "Zerg Hive")),
]


def units_in_play(cli: Cli, trigger_text: str = "") -> set[str]:
    """맵에 실제로 나타날 유닛 이름. 배치된 것 + 트리거가 만드는 것."""
    names = set()
    try:
        for u in cli.units():
            n = u.get("type_name")
            if n:
                names.add(n)
    except Exception:
        pass
    for m in re.finditer(r'Create Unit(?:\s*with\s*Properties)?\('
                         r'\s*"[^"]*"\s*,\s*"([^"]+)"', trigger_text):
        names.add(m.group(1))
    for m in re.finditer(r'Give Units to Player\([^,]*,[^,]*,\s*"([^"]+)"',
                         trigger_text):
        names.add(m.group(1))
    return names


class MapResources:
    """맵 하나에서 나눠 쓰는 것을 한 곳에서 준다.

        res = scmap.MapResources(in_play={"Terran Marine", "Zerg Sunken Colony"})
        life  = res.counter("목숨")          # 'Dark Swarm'
        wave  = res.counter("웨이브")        # 'Disruption Web'
        sw    = res.switch("문 열림")        # 1
        res.claim_countdown("Player 8")     # 맵에 하나뿐 — 두 번 부르면 raise
        res.claim_leaderboard("Custom")     # 순위표도 하나뿐

    같은 이름으로 다시 부르면 **같은 것**을 준다. 부품이 서로를 몰라도
    이름만 맞추면 한 칸을 함께 쓸 수 있다.
    """

    def __init__(self, in_play: "set[str] | None" = None):
        self.in_play = set(in_play or ())
        self._counters: dict[str, str] = {}
        self._switches: dict[str, int] = {}
        self._countdown: str | None = None
        self._leaderboard: str | None = None

    # -- 죽음 수 칸 ------------------------------------------------------
    def counter(self, purpose: str) -> str:
        if purpose in self._counters:
            return self._counters[purpose]
        taken = set(self._counters.values())
        for name, makers in COUNTER_SLOTS:
            if name in taken:
                continue
            if any(mk in self.in_play for mk in makers):
                continue          # 이 맵에서는 저절로 생긴다 — 값이 틀어진다
            if name in self.in_play:
                continue          # 맵에 직접 놓여 있다
            self._counters[purpose] = name
            return name
        raise CliError(
            f"죽음 수 칸이 모자랍니다 ('{purpose}'). 이미 {len(taken)}개를 "
            f"쓰고 있고, 남은 칸은 이 맵이 쓰는 유닛과 겹칩니다. 쓰는 유닛을 "
            f"줄이거나 스위치(Switch)로 바꾸세요.")

    def why_not(self, name: str) -> str | None:
        """그 칸을 왜 못 쓰는지 — 검사기가 사람에게 말해 줄 때 쓴다."""
        for slot, makers in COUNTER_SLOTS:
            if slot != name:
                continue
            bad = [mk for mk in makers if mk in self.in_play]
            if bad:
                return f"{bad[0]} 이 이 맵에 있어 {name} 이 저절로 생기고 죽는다"
        if name in self.in_play:
            return f"{name} 이 맵에 직접 놓여 있다"
        return None

    # -- 스위치 ----------------------------------------------------------
    def switch(self, purpose: str) -> int:
        """스위치는 **이름을 못 쓴다** — 1~255 번호뿐이다."""
        if purpose in self._switches:
            return self._switches[purpose]
        n = len(self._switches) + 1
        if n > 255:
            raise CliError("스위치는 255개까지입니다.")
        self._switches[purpose] = n
        return n

    # -- 하나뿐인 것 -----------------------------------------------------
    def claim_countdown(self, owner: str) -> None:
        """카운트다운 타이머는 **맵 전체에 하나**다.

        두 부품이 각자 `Set Countdown Timer` 를 걸면 서로 시간을 덮어
        쓴다. 먼저 잡은 쪽만 쓰게 한다.
        """
        if self._countdown is not None and self._countdown != owner:
            raise CliError(
                f"카운트다운 타이머는 맵에 하나뿐입니다. 이미 "
                f"'{self._countdown}' 가 쓰고 있어 '{owner}' 는 못 씁니다.")
        self._countdown = owner

    def claim_leaderboard(self, kind: str) -> None:
        """순위표도 하나다. 종류를 바꿔 걸면 앞의 것이 사라진다."""
        if self._leaderboard is not None and self._leaderboard != kind:
            raise CliError(
                f"순위표는 하나뿐입니다. 이미 '{self._leaderboard}' 로 걸려 "
                f"있어 '{kind}' 로 또 걸면 앞의 것이 사라집니다.")
        self._leaderboard = kind

    def describe(self) -> str:
        out = [f"죽음 수 칸 {len(self._counters)}개: " +
               ", ".join(f"{k}={v}" for k, v in self._counters.items())]
        if self._switches:
            out.append(f"스위치 {len(self._switches)}개: " +
                       ", ".join(f"{k}=Switch {v}"
                                 for k, v in self._switches.items()))
        if self._countdown:
            out.append(f"카운트다운: {self._countdown}")
        if self._leaderboard:
            out.append(f"순위표: {self._leaderboard}")
        return " · ".join(out)

# 있는지 표시하는 데 쓰는 유닛. 스펠이라 맵에 놓을 수 없고 실제로 죽는
# 일도 없어, 죽음 수가 순수한 변수가 된다.
PRESENCE_UNIT = "Disruption Web"


def absent_player_cleanup(humans: int, system_owner: str,
                          min_players: int = 1, grace: int = 3,
                          count_slot: str = "Disruption Web") -> list[str]:
    """**들어오지 않은 자리를 치우고, 인원이 모자라면 끝낸다.**

    유즈맵은 슬롯이 다 차지 않는 게 보통이다. 6인 맵에 넷이 들어오면
    빈 두 자리의 유닛이 필드에 남는다. 적이 그걸 때리러 가고, 전멸
    판정이 영영 참이 되지 않아 게임이 멈춘다.

    `Always()` 를 건 `"All players"` 트리거는 **실제로 들어온 사람에게만**
    돈다. 그걸로 표시를 찍고, 몇 초 뒤 표시가 없는 자리를 치운다.

    **인원은 세어야 한다.** 앞서 `min_players=3` 일 때 "P3 이 있는가" 만
    보았는데, P1·P2·P4 셋이 들어와도 지고 P3 혼자 들어오면 통과했다.
    들어온 사람마다 시스템 칸을 하나씩 올려 그 합을 본다.

    `count_slot` 은 **그 맵에 절대 나타나지 않는 유닛**이어야 한다.
    앞서 기본값이 `Protoss Interceptor` 였는데, 캐리어가 있는 맵에서는
    인터셉터가 끊임없이 생기고 죽어 인원수가 엉망이 된다. 어느 칸이
    안전한지는 맵이 쓰는 유닛에 따라 다르니 `MapResources.counter()`
    로 받아 쓰는 것이 옳다 — 기본값은 코르세어 없는 맵을 가정한다.
    """
    out = ['Trigger("All players"){\nConditions:\n\tAlways();\n'
           '\tDeaths("Current Player", "%s", Exactly, 0);\n\n'
           'Actions:\n'
           '\tSet Deaths("Current Player", "%s", Set To, 1);\n'
           '\tSet Deaths("%s", "%s", Add, 1);\n'
           '\tPreserve Trigger();\n}'
           % (PRESENCE_UNIT, PRESENCE_UNIT, system_owner, count_slot)]
    for p in range(1, humans + 1):
        out.append('Trigger("%s"){\nConditions:\n'
                   '\tElapsed Time(At least, %d);\n'
                   '\tDeaths("Player %d", "%s", Exactly, 0);\n\n'
                   'Actions:\n'
                   '\tRemove Unit("Player %d", "Any unit");\n'
                   '\tPreserve Trigger();\n}'
                   % (system_owner, grace, p, PRESENCE_UNIT, p))
    if min_players > 1:
        who = ",".join('"Player %d"' % p for p in range(1, humans + 1))
        out.append('Trigger(%s){\nConditions:\n'
                   '\tElapsed Time(At least, %d);\n'
                   '\tDeaths("%s", "%s", At most, %d);\n\n'
                   'Actions:\n'
                   '\tDisplay Text Message(Always Display, '
                   '"\\x06사람이 모자랍니다.\\x02 %d명 이상 필요합니다.");\n'
                   '\tDefeat();\n}'
                   % (who, grace + 2, system_owner, count_slot,
                      min_players - 1, min_players))
    return out



def briefing_text(lines: list[str], objectives: str | None = None,
                  portrait: str | None = None, hold_ms: int = 6000) -> str:
    """미션 브리핑(MBRF) 을 글로 짠다.

    유즈맵을 열면 게임 전에 뜨는 화면이다. **실측 772장 중 457장(59%)이
    쓴다.** 안 쓰면 플레이어가 아무 설명 없이 맵에 던져진다.

    브리핑에는 **조건이 없다.** 플레이어와 동작만 있고, 스위치 개념도
    없다. 적힌 차례대로 흐른다.

    실측에서 쓰는 동작 (457장 기준):
        Mission Objectives  83%   화면 왼쪽에 목표를 적는다
        Text Message        78%   가운데에 글을 띄운다 (글, 밀리초)
        Wait                65%   다음 줄까지 기다린다
        Show Portrait       55%   말하는 얼굴을 띄운다 (유닛, 슬롯)
        Play WAV            24%
    """
    out = []
    if objectives:
        out.append('Briefing("All players"){\n'
                   f'\tMission Objectives("{objectives}");\n}}')
    for i, line in enumerate(lines):
        body = ""
        if portrait and i == 0:
            body += f'\tShow Portrait("{portrait}", 0);\n'
        body += f'\tText Message("{line}", {hold_ms});\n'
        body += f'\tWait({hold_ms});\n'
        out.append('Briefing("All players"){\n' + body + '}')
    return TRIGGER_SEP.join(out)


# ===================================================================
# 부품 — 유즈맵 트리거 조각
#
# 장르마다 생성기를 따로 쓰면 결과물이 늘 똑같고, 새 장르를 만들 때마다
# 품질 바닥(하이퍼·종족·비콘 밀어내기·빈 슬롯)을 다시 챙겨야 한다.
# 그래서 **바닥은 여기 부품에 넣고, 조립은 그때그때 한다.**
#
# 부품은 트리거 글 조각(list[str])을 돌려준다. 이어 붙여서
# `TRIGGER_SEP.join(...)` 으로 만들고 `cli.apply_triggers()` 에 넘긴다.
# ===================================================================

def part_intro(humans: list[str], lines: list[str], ore: int = 0,
               objectives: str | None = None, timer: int | None = None,
               counters: dict[str, int] | None = None) -> list[str]:
    """맨 처음 한 번 — 자원·카운터를 놓고 안내를 띄운다.

    **안내는 사람이 실행하는 트리거에 둔다.** `Display Text Message` 는
    그 트리거를 실행하는 플레이어에게만 보인다.
    """
    who = ",".join(f'"{h}"' for h in humans)
    acts = []
    if ore:
        acts.append(f'\tSet Resources("Current Player", Set To, {ore}, ore);')
    for unit, val in (counters or {}).items():
        acts.append(f'\tSet Deaths("Current Player", "{unit}", Set To, {val});')
    for l in lines:
        acts.append(f'\tDisplay Text Message(Always Display, "{l}");')
    if objectives:
        acts.append(f'\tSet Mission Objectives("{objectives}");')
    if timer:
        acts.append(f'\tSet Countdown Timer(Set To, {timer});')
    return [f'Trigger({who}){{\nConditions:\n\tAlways();\n\n'
            f'Actions:\n' + "\n".join(acts) + '\n}']


def part_leaderboard(label: str, kind: str = "Custom") -> list[str]:
    """순위표. 실측에서 디펜스 92%, 땅따먹기 100% 가 쓴다.

    kind: Custom · Kills · Kills and razings · Control · Resources
    """
    if kind == "Kills":
        return [f'Trigger("All players"){{\nConditions:\n\tAlways();\n\n'
                f'Actions:\n\tLeader Board Kills("{label}", "Any unit");\n'
                f'\tPreserve Trigger();\n}}']
    return [f'Trigger("All players"){{\nConditions:\n\tAlways();\n\n'
            f'Actions:\n\tLeader Board Points("{label}", {kind});\n'
            f'\tPreserve Trigger();\n}}']


def part_beacon_shop(player: str, beacon_loc: str, cost: int, effect: list[str],
                     label: str, push_to: str, resource: str = "ore") -> list[str]:
    """비콘 상점 한 자리.

    **산 뒤에 비콘 밖으로 밀어낸다.** 안 밀어내면 하이퍼 트리거와 맞물려
    서 있는 동안 매 프레임 사들여 돈이 순식간에 증발한다.
    """
    acts = [f'\tSet Resources("{player}", Subtract, {cost}, {resource});']
    acts += effect
    acts.append(f'\tMove Unit("{player}", "Men", All, "{beacon_loc}", "{push_to}");')
    acts.append(f'\tDisplay Text Message(Always Display, "{label} \\x02-{cost}");')
    acts.append('\tPlay WAV("sound\\\\Misc\\\\Button.wav", 0);')
    acts.append('\tPreserve Trigger();')
    return [f'Trigger("{player}"){{\nConditions:\n'
            f'\tBring("{player}", "Men", "{beacon_loc}", At least, 1);\n'
            f'\tAccumulate("{player}", At least, {cost}, {resource});\n\n'
            f'Actions:\n' + "\n".join(acts) + '\n}']


def part_patrol_path(owner: str, stops: list[str], mode: str) -> list[str]:
    """스폰한 무리를 길을 따라 보낸다. **mode 를 반드시 골라 준다.**

    실측 최빈값은 patrol(4341회, move 1159, attack 918) 이지만 그것을
    기본값으로 두지 않는다. 최빈값을 기본으로 삼는 것은 "수치에 맞추기"
    이고, 그렇게 만든 맵이 여러 번 망했다. 놀이가 정한다:

        attack  — 길을 따라가며 마주치는 것을 친다 (디펜스 웨이브)
        patrol  — 두 자리를 오간다 (순찰하는 몬스터)
        move    — 도착하면 **멈춰 선다** (자리를 잡아야 하는 경우)
    """
    if mode not in ("attack", "patrol", "move"):
        raise CliError(f"mode 는 attack·patrol·move 중 하나입니다: {mode}")
    acts = [f'\tOrder("{owner}", "Any unit", "{a}", "{b}", {mode});'
            for a, b in zip(stops, stops[1:])]
    acts.append('\tPreserve Trigger();')
    return [f'Trigger("{owner}"){{\nConditions:\n\tAlways();\n\n'
            f'Actions:\n' + "\n".join(acts) + '\n}']


def part_lives(player: str, counter: str, start: int, lose_when: str,
               where: str, leaker: str,
               msg: str = "\\x06새어 나갔습니다!") -> list[str]:
    """목숨 — 조건이 참이면 하나 깎고, 0 이 되면 진다.

    `leaker` 는 **새어 나간 유닛의 주인**이다. 앞서 이 자리에 로케이션
    이름을 넣어 `Remove Unit At Location("Exit", ...)` 같은 트리거가
    나갔다. 컴파일은 통과하고 게임에서는 아무 일도 안 일어난다.

    **한 기씩 지운다.** 통째로 지우면 다섯이 새어도 목숨이 하나만 준다.

    `start` 로 초기값도 여기서 찍는다 — 호출자가 따로 기억하지 않게.
    """
    return [
        'Trigger("%s"){\nConditions:\n\tAlways();\n'
        '\tDeaths("%s", "%s", Exactly, 0);\n\n'
        'Actions:\n'
        '\tSet Deaths("%s", "%s", Set To, %d);\n'
        '\tSet Score("%s", Set To, %d, Custom);\n}'
        % (player, player, counter, player, counter, start, player, start),

        'Trigger("%s"){\nConditions:\n\t%s\n\n'
        'Actions:\n'
        '\tRemove Unit At Location("%s", "Any unit", 1, "%s");\n'
        '\tSet Deaths("%s", "%s", Subtract, 1);\n'
        '\tSet Score("%s", Subtract, 1, Custom);\n'
        '\tDisplay Text Message(Always Display, "%s");\n'
        '\tMinimap Ping("%s");\n'
        '\tPreserve Trigger();\n}'
        % (player, lose_when, leaker, where, player, counter, player, msg, where),

        'Trigger("%s"){\nConditions:\n'
        '\tDeaths("%s", "%s", Exactly, 0);\n\n'
        'Actions:\n'
        '\tDisplay Text Message(Always Display, "\\x06목숨이 다했습니다.");\n'
        '\tDefeat();\n}'
        % (player, player, counter)]


def part_respawn(player: str, unit: str, where: str, count: int = 4,
                 cooldown_counter: str = "Protoss Interceptor",
                 guard: str | None = None) -> list[str]:
    """병력이 다 죽으면 다시 준다 — 구경만 하다 지지 않게.

    **잠금이 반드시 있어야 한다.** 조건(`Men At most 0`)은 새 유닛이
    실제로 잡히기 전까지 계속 참이라, 하이퍼 트리거와 맞물리면 한 번에
    수십 기가 쏟아진다. 죽음 수를 잠금으로 써서 한 번만 주고, 병력이
    생기면 잠금을 푼다.

    `Command(..., "Men", At most, 0)` 을 쓴다: 생산 중인 유닛까지 세므로
    헛발동이 없다.
    """
    cond = [f'\tCommand("{player}", "Men", At most, 0);',
            f'\tDeaths("{player}", "{cooldown_counter}", Exactly, 0);']
    if guard:
        cond.append(f'\t{guard}')
    return [
        f'Trigger("{player}"){{\nConditions:\n' + "\n".join(cond) + '\n\n'
        f'Actions:\n'
        f'\tSet Deaths("{player}", "{cooldown_counter}", Set To, 1);\n'
        f'\tCreate Unit("{player}", "{unit}", {count}, "{where}");\n'
        f'\tDisplay Text Message(Always Display, "\\x03병력을 다시 받았습니다.");\n'
        f'\tCenter View("{where}");\n'
        f'\tPreserve Trigger();\n}}',
        # 병력이 생기면 잠금을 푼다
        f'Trigger("{player}"){{\nConditions:\n'
        f'\tCommand("{player}", "Men", At least, 1);\n'
        f'\tDeaths("{player}", "{cooldown_counter}", At least, 1);\n\n'
        f'Actions:\n'
        f'\tSet Deaths("{player}", "{cooldown_counter}", Set To, 0);\n'
        f'\tPreserve Trigger();\n}}']


def part_heal_zone(player: str, where: str, cost: int = 0,
                   push_to: str | None = None) -> list[str]:
    # 값을 받으면 밀어내기가 **필수**다. 안 밀어내면 비콘 위에 서 있는
    # 동안 매 프레임 결제된다 — part_beacon_shop 이 막는 바로 그 버그다.
    """회복 구역. 값을 0 으로 두면 공짜(마을), 주면 돈을 받는다.

    **퍼센트가 먼저다.** `(플레이어, 유닛, 퍼센트, 개수, 로케이션)` 이고
    개수 0 이 "전부" 다. 차례를 바꿔 쓰면 회복이 아니라 깎는 동작이 된다.
    """
    if cost and not push_to:
        raise CliError("값을 받는 회복 구역에는 push_to 가 있어야 합니다. "
                       "안 밀어내면 서 있는 동안 매 프레임 결제됩니다.")
    cond = [f'\tBring("{player}", "Men", "{where}", At least, 1);']
    acts = []
    if cost:
        cond.append(f'\tAccumulate("{player}", At least, {cost}, ore);')
        acts.append(f'\tSet Resources("{player}", Subtract, {cost}, ore);')
    acts += [f'\tModify Unit Hit Points("{player}", "Men", 100, 0, "{where}");',
             f'\tModify Unit Energy("{player}", "Men", 100, 0, "{where}");',
             f'\tModify Unit Shield Points("{player}", "Men", 100, 0, "{where}");']
    if push_to:
        acts.append(f'\tMove Unit("{player}", "Men", All, "{where}", "{push_to}");')
    acts.append('\tPreserve Trigger();')
    return [f'Trigger("{player}"){{\nConditions:\n' + "\n".join(cond) + '\n\n'
            f'Actions:\n' + "\n".join(acts) + '\n}']


def part_wave_clock(owner: str, counter: str, waves: int,
                    seconds: int = 35) -> list[str]:
    """웨이브 번호를 올리는 시계. 컴퓨터가 돌린다."""
    return [f'Trigger("{owner}"){{\nConditions:\n'
            f'\tCountdown Timer(At most, 0);\n'
            f'\tDeaths("{owner}", "{counter}", At most, {waves});\n\n'
            f'Actions:\n'
            f'\tSet Deaths("{owner}", "{counter}", Add, 1);\n'
            f'\tSet Countdown Timer(Set To, {seconds});\n'
            f'\tPreserve Trigger();\n}}']


def part_announce_once(humans: list[str], when: str, seen_counter: str,
                       step: int, lines: list[str],
                       wav: str | None = None) -> list[str]:
    """사람마다 **한 번만** 띄우는 안내.

    조건이 한동안 계속 참인 트리거에 `Preserve` 를 붙이면 매 프레임
    도배된다. 잠금은 스위치가 아니라 **플레이어별 죽음 수**로 건다 —
    스위치는 맵 전체에 하나뿐이라 첫 사람만 걸린다.
    """
    who = ",".join(f'"{h}"' for h in humans)
    acts = [f'\tSet Deaths("Current Player", "{seen_counter}", Set To, {step});']
    acts += [f'\tDisplay Text Message(Always Display, "{l}");' for l in lines]
    if wav:
        acts.append(f'\tPlay WAV("{wav}", 0);')
    acts.append('\tPreserve Trigger();')
    return [f'Trigger({who}){{\nConditions:\n\t{when}\n'
            f'\tDeaths("Current Player", "{seen_counter}", At most, {step - 1});\n\n'
            f'Actions:\n' + "\n".join(acts) + '\n}']


def part_win(players: list[str], conds: list[str],
             msg: str = "\\x07이겼습니다!") -> list[str]:
    """승리. **`Preserve Trigger` 를 붙이지 않는다** — 붙이면 매 틱 재발동한다."""
    who = ",".join(f'"{p}"' for p in players)
    return [f'Trigger({who}){{\nConditions:\n' +
            "\n".join(f'\t{c}' for c in conds) + '\n\n'
            f'Actions:\n'
            f'\tDisplay Text Message(Always Display, "{msg}");\n'
            f'\tVictory();\n}}']


def usemap_floor(humans: int, system_owner: str,
                 min_players: int = 1) -> list[str]:
    """**품질 바닥.** 어떤 유즈맵이든 이것부터 깔고 시작한다.

    하이퍼 트리거 세 벌 + 들어오지 않은 자리 정리.
    """
    return hyper_triggers(system_owner) + \
        absent_player_cleanup(humans, system_owner, min_players)


def part_infection(humans: list[str], zombie: str, mark: str,
                   zombie_unit: str, spawn_at: str) -> list[str]:
    """감염 — 병력을 다 잃은 사람이 좀비 편으로 넘어간다.

    실측 좀비 맵 35장 중 91% 가 `Set Alliance Status` 를 쓴다. 관용구는
    **죽음 수로 표시를 찍고, 그 표시를 보고 편을 바꾸는 것**이다.
    한 트리거에서 다 하려 들면 편이 바뀐 뒤에도 조건이 참이라 되풀이된다.
    """
    out = []
    who = ",".join(f'"{h}"' for h in humans)
    # 1) 병력을 다 잃으면 표시를 찍는다 (사람이 실행)
    out.append(
        f'Trigger({who}){{\nConditions:\n'
        f'\tCommand("Current Player", "Men", At most, 0);\n'
        f'\tDeaths("Current Player", "{mark}", Exactly, 0);\n\n'
        f'Actions:\n'
        f'\tSet Deaths("Current Player", "{mark}", Set To, 1);\n'
        f'\tDisplay Text Message(Always Display, '
        f'"\\x06감염되었습니다.\\x02 이제 좀비입니다.");\n'
        f'\tPlay WAV("sound\\\\Zerg\\\\Advisor\\\\ZAdUpd00.wav", 0);\n'
        f'\tPreserve Trigger();\n}}')
    # 2) 표시가 찍힌 사람을 좀비 편으로 (좀비가 실행)
    for h in humans:
        out.append(
            f'Trigger("{zombie}"){{\nConditions:\n'
            f'\tDeaths("{h}", "{mark}", At least, 1);\n\n'
            f'Actions:\n'
            f'\tSet Alliance Status("{h}", Ally);\n'
            f'\tPreserve Trigger();\n}}')
    # 3) 감염자와 생존자를 **서로** 적으로 돌린다. 한쪽만 바꾸면 안 된다 —
    #    setup_usemap_players 가 사람 전원을 동맹으로 묶어 두므로,
    #    감염자가 좀비와도 생존자와도 동맹인 상태로 남는다.
    for h in humans:
        others = "".join('\tSet Alliance Status("%s", Enemy);\n' % o
                         for o in humans if o != h)
        out.append('Trigger("%s"){\nConditions:\n'
                   '\tDeaths("%s", "%s", At least, 1);\n\n'
                   'Actions:\n%s'
                   '\tSet Alliance Status("%s", Ally);\n'
                   '\tPreserve Trigger();\n}'
                   % (h, h, mark, others, zombie))
        out.append('Trigger(%s){\nConditions:\n'
                   '\tDeaths("%s", "%s", At least, 1);\n'
                   '\tDeaths("Current Player", "%s", Exactly, 0);\n\n'
                   'Actions:\n'
                   '\tSet Alliance Status("%s", Enemy);\n'
                   '\tPreserve Trigger();\n}'
                   % (",".join('"%s"' % o for o in humans if o != h),
                      h, mark, mark, h))

    # 4) 좀비가 된 사람에게 좀비 유닛을 준다
    out.append(
        f'Trigger({who}){{\nConditions:\n'
        f'\tDeaths("Current Player", "{mark}", At least, 1);\n'
        f'\tCommand("Current Player", "Men", At most, 0);\n\n'
        f'Actions:\n'
        f'\tSet Alliance Status("{zombie}", Ally);\n'
        f'\tCreate Unit("Current Player", "{zombie_unit}", 2, "{spawn_at}");\n'
        f'\tCenter View("{spawn_at}");\n'
        f'\tPreserve Trigger();\n}}')
    return out


def part_survive_timer(humans: list[str], seconds: int,
                       msg: str = "\\x07끝까지 버텼습니다!") -> list[str]:
    """정해진 시간을 버티면 이긴다. 블러드·좀비·술래잡기의 끝맺음."""
    who = ",".join(f'"{h}"' for h in humans)
    return [f'Trigger({who}){{\nConditions:\n'
            f'\tElapsed Time(At least, {seconds});\n'
            f'\tCommand("Current Player", "Men", At least, 1);\n\n'
            f'Actions:\n'
            f'\tDisplay Text Message(Always Display, "{msg}");\n'
            f'\tVictory();\n}}']
