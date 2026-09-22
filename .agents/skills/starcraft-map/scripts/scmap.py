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

    def doodads(self) -> list[dict]:
        """이 맵에 **놓여 있는** 두뎃. 목록 차례가 곧 `doodad remove` 번호다."""
        out = self.run("doodad", "list", self.path, "--install", self.install)
        items = []
        for line in out.splitlines():
            m = re.match(r"^\s*(\d+)\s+\((\d+), *(\d+)\)\s+타일 "
                         r"\((\d+), *(\d+)\)\s+두들 (\d+)", line)
            if m:
                items.append({"index": int(m.group(1)),
                              "x": int(m.group(4)), "y": int(m.group(5)),
                              "id": int(m.group(6))})
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
        """ISOM 붓질을 한꺼번에 놓는다.

        `strokes` 는 `(타일x, 타일y, 지형)` 또는 `(타일x, 타일y, 지형,
        브러시)` 목록이다. 면을 채울 때는 **브러시 1** 을 명시하는 편이
        낫다 — 큰 브러시는 경계를 예측하기 어렵게 넓힌다.

        붓질마다 명령을 부르면 맵을 열고 저장하는 값이 붓질 값보다 훨씬
        크다 — 천 번 칠하는 데 몇 분이 걸린다. 한 번에 보낸다.
        """
        strokes = list(strokes)
        if not strokes:
            return 0
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                         encoding="utf-8") as f:
            for st in strokes:
                tx, ty, terrain = st[0], st[1], st[2]
                brush = st[3] if len(st) > 3 else None
                line = f"{tx * TILE} {ty * TILE} {terrain}"
                if brush is not None:
                    line += f" {brush}"
                f.write(line + "\n")
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
# **램프는 생타일이 아니라 두뎃이다.** 이걸 몰라서 세 번 틀렸다.
#
#   1. 손으로 적은 표에 "기준 타일값 0x4a70" 처럼 적어 두었는데, 그
#      값은 사실 **두뎃 번호**였다.
#   2. 한 세트에서 **행 오프셋만 바꿔** 네 방향에 붙였다. 방향마다
#      아예 다른 두뎃을 써야 한다.
#   3. "Space·Desert·Ice·Twilight 는 램프가 없다" 고 적었다. 실은
#      그 넷이 가장 많다 (Desert 166개, Space 136개).
#
# StarEdit 은 램프 세트가 절벽에 붙는지를 이미 알고 있다. 그러니 타일을
# 손으로 펼치지 말고 **두뎃을 그대로 놓는다.**
#
# 표는 `data/ramps.json` — `measure_ramps.py` 가 여덟 타일셋에 실제로
# 놓아 보고, 램프 깃발이 선 칸이 고지대 덩이의 어느 쪽에 붙는지로
# 방향을 가려 만든다. 612개를 찾았다.


def _ramp_table() -> dict:
    """`data/ramps.json` — 타일셋마다 램프 두뎃과 그 방향."""
    global _RAMP_TABLE
    try:
        return _RAMP_TABLE
    except NameError:
        pass
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "data", "ramps.json")
    try:
        with open(os.path.normpath(path), encoding="utf-8") as f:
            _RAMP_TABLE = json.load(f).get("ramps", {})
    except OSError:
        _RAMP_TABLE = {}
    return _RAMP_TABLE


TILESET_NAMES = ("badlands", "space", "installation", "ashworld",
                 "jungle", "desert", "ice", "twilight")


def ramp_candidates(tileset_id: int, direction="down") -> list[dict]:
    """그 타일셋·방향에서 쓸 **램프 두뎃** 목록. 실측 표에서 읽는다.

    direction 은 "down"/"up"/"left"/"right" — **내려가는 쪽**이다. 옛
    코드를 위해 참/거짓도 받는다 (참이면 down).

    돌려주는 것은 `{"id", "w", "h", "kind", "dir", "ramp_tiles"}` 목록.
    램프 칸이 많은 것부터 준다 — 넓은 램프가 막힐 일이 적다.
    """
    if isinstance(direction, bool):
        direction = "down" if direction else "up"
    name = TILESET_NAMES[tileset_id & 7]
    rows = [r for r in (_ramp_table().get(name) or [])
            if r.get("dir") == direction]
    rows.sort(key=lambda r: -r.get("ramp_tiles", 0))
    return rows


def place_ramp(cli: Cli, tile_x: int, tile_y: int, doodad_id: int,
               width: int, height: int):
    """램프를 놓는다. **(tile_x, tile_y) 는 램프의 가운데**다.

    `doodad place` 가 가운데 기준이므로 `place_doodad` 를 거친다.
    """
    place_doodad(cli, doodad_id, tile_x, tile_y, width, height)


def default_ramp(tileset_id: int):
    """옛 이름. (두뎃번호, 가로, 세로) 또는 None."""
    entries = ramp_candidates(tileset_id, "down")
    if not entries:
        return None
    e = entries[0]
    return e["id"], e["w"], e["h"]



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
               start_location: bool = True,
               tileset_id: int | None = None, check_terrain: bool = True):
    """스타팅 한 곳을 통째로 놓는다 — 스타팅 표시 + 미네랄 + 가스.

    facing 은 90도 단위 회전 수다. 대칭으로 놓은 스타팅마다 같은 모양을
    돌려 쓰기 위한 것이다.

    **지형을 보고 놓는다.** 앞서 자리만 계산해 그냥 박았다 — 자원이
    절벽·물 위에 얹혀도 모르고 지나갔다. 자원은 **건물을 지을 수 있는
    평지**에 있어야 일꾼이 붙는다. `tileset_id` 를 주면 놓기 전에
    확인하고, 안 되는 자리는 건너뛰며 무엇을 건너뛰었는지 돌려준다.

    돌려주는 것은 `(놓은 것 목록, 건너뛴 것 목록)` 이다.
    """
    tiles = tileset_tiles(cli, tileset_id) if (check_terrain and
                                               tileset_id is not None) else None
    grid = None
    if tiles is not None:
        grid = cli.tiles(0, 0, width, height)

    def buildable(px_x: int, px_y: int, w: int, h: int) -> bool:
        """자원이 놓일 칸이 다 **짓기 가능**한가.

        미네랄은 1x1, 베스핀은 4x2 를 차지한다. 그 칸이 물·절벽이면
        일꾼이 못 붙는다.
        """
        if grid is None:
            return True
        tx, ty = px_x // TILE, px_y // TILE
        for yy in range(ty - h // 2, ty + (h + 1) // 2):
            for xx in range(tx - w // 2, tx + (w + 1) // 2):
                if not (0 <= yy < len(grid) and 0 <= xx < len(grid[0])):
                    return False
                p = tiles.get(grid[yy][xx])
                if p is None or not p[1] or not p[2]:   # 걷기·짓기
                    return False
        return True
    if start_location:
        cli.place(START_LOCATION, tile_x, tile_y, owner)

    def turn(dx, dy):
        """자원을 본진 **바깥쪽**으로 보낸다.

        돌리지 않고 뒤집기만 한다. 90도로 돌리면 자원이 램프 쪽으로
        가서 입구를 막는다 — 실제로 베스핀이 램프 위에 얹힌 적이 있다.
        """
        return dx * (1 if out_x < 0 else -1), dy * (1 if out_y < 0 else -1)

    placed, skipped = [], []
    kinds = (MINERAL_1, MINERAL_2, MINERAL_3)
    for i in range(minerals):
        dx, dy = MAIN_MINERAL_OFFSETS[i % len(MAIN_MINERAL_OFFSETS)]
        dx, dy = turn(dx, dy)
        px, py = tile_x * TILE + dx, tile_y * TILE + dy
        if not buildable(px, py, 2, 1):
            skipped.append(("mineral", px // TILE, py // TILE))
            continue
        cli.edit("unit", "place", cli.path, str(kinds[i % 3]),
                 str(px), str(py), "--owner", "12")
        placed.append(("mineral", dx, dy))
    for i in range(gas):
        dx, dy = turn(*MAIN_GAS_OFFSET)
        px, py = tile_x * TILE + dx, tile_y * TILE + dy + i * 96
        if not buildable(px, py, 4, 2):
            skipped.append(("gas", px // TILE, py // TILE))
            continue
        cli.edit("unit", "place", cli.path, str(VESPENE_GEYSER),
                 str(px), str(py), "--owner", "12")
        placed.append(("gas", dx, dy))

    return placed, skipped


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
    # **세력 깃발(Allied·Shared Vision·Enable Allied Victory)은 켜도
    # 적용되지 않는다.** 스타 에디터 아카데미 [기초4] 유즈맵의 필수 요소:
    #
    #   "그 외 Allied, Shared Vision, Enable Allied Victory는 체크하든
    #    말든 적용 안되니 무시해도 됩니다. … 같은 세력에 넣었다고 자동으로
    #    서로 동맹이 되고 다른 세력이라고 자동으로 적이 되고 이런건 없습니다."
    #
    # 기본 상태는 이렇다: 컴퓨터끼리는 동맹, 사람↔컴퓨터는 적, **사람끼리도
    # 적**이다. 협동으로 만들려면 트리거에서 `Set Alliance Status` 로
    # 직접 묶어야 한다 (`part_ally_humans`).
    #
    # ⚠ 다만 **이것을 직접 확인하지는 못했다.** 카페 한 편이 출처이고
    # 게임을 돌려 보지 못했다. 실측 맵은 오히려 그 깃발을 대부분 켜 둔다
    # (docs/chk/anatomy.md). 그래서 깃발은 켜 두되 **믿지 않고 트리거로
    # 한 번 더 묶는다** — 깃발이 먹으면 중복일 뿐이고 안 먹으면 트리거가
    # 살린다.
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
    """램프 두뎃을 놓고 **실제로 걸어서 통하는지** 확인한다. 안 되면 뺀다.

    direction 은 **내려가는 쪽**이다. down/up 이면 `edge` 는 고지대가
    끝나는 **줄**이고 `fixed` 는 가로 자리, left/right 면 `edge` 가
    **칸**이고 `fixed` 가 세로 자리다.

    "눈으로 보니 이어졌다" 는 검증이 아니다. 게임은 **미니타일** 단위로
    길을 찾으므로 그 격자에서 high_point → low_point 를 따라가 본다.
    멀쩡해 보이는 램프가 실제로는 막혀 있던 적이 있다.
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

    seen = set()
    for shift in shifts:
        for e in candidates:
            did, w, h = e["id"], e["w"], e["h"]
            pos = edge + shift
            x, y = (pos, fixed) if horizontal else (fixed, pos)
            if x < 0 or y < 0 or x + w > mw or y + h > mh:
                continue
            if (x, y, did) in seen:
                continue
            seen.add((x, y, did))

            before = cli.tiles(x, y, w, h)
            n_before = len(cli.doodads())
            try:
                # 두뎃은 가운데 기준이다. 왼위를 가운데로 옮겨 넘긴다.
                cx, cy = doodad_anchor(x, y, w, h)
                place_doodad(cli, did, cx, cy, w, h)
            except CliError:
                continue

            grid = walk_grid(cli, tileset_id, rx0, ry0, rw, rh)
            a_ = nearest_walkable(grid, (hx - rx0) * 4 + 2, (hy - ry0) * 4 + 2)
            b_ = nearest_walkable(grid, (lx - rx0) * 4 + 2, (ly - ry0) * 4 + 2)
            if a_ and b_ and walk_reachable(grid, a_, b_):
                return did, x, y

            # 안 통하면 두뎃 항목과 타일을 함께 되돌린다. 두뎃만 빼면
            # 지형이 남고, 타일만 되돌리면 DD2 에 유령이 남는다.
            n_after = len(cli.doodads())
            if n_after > n_before:
                cli.edit("doodad", "remove", cli.path, str(n_after - 1),
                         "--install", cli.install)
            cli.paste_tiles(x, y, before)

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

# ---------------------------------------------------------------------------
# 지형을 놓는 두 가지 길 — **ISOM 이냐 사각형이냐는 장르가 정한다**
#
# 내가 "밀리=ISOM, 유즈맵=사각형" 으로 못 박아 두고 있었다. 틀렸다.
#
#   밀리맵은 ISOM 만 쓰는 것이 맞다. 절벽이 이어져야 하고 길찾기가
#   지형을 그대로 읽기 때문이다.
#
#   유즈맵은 **컨셉과 장르가 정한다.**
#     - OX 퀴즈처럼 지형이 주인공이 아닌 것 → 사각형. O 칸과 X 칸이
#       또렷하게 갈려야 하지 자연스러울 까닭이 없다.
#     - RPG 처럼 돌아다니는 것이 재미인 것 → ISOM. 사각형이면 심심하다.
#       진짜 같은 지형이 있어야 걸어다닐 맛이 난다.
#
# 장르별 실측은 data/isom-usage.json, 설명은 docs/usemap/terrain.md.
# ---------------------------------------------------------------------------

def terrain_mode(genre: str, default: str = "rect") -> str:
    """이 장르는 ISOM 으로 짓는가 사각형으로 짓는가. 실측에서 읽는다.

    기준은 **ISOM 다양도 중앙값**이다. "한 번이라도 붓질했나" 로 재면
    퀴즈까지 ISOM 으로 잡힌다 — 실제로는 거의 균일한데 귀퉁이 몇 번
    건드린 맵이 섞여서 그렇다. 중앙값이 0.2 를 넘어야 "이 장르는 지형을
    ISOM 으로 짓는다" 고 할 수 있다.

    유즈맵 479장 실측 (트리거 지문으로 장르를 붙임):

        rpg     0.445   blood  0.616   ← ISOM
        zombie  0.000   defense 0.000  control 0.000  escape 0.000
        quiz    0.073   tag    0.046   land 0.000     ← 사각형
    """
    d = _isom_usage().get(genre)
    if not d:
        return default
    return "isom" if d.get("isom_variety_median", 0) >= 0.2 else "rect"


def _isom_usage() -> dict:
    global _ISOM_USAGE
    try:
        return _ISOM_USAGE
    except NameError:
        pass
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "data", "isom-usage.json")
    try:
        with open(os.path.normpath(path), encoding="utf-8") as f:
            _ISOM_USAGE = json.load(f).get("by_genre", {})
    except OSError:
        _ISOM_USAGE = {}
    return _ISOM_USAGE


def isom_fill(cli: Cli, terrain: int, x: int, y: int, w: int, h: int,
              brush: int | None = None):
    """네모를 **ISOM 붓질로** 채운다 — 가장자리가 저절로 이어진다.

    `terrain fill` 과 달리 타일 값을 직접 쓰지 않는다. 브러시가 한 번에
    한 타일 남짓을 덮으므로 가로는 두 칸, 세로는 한 칸 간격으로 찍는다.
    그래야 마름모 격자에 맞아 어긋나지 않는다.
    """
    strokes = []
    for ty in range(y, y + h):
        for tx in range(x - (x % 2), x + w, 2):
            if x <= tx < x + w:
                strokes.append((tx, ty, terrain)
                               if brush is None else (tx, ty, terrain, brush))
    if strokes:
        cli.isom_batch(strokes)


def isom_blob(cli: Cli, terrain: int, rng, center_x: int, center_y: int,
              area: int, elongate: float = 1.0, bounds=None,
              brush: int | None = None) -> set:
    """덩이 하나를 ISOM 으로 놓는다. 실제로 찍은 타일 자리를 돌려준다.

    `organic_blob` 이 만든 모양을 그대로 붓질로 옮긴다. 네모가 아니므로
    RPG·좀비처럼 "돌아다니는" 맵의 지형이 된다.
    """
    cells = organic_blob(rng, area, elongate)
    strokes, done = [], set()
    for dx, dy in sorted(cells):
        tx, ty = center_x + dx, center_y + dy
        if bounds is not None:
            bx, by, bw, bh = bounds
            if not (bx <= tx < bx + bw and by <= ty < by + bh):
                continue
        done.add((tx, ty))
        strokes.append((tx, ty, terrain)
                       if brush is None else (tx, ty, terrain, brush))
    if strokes:
        cli.isom_batch(strokes)
    return done


def isom_landscape(cli: Cli, tileset_id: int, rng, width: int, height: int,
                   keep_clear=(), walls: int = 12, rises: int = 18,
                   patches: int = 22, area: int = 70, margin: int = 3) -> dict:
    """**돌아다니는 맵의 지형**을 ISOM 으로 짓는다.

    사각형 방식(`cover_map` 으로 다 덮고 `room` 으로 뚫기)과 뒤집힌
    순서다. 여기서는 **바닥이 먼저**고, 그 위에 지형 덩이를 키운다.
    그래야 길이 네모나지 않고 실제 지형처럼 보인다.

    실측 RPG 유즈맵의 타일 그룹 중앙값이 **248개**다 (디펜스는 15개).
    지형 자체가 콘텐츠인 장르라 이렇게 짓는다 — docs/usemap/terrain.md.

    덩이를 **세 갈래로** 놓는다. 한 갈래만 쓰면 노는 자리가 텅 비고,
    지형을 세 종류만 쓰면 바닥이 한 가지 색으로 남는다 — 둘 다 겪었다.

    | 갈래 | 지형 | 걷는가 | 어디에 |
    | --- | --- | --- | --- |
    | `patches` | 낮고 걷는 지형 여럿 (흙·진흙·수풀·돌) | 걷는다 | 아무 데나 |
    | `rises` | 높고 걷는 지형 여럿 | 걷는다 | **노는 자리 안까지** |
    | `walls` | 물·용암 | **못 걷는다** | `keep_clear` 를 피해서만 |

    걸을 수 있는 지형은 길을 막지 않으므로 마을·구역 위에 얹어도 된다.
    그래야 노는 자리에도 지형이 생긴다.

    `keep_clear` 는 못 걷는 지형이 들어가면 안 되는 네모들
    `(x, y, w, h)`.
    """
    ids = isom_terrain_ids(cli, tileset_id)
    if not ids["low"]:
        raise CliError("이 타일셋에서 낮고 걷는 지형을 못 찾았습니다")
    base = ids["low"][0]

    # 1) 바닥부터. 덩이는 이 위에 얹는다.
    isom_fill(cli, base, 0, 0, width, height)

    def clear_of(tx, ty, pad=2):
        for (kx, ky, kw, kh) in keep_clear:
            if (kx - pad <= tx < kx + kw + pad
                    and ky - pad <= ty < ky + kh + pad):
                return False
        return True

    def grow(choices, count, avoid_clear: bool, blob_area: int):
        placed, tries = [], 0
        if not choices:
            return placed
        while len(placed) < count and tries < count * 40:
            tries += 1
            cx = rng.randrange(margin, max(margin + 1, width - margin))
            cy = rng.randrange(margin, max(margin + 1, height - margin))
            cx -= cx % 2                # ISOM 은 가로 두 칸이 한 걸음
            shape = organic_blob(rng, rng.randrange(blob_area // 2,
                                                    blob_area + 1),
                                 elongate=rng.uniform(0.6, 1.6))
            cells = [(cx + dx, cy + dy) for dx, dy in sorted(shape)]
            if any(not (margin <= x < width - margin
                        and margin <= y < height - margin)
                   for x, y in cells):
                continue
            if avoid_clear and any(not clear_of(x, y, 2) for x, y in cells):
                continue
            terrain = rng.choice(choices)
            cli.isom_batch([(x, y, terrain) for x, y in cells])
            placed.append((cx, cy, terrain))
        return placed

    out = dict(ids)
    out["base"] = base
    # 낮은 땅부터 여러 종류로 얼룩지게 — 바닥이 한 색으로 남지 않게.
    out["patches"] = grow(ids["low"][1:] or ids["low"], patches, False, area)
    # 걸을 수 있는 높은 땅. 노는 자리 위에도 얹는다.
    out["rises"] = grow(ids["high"], rises, False, area)
    # 못 걷는 물은 나중에, 비워 둘 자리를 피해서.
    out["walls"] = grow(ids["blocked"], walls, True, area)
    return out


def isom_terrain_ids(cli: Cli, tileset_id: int | None = None) -> dict:
    """이 타일셋의 지형 종류를 **걷는가 · 고도** 로 갈라 놓는다.

    앞서는 이름에 "high"·"water" 가 들어갔는지로 골랐다. 그러면 정글
    13종 중 3종밖에 안 쓰게 된다 — 실측 RPG 유즈맵의 타일 그룹 중앙값이
    **248개**인데 3종으로는 어림없다.

    여기서는 두 가지 실측을 겹쳐 본다.

    - `data/terrain-types.json` — 지형 종류마다 **어느 타일 그룹**이
      나오고 **고도**가 얼마인지. `measure_terrain_types.py` 가 실제로
      칠해 보고 잰 것이다.
    - `terrain_groups()` — 그 타일 그룹을 **걸을 수 있는지**.

    돌려주는 것::

        {"names": {이름: 번호},
         "low":     [낮고 걷는 지형 번호들],
         "high":    [높고 걷는 지형 번호들],
         "blocked": [못 걷는 지형 번호들],
         "by_level": {고도: [번호들]}}
    """
    types = cli.terrain_types()
    out = {"names": types, "low": [], "high": [], "blocked": [],
           "by_level": {}}
    if tileset_id is None:
        return _isom_ids_by_name(types, out)

    table = terrain_types_table(tileset_id)
    if not table:
        return _isom_ids_by_name(types, out)
    groups = terrain_groups(cli, tileset_id)

    for name, num in types.items():
        info = table.get(name)
        if not info:
            continue
        gs = info.get("groups") or []
        if isinstance(gs, str):
            gs = json.loads(gs)
        lv = info.get("levels") or {}
        if isinstance(lv, str):
            lv = json.loads(lv.replace("'", '"'))
        level = int(max(lv, key=lambda k: lv[k])) if lv else 0
        # 그 지형이 내는 타일 그룹 가운데 걸을 수 있는 것이 반을 넘으면
        # 걷는 지형으로 본다. 가장자리 그룹은 못 걷는 것이 섞인다.
        walk = [groups.get(g, (0, 0))[1] for g in gs if g in groups]
        walkable = bool(walk) and sum(1 for w in walk if w) * 2 >= len(walk)
        out["by_level"].setdefault(level, []).append(num)
        if not walkable:
            out["blocked"].append(num)
        elif level >= 1:
            out["high"].append(num)
        else:
            out["low"].append(num)
    if not out["low"] and not out["high"]:
        return _isom_ids_by_name(types, out)
    return out


def _isom_ids_by_name(types: dict, out: dict) -> dict:
    """실측 표가 없을 때 쓰는 이름 짐작. 되도록 쓰지 않는다."""
    for name, num in types.items():
        low = name.lower()
        if "water" in low or "lava" in low or "magma" in low:
            out["blocked"].append(num)
        elif low.startswith("high") or "raised" in low:
            out["high"].append(num)
        else:
            out["low"].append(num)
    return out


# ---------------------------------------------------------------------------
# 업그레이드와 기술 — **유즈맵은 이걸 고친다**
#
# scmscx 유즈맵 479장을 CHK 로 훑어 보니 업그레이드를 중앙 7가지,
# 90% 지점은 61가지(전부) 고친다. 기술도 중앙 6, 90%가 44가지 전부다.
# 밀리맵은 둘 다 중앙 0 이다 — **유즈맵만의 버릇**이고 내 생성기는
# 하나도 안 고치고 있었다 (docs/chk/anatomy.md).
# ---------------------------------------------------------------------------

def setup_usemap_upgrades(cli: Cli, humans: int, *, free_levels: int = 0,
                          max_level: int | None = None,
                          mineral: int | None = None,
                          gas: int | None = None,
                          time: int | None = None,
                          which=range(0, 12)) -> int:
    """유즈맵답게 업그레이드 값을 정한다. 고친 가짓수를 돌려준다.

    유즈맵에서 업그레이드는 **파는 것**이다. 그대로 두면 밀리맵 비용과
    시간이 그대로라 유즈맵 흐름에 안 맞는다.

    - `free_levels` — 시작할 때 이미 되어 있는 단계
    - `max_level` — 올릴 수 있는 끝 (기본값은 그대로 둔다)
    - `mineral`·`gas`·`time` — 비용. 시간은 **노말 기준 초**다

    기본 `which` 는 공격력·방어력 열두 가지다 (종족별 지상·공중).
    """
    n = 0
    for up in which:
        args = ["upgrade", "set", cli.path, str(up), "--default-costs", "off"]
        if mineral is not None:
            args += ["--minerals", str(mineral), "--mineral-factor", "0"]
        if gas is not None:
            args += ["--gas", str(gas), "--gas-factor", "0"]
        if time is not None:
            args += ["--time", str(time), "--time-factor", "0"]
        if free_levels:
            args += ["--start-level", str(free_levels)]
        if max_level is not None:
            args += ["--max-level", str(max_level)]
        try:
            cli.edit(*args)
            n += 1
        except CliError:
            break
    return n


def setup_usemap_tech(cli: Cli, *, available=(), researched=(),
                      mineral: int | None = None, gas: int | None = None,
                      time: int | None = None, energy: int | None = None,
                      which=()) -> int:
    """기술(스킬) 값을 정한다. 고친 가짓수를 돌려준다.

    `available` · `researched` 는 플레이어 목록이거나 "all"/"none" 이다.
    **일부 기술은 에너지 설정이 안 먹는다** — 시즈 모드, 커맨드 센터
    감염 등 (카페 [기초6]).
    """
    n = 0
    for t in which:
        args = ["tech", "set", cli.path, str(t), "--default-costs", "off"]
        if mineral is not None:
            args += ["--minerals", str(mineral)]
        if gas is not None:
            args += ["--gas", str(gas)]
        if time is not None:
            args += ["--time", str(time)]
        if energy is not None:
            args += ["--energy", str(energy)]
        if available:
            args += ["--available",
                     available if isinstance(available, str)
                     else ",".join(str(p) for p in available)]
        if researched:
            args += ["--researched",
                     researched if isinstance(researched, str)
                     else ",".join(str(p) for p in researched)]
        try:
            cli.edit(*args)
            n += 1
        except CliError:
            break
    return n


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




def stamp_glyph(cli: Cli, pal: "Palette", glyph: str, x: int, y: int,
                w: int, h: int, role: str = "pad", thick: int = 2):
    """방 바닥에 **지형으로** 글자를 그린다 (O · X · 화살표 등).

    앞서 O 와 X 를 파일런으로 늘어놓았다. 보이긴 했지만 **유닛이라
    길을 막아** 발판 안에서 걸어다니지를 못했다 — 열두 초 안에 옮겨야
    하는 놀이에서 치명적이다. 글자는 밟고 지나갈 수 있어야 한다.

    지형으로 그리면 막지 않고, 발판 지형과 색이 멀어 글자로 읽힌다
    (`Palette` 가 맞닿는 짝의 색 차를 18 이상으로 고른다).
    """
    cx, cy = x + w / 2.0, y + h / 2.0
    rx, ry = (w - 4) / 2.0, (h - 4) / 2.0
    cells: set[tuple[int, int]] = set()

    def put(px: float, py: float):
        for dy in range(thick):
            for dx in range(thick):
                tx, ty = int(round(px)) + dx, int(round(py)) + dy
                if x + 1 <= tx < x + w - 1 and y + 1 <= ty < y + h - 1:
                    cells.add((tx, ty))

    if glyph.upper() == "O":
        n = int(4 * (rx + ry))
        for k in range(n):
            a = 2 * math.pi * k / n
            put(cx + rx * math.cos(a) - thick / 2,
                cy + ry * math.sin(a) - thick / 2)
    elif glyph.upper() == "X":
        n = int(2 * max(rx, ry)) * 2
        for k in range(n + 1):
            t = -1.0 + 2.0 * k / n
            put(cx + rx * t - thick / 2, cy + ry * t - thick / 2)
            put(cx + rx * t - thick / 2, cy - ry * t - thick / 2)
    else:
        raise CliError(f"모르는 글자입니다: {glyph!r}")

    # 한 칸씩 찍지 않고 줄 단위로 모아 찍는다 — 호출 수를 줄인다
    by_row: dict[int, list[int]] = {}
    for (tx, ty) in cells:
        by_row.setdefault(ty, []).append(tx)
    n_tiles = 0
    for ty, xs in sorted(by_row.items()):
        xs.sort()
        run = [xs[0]]
        for tx in xs[1:] + [None]:
            if tx is not None and tx == run[-1] + 1:
                run.append(tx)
                continue
            pal.fill(cli, role, run[0], ty, len(run), 1)
            n_tiles += len(run)
            if tx is None:
                break
            run = [tx]
    return n_tiles



_TERRAIN_TYPES: dict | None = None


def terrain_types_table(tileset_id: int) -> dict:
    """지형 종류 이름 → {번호, 타일 그룹, 고도}. `measure_terrain_types.py` 산."""
    global _TERRAIN_TYPES
    if _TERRAIN_TYPES is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "data", "terrain-types.json")
        try:
            with open(os.path.normpath(path), encoding="utf-8") as f:
                _TERRAIN_TYPES = json.load(f)["types"]
        except Exception:
            _TERRAIN_TYPES = {}
    return _TERRAIN_TYPES.get(_TS_NAMES.get(tileset_id & 7, ""), {})


def group_terrain_name(tileset_id: int) -> dict[int, str]:
    """타일 그룹 → 지형 종류 이름. 두뎃 갈래와 짝지을 때 쓴다."""
    out = {}
    for name, v in terrain_types_table(tileset_id).items():
        for g in v.get("groups", []):
            out.setdefault(int(g), name)
    return out

# ------------------------------------------------------------ 두뎃 꾸미기
#
# **왜 있는가.** 만든 유즈맵을 실제 인기 맵과 나란히 그려 보니, 실제
# 맵의 방은 테두리에 바위·잔해가 둘려 있어 "방" 으로 읽혔다. 내 방은
# 맨바닥이었다. 앞서 실측 표에 "유즈맵 두뎃 0개" 라고 적어 두었는데
# **그건 DD2 섹션만 센 것**이었다. 에디터에서 놓은 두뎃은 저장할 때
# 지형(MTXM)으로 눌러 담기므로 DD2 가 비어 있어도 타일에는 남는다.
#
# 다시 세어 보니 (내려받기 상위 유즈맵 76장, 두뎃 타일 = 그룹 1024 이상):
#
#     두뎃 타일을 쓴 맵            70/76 = **92%**
#     두뎃 칸 수                   중앙 **868칸** (사분위 398~3640)
#     걷기 경계 두 칸 안에 놓인 것  중앙 **89%** (아무 칸이나면 54%)
#
# 두뎃은 **경계에 몰려 있다.** 방 테두리를 꾸미는 것이 맞다.
#
# **두뎃은 길을 막을 수 있다.** 바위를 방 가운데 놓으면 동선이 끊긴다.
# 그래서 놓은 뒤 미니타일 길찾기로 **연결이 그대로인지 확인하고**, 깨지면
# 되돌려 성기게 다시 놓는다. 눈으로 좋아 보이려고 맵을 망가뜨리지 않는다.

DECOR_SKIP_KINDS = ("Bridges", "Cliff", "Water", "Coastal")


# 두뎃은 **가운데를 기준으로** 놓인다. `doodad place x y` 의 (x, y) 는
# 왼위가 아니라 한가운데다 — 6x6 두뎃을 (20,20) 에 놓으면 (17,17)~(22,22)
# 를 덮는다. 실제로 찍어 보고 확인했다.
#
# 앞서 이걸 왼위로 알고 썼다. 모든 두뎃이 제 크기의 절반만큼 밀려 놓여
# 방 테두리를 뚫거나 벽 위에 얹혔다.

def doodad_anchor(tile_x: int, tile_y: int, w: int, h: int) -> tuple[int, int]:
    """왼위 좌표 → `doodad place` 에 넘길 가운데 좌표."""
    return tile_x + w // 2, tile_y + h // 2


def doodad_topleft(center_x: int, center_y: int, w: int, h: int) -> tuple[int, int]:
    """가운데 좌표 → 실제로 덮는 왼위 좌표."""
    return center_x - w // 2, center_y - h // 2


def place_doodad(cli: Cli, doodad_id: int, tile_x: int, tile_y: int,
                 w: int, h: int, owner: int | None = None):
    """**왼위 좌표로** 두뎃을 놓는다. 가운데 변환을 여기서 한다."""
    cx, cy = doodad_anchor(tile_x, tile_y, w, h)
    args = ["doodad", "place", cli.path, str(doodad_id), str(cx), str(cy)]
    if owner is not None:
        args += ["--owner", str(owner)]
    args += ["--install", cli.install]
    cli.edit(*args)

_DOODAD_WALK: dict | None = None


def doodad_walk_table(tileset_id: int) -> dict[int, dict]:
    """두뎃 번호 → 크기·갈래·**걷기를 막는가**.

    `measure_doodads.py` 가 만든 `data/doodad-walk.json` 을 읽는다.
    타일셋마다 두뎃 240~320종 중 걷기를 안 막는 것이 160~235종이다
    (Badlands 246종 중 188종). 막는 것을 걸러 쓰면 놓고 나서 길찾기로
    되돌릴 일이 없다 — 그렇게 하던 판은 밀도를 못 올려 실측의 15분의 1
    (48칸 대 중앙 868칸) 에서 멈췄고 맵 하나에 1분이 넘게 걸렸다.
    """
    global _DOODAD_WALK
    if _DOODAD_WALK is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "data", "doodad-walk.json")
        try:
            with open(os.path.normpath(path), encoding="utf-8") as f:
                raw = json.load(f)["doodads"]
        except Exception:
            raw = {}
        _DOODAD_WALK = {n: {int(k): v for k, v in d.items()}
                        for n, d in raw.items()}
    return _DOODAD_WALK.get(_TS_NAMES.get(tileset_id & 7, ""), {})


def _components(grid, points: list[tuple[int, int]]) -> list[int]:
    """점마다 '어느 덩이에 속하나' 를 매긴다. 못 걷는 자리는 -1."""
    from collections import deque
    H, W = len(grid), len(grid[0])
    lab = [[0] * W for _ in range(H)]
    cur = 0
    starts = []
    for (px, py) in points:
        s = nearest_walkable(grid, px, py, radius=24)
        starts.append(s)
    out = []
    seen_lab: dict[tuple[int, int], int] = {}
    for s in starts:
        if s is None:
            out.append(-1)
            continue
        if lab[s[1]][s[0]]:
            out.append(lab[s[1]][s[0]])
            continue
        cur += 1
        q = deque([s])
        lab[s[1]][s[0]] = cur
        while q:
            x, y = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if (0 <= ny < H and 0 <= nx < W and grid[ny][nx]
                        and not lab[ny][nx]):
                    lab[ny][nx] = cur
                    q.append((nx, ny))
        out.append(cur)
    # 같은 덩이인지만 중요하다 — 번호를 정규화한다
    norm, m = [], {}
    for v in out:
        if v < 0:
            norm.append(-1)
            continue
        norm.append(m.setdefault(v, len(m)))
    return norm


def decorate_rim(cli: Cli, tileset_id: int, rooms, rng,
                 keep_clear=(), band: int = 3, density: float = 0.55,
                 max_area: int = 12, tries: int = 4,
                 want_tiles: int | None = 400, inset: int = 2) -> int:
    """방 테두리 안쪽 띠에 작은 두뎃을 흩는다.

    `rooms` 는 방 네모 `(x, y, w, h)` 목록, `keep_clear` 는 건드리면 안 되는
    네모 목록(유닛 무리·비콘 발판·상점 자리)이다.

    놓은 뒤 **연결이 그대로인지 확인한다.** 방마다 가운데와 네 변 가운데를
    "이어져 있어야 하는 점" 으로 잡고, 놓기 전과 같은 덩이에 있는지 본다.
    깨지면 되돌리고 밀도를 반으로 줄여 다시 한다.

    `want_tiles` 는 채우고 싶은 두뎃 **칸** 수다 (실측 중앙 868칸,
    사분위 398~3640). 처음에 density 0.18·band 2 로 했더니 60칸이 나와
    실측의 15분의 1 이었다 — 눈으로도 티가 안 났다. 자리가 모자라면
    밀도를 올려 가며 채운다.

    돌려주는 것은 실제로 놓인 두뎃 개수다.
    """
    import shutil

    # **걷기를 막는 두뎃은 아예 쓰지 않는다.** 표가 없으면 옛 방식으로
    # 물러서지만, 그때는 밀도를 못 올린다.
    safe = doodad_walk_table(tileset_id)
    cat = [d for d in cli.doodad_catalogue()
           if d["w"] * d["h"] <= max_area
           and not any(k in d["kind"] for k in DECOR_SKIP_KINDS)
           and (not safe or (safe.get(d["id"], {}).get("walk")
                             and safe[d["id"]].get("tiles", 0) > 0))]
    if not cat or not rooms:
        return 0

    # **두뎃 갈래는 지형 종류 이름과 같다.** High Dirt 바닥에는 갈래가
    # "High Dirt" 인 두뎃을 놓아야 한다. Dirt 두뎃을 올리면 안 어울린다 —
    # 실제로 그렇게 만들어 지적받았다. 갈래별로 나눠 두고, 놓을 자리
    # 밑의 타일 그룹이 무슨 지형인지 보고 고른다.
    by_kind: dict[str, list[dict]] = {}
    for d in cat:
        by_kind.setdefault(d["kind"], []).append(d)
    gname = group_terrain_name(tileset_id)
    tile_rows = cli.tiles(0, 0, cli.info()["width"], cli.info()["height"])

    def kind_here(tx: int, ty: int) -> str | None:
        if not (0 <= ty < len(tile_rows) and 0 <= tx < len(tile_rows[0])):
            return None
        return gname.get(tile_rows[ty][tx] >> 4)
    if safe:
        tries = 1          # 막는 것을 걸렀으니 되돌릴 일이 없다

    W, H = cli.info()["width"], cli.info()["height"]
    probes: list[tuple[int, int]] = []
    for (x, y, w, h) in rooms:
        probes += [((x + w // 2) * 4 + 2, (y + h // 2) * 4 + 2),
                   ((x + 2) * 4 + 2, (y + h // 2) * 4 + 2),
                   ((x + w - 3) * 4 + 2, (y + h // 2) * 4 + 2),
                   ((x + w // 2) * 4 + 2, (y + 2) * 4 + 2),
                   ((x + w // 2) * 4 + 2, (y + h - 3) * 4 + 2)]
    before = _components(walk_grid(cli, tileset_id, 0, 0, W, H), probes)

    def blocked(px, py, dw, dh):
        for (cx, cy, cw, ch) in keep_clear:
            if not (px + dw <= cx or cx + cw <= px
                    or py + dh <= cy or cy + ch <= py):
                return True
        return False

    backup = cli.path + ".predecor"
    shutil.copy(cli.path, backup)
    placed = 0
    try:
        for attempt in range(tries):
            spots = []
            for (x, y, w, h) in rooms:
                # **테두리 바로 위에 놓으면 벽을 뚫는다.** 두뎃은 밑에
                # 무엇이 있든 제 타일을 써 버리므로, 방 테두리에 걸치면
                # 못 걷던 벽 칸이 걷는 두뎃 바닥으로 바뀌어 **방에 구멍이
                # 난다.** 실제로 그렇게 해서 연결 검사가 떨어졌고 두뎃이
                # 하나도 안 놓였다. `inset` 만큼 안으로 물린다.
                x0, y0 = x + inset, y + inset
                x1, y1 = x + w - inset, y + h - inset
                edge = []
                for ty in range(y0, y1):
                    for tx in range(x0, x1):
                        near = (tx - x0 < band or x1 - 1 - tx < band
                                or ty - y0 < band or y1 - 1 - ty < band)
                        if near:
                            edge.append((tx, ty))
                rng.shuffle(edge)
                want = int(len(edge) * density)
                for (tx, ty) in edge[:want]:
                    pool = by_kind.get(kind_here(tx, ty) or "") or []
                    if not pool:
                        continue          # 그 지형에 맞는 두뎃이 없으면 안 놓는다
                    d = rng.choice(pool)
                    if tx + d["w"] > x1 or ty + d["h"] > y1:
                        continue
                    if blocked(tx, ty, d["w"], d["h"]):
                        continue
                    spots.append((d["id"], tx, ty, d["w"], d["h"]))
            rng.shuffle(spots)
            # **두뎃끼리 겹치면 안 된다.** 겹쳐 놓으면 앞의 두뎃 타일이
            # 반만 덮여 조각이 남고, 그 조각이 못 걷는 타일이라 길이
            # 막힌다. 낱개로는 다 안전한 두뎃인데 117개를 섞어 놓으니
            # 연결 검사가 떨어졌다 — 자리 순서대로 놓았을 때는 우연히
            # 안 겹쳐 통과했고, 섞고 나서야 드러났다.
            taken: list[tuple[int, int, int, int]] = []

            def overlaps(px, py, dw, dh):
                for (ax, ay, aw, ah) in taken:
                    if not (px + dw <= ax or ax + aw <= px
                            or py + dh <= ay or ay + ah <= py):
                        return True
                return False

            area = 0
            for (did, tx, ty, dw, dh) in spots:
                if want_tiles and area >= want_tiles:
                    break
                if overlaps(tx, ty, dw, dh):
                    continue
                try:
                    # **가운데 기준으로 바꿔 넘긴다** — (tx, ty) 는 왼위다
                    place_doodad(cli, did, tx, ty, dw, dh)
                    placed += 1
                    area += dw * dh
                    taken.append((tx, ty, dw, dh))
                except CliError:
                    pass
            # **DD2 항목을 지우고 지형만 남긴다.** 실제 맵의 두뎃은
            # 저장할 때 지형으로 눌러 담겨 DD2 가 비어 있다 (실측 DD2
            # 중앙 0~18개, 타일은 868칸). 항목을 남겨 두면 나중에
            # `doodad check` 가 자리 어긋남을 잡거나 에디터가 두뎃을
            # 통째로 옮겨 지형을 되돌릴 수 있다.
            if placed:
                try:
                    cli.edit("doodad", "to-terrain", cli.path,
                             "--install", cli.install)
                except CliError:
                    pass
            after = _components(walk_grid(cli, tileset_id, 0, 0, W, H), probes)
            if after == before:
                return placed
            # 길이 끊겼다 — 되돌리고 성기게
            shutil.copy(backup, cli.path)
            placed = 0
            density /= 2.0
        return 0
    finally:
        if os.path.exists(backup):
            os.remove(backup)

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


def part_ally_humans(humans: list[str]) -> list[str]:
    """**사람끼리 동맹을 트리거로 묶는다.**

    세력(Forces) 의 Allied·Shared Vision·Enable Allied Victory 깃발이
    먹는지는 **확인하지 못했다** — 카페 [기초4] 는 안 먹는다고 적고,
    실측 맵은 대부분 켜 둔다. 어느 쪽이든 **트리거로 묶으면 확실하다.**
    협동 유즈맵이면 이것을 넣는다.

    맵 시작에 한 번만 돌면 된다 — `Preserve` 를 붙이지 않는다.
    """
    out = []
    for h in humans:
        others = ",".join(f'"{o}"' for o in humans if o != h)
        if not others:
            continue
        out.append(f'Trigger("{h}"){{\nConditions:\n\tAlways();\n\n'
                   f'Actions:\n'
                   f'\tSet Alliance Status({others}, Allied Victory);\n}}')
    return out

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


def part_token_shop(player: str, loc: str, token: str, effect: list[str],
                    label: str, consume: str = "remove",
                    count: int = 1) -> list[str]:
    """**물건을 갖다 주고 받는 상점** — 실측 유즈맵의 주된 상점 꼴이다.

    인기 유즈맵 134장에서 `Bring` 조건이 달린 트리거 24,125개를 세어
    보니, 값을 치르는 방식은 이랬다:

        데려온 것을 치운다   25%      조건에서 돈을 본다(Accumulate) 13%
        데려온 것을 밀어낸다 21%      자원을 깎는다(Set Resources)    7%
        데려온 것을 죽인다   16%      점수를 깎는다                   2%
                                      주인을 바꾼다                   5%

    주는 것은 유닛 45% · 죽음 수 칸(상태) 14% · 돈 9% · 스위치 8% ·
    능력치 3% 다.

    즉 **돈으로 사는 상점은 소수파**고, 표준은 "유닛을 자리로 데려가면
    그 유닛이 사라지고 다른 것이 나온다" 는 교환이다. `part_beacon_shop`
    하나만 두었던 것은 실측의 7~13%만 덮는 것이었다.

    치운다는 것이 곧 **잠금**이다. 물건이 사라지니 조건이 다시 참이 되지
    않는다 — 돈 상점처럼 따로 밀어낼 필요가 없다. 실측에서도 `Preserve`
    가 붙은 것(80%) 중 **56%가 밀어내기·치우기로 잠근다.** 스위치로
    잠그는 것은 1%, 자원 소비로 잠그는 것은 0% 다. (21%는 눈에 보이는
    잠금이 없다 — 인기 맵도 그렇다는 뜻이니, 잠금이 없다는 지적은
    보상 트리거에만 세게 걸고 나머지는 참고로만 본다.)

    `consume` 은 "remove"(치운다) · "kill"(죽인다) · "give"(주인을 바꾼다)
    중 하나다. 죽이면 상대에게 킬 점수가 들어가므로, 점수를 쓰는 맵에서는
    "remove" 를 쓴다.
    """
    if consume == "remove":
        take = f'\tRemove Unit At Location("{player}", "{token}", {count}, "{loc}");'
    elif consume == "kill":
        take = f'\tKill Unit At Location("{player}", "{token}", {count}, "{loc}");'
    elif consume == "give":
        take = f'\tGive Units to Player("{player}", "Player 8", "{token}", {count}, "{loc}");'
    else:
        raise CliError(f"consume 은 remove·kill·give 중 하나입니다: {consume!r}")
    acts = [take] + list(effect)
    acts.append(f'\tDisplay Text Message(Always Display, "{label}");')
    acts.append('\tPlay WAV("sound\\\\Misc\\\\Button.wav", 0);')
    acts.append('\tPreserve Trigger();')
    return [f'Trigger("{player}"){{\nConditions:\n'
            f'\tBring("{player}", "{token}", "{loc}", At least, {count});\n\n'
            f'Actions:\n' + "\n".join(acts) + '\n}']


def part_beacon_shop(player: str, beacon_loc: str, cost: int, effect: list[str],
                     label: str, push_to: str, resource: str = "ore") -> list[str]:
    """**돈으로 사는** 비콘 상점 한 자리.

    실측에서는 소수파다 — `Bring` 조건이 달린 트리거 24,125개 중 돈을
    보는 것이 13%, 자원을 깎는 것이 7% 였다. 표준은 물건 교환이다
    (`part_token_shop`). 값을 미네랄로 매기고 싶을 때만 이걸 쓴다.

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


def floor_triggers(humans: int, system_owner: str,
                   min_players: int = 1) -> list[str]:
    """바닥 중 **트리거로 되는 부분** — 하이퍼 세 벌 + 빈 자리 정리."""
    return hyper_triggers(system_owner) + \
        absent_player_cleanup(humans, system_owner, min_players)


def usemap_floor(cli: Cli, humans: int, system_owner: str,
                 computers: "list[int] | None" = None,
                 min_players: int = 1, race: str = "terran") -> list[str]:
    """**품질 바닥.** 어떤 유즈맵이든 이것부터 깔고 시작한다.

    바닥은 트리거만이 아니다. 앞서 이 함수가 트리거만 돌려주는 바람에
    "바닥을 깔았다" 고 하면서 종족이 '선택 가능' 으로 남고 시야도 안
    열린 맵이 나왔다. 이름이 바닥이면 바닥을 다 해야 한다:

      - 사람 슬롯 종족을 못 박고(선택 가능이면 배치 유닛이 무시된다),
        트리거가 쓰는 적을 컴퓨터로, 시작 위치 섞기를 끈다
      - 사람 **모두**에게 Map Revealer 를 깐다
      - 하이퍼 트리거 세 벌 (컴퓨터 소유)
      - 들어오지 않은 자리 정리와 인원 판정

    `computers` 는 트리거가 적·시스템으로 쓰는 플레이어 번호다. 주지
    않으면 `system_owner` 하나만 컴퓨터로 만든다.
    """
    if computers is None:
        m = re.search(r"(\d+)", system_owner)
        computers = [int(m.group(1))] if m else []
    setup_usemap_players(cli, humans, computers, race=race)
    reveal_for_all(cli, humans)
    return floor_triggers(humans, system_owner, min_players)


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
