#!/usr/bin/env python3
"""생성된 트리거 글의 최소 계약.

check(text) 가 빈 목록이면 통과다. 문구가 있으면 그 트리거는
하이퍼 트리거와 맞물려 안내를 건너뛰거나, 유닛을 매 프레임 만들거나,
서 있는 동안 돈을 깎거나, WAV 경로의 역슬래시가 한 겹이다.

같은 트리거 안의 Set To N / At most N-1 은 한 번만 울리는 걸쇠라
점프로 보지 않는다. Current Player 는 Current Player 와만 부딪친다.
"""
from __future__ import annotations

import re

_TRIGGER = re.compile(r"Trigger\((.*?)\)\{", re.S)
_ELAPSED = re.compile(r"Elapsed Time\(\s*At least\s*,\s*(\d+)\s*\)")
_SET_TO = re.compile(
    r'Set Deaths\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*Set To\s*,\s*(\d+)\s*\)')
_AT_MOST = re.compile(
    r'Deaths\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*At most\s*,\s*(\d+)\s*\)')
_EXACTLY_0 = re.compile(
    r'Deaths\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*Exactly\s*,\s*0\s*\)')
_CREATE = re.compile(
    r'Create Unit(?: with Properties)?\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*\d+\s*,\s*"([^"]*)"')
_BRING_AT_LEAST = re.compile(
    r'Bring\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*At least\s*,\s*(\d+)\s*\)')
_MOVE = re.compile(
    r'Move Unit\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*All\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)')
_SWITCH_CLEAR = re.compile(r'Switch\(\s*"([^"]*)"\s*,\s*not set\s*\)')
_SWITCH_SET = re.compile(r'Set Switch\(\s*"([^"]*)"\s*,\s*set\s*\)')
_WAV = re.compile(r'Play WAV\("([^"]*)"')
_COUNTDOWN_RESET = re.compile(
    r"Set Countdown Timer\(\s*Set To\s*,\s*([1-9]\d*)\s*\)")


def _triggers(text: str):
    found = []
    for match in _TRIGGER.finditer(text):
        start = match.end()
        depth = 1
        i = start
        while i < len(text) and depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        body = text[start:i - 1]
        parts = re.split(r"\nActions:\s*", body, maxsplit=1)
        cond = parts[0]
        acts = parts[1] if len(parts) > 1 else ""
        found.append((cond, acts))
    return found


def _elapsed(cond: str):
    times = [int(n) for n in _ELAPSED.findall(cond)]
    return min(times) if times else None


def _lone_backslash(path: str) -> bool:
    i = 0
    while i < len(path):
        if path[i] == "\\":
            if i + 1 < len(path) and path[i + 1] == "\\":
                i += 2
                continue
            return True
        i += 1
    return False


def _one_shot(cond: str, acts: str) -> bool:
    armed = {(p, u) for p, u in _EXACTLY_0.findall(cond)}
    for player, unit, value in _SET_TO.findall(acts):
        if int(value) != 0 and (player, unit) in armed:
            return True
    cleared = set(_SWITCH_CLEAR.findall(cond))
    return bool(cleared & set(_SWITCH_SET.findall(acts)))


def _spawn_capped(cond: str, unit: str, loc: str) -> bool:
    """생성 위치이거나, 그 유닛의 수량 상한이 조건에 있으면 매 프레임 반복이 아니다.

    구역 보충은 스폰 칸이 아니라 그 칸을 품은 방의 Bring(유닛, At most)로
    상한을 건다. 방 밖에 있는 다른 유닛을 세는 Bring 은 상한이 아니다.
    """
    if re.search(
            rf'Bring\(\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"{re.escape(loc)}"\s*,',
            cond):
        return True
    if re.search(
            rf'Bring\(\s*"[^"]*"\s*,\s*"{re.escape(unit)}"\s*,\s*"[^"]*"\s*,\s*(?:At most|Exactly)\s*,',
            cond):
        return True
    return re.search(
        rf'Command\(\s*"[^"]*"\s*,\s*"{re.escape(unit)}"\s*,\s*(?:At most|Exactly)\s*,',
        cond) is not None


def _countdown_gated(cond: str, text: str) -> bool:
    return ("Countdown Timer(At most, 0)" in cond
            and _COUNTDOWN_RESET.search(text) is not None)


def _pad_locked(cond: str, acts: str) -> bool:
    """비콘에서 사면 그 칸 밖으로 밀어 낸다. 생성 위치는 집이라도 매 프레임 반복되지 않는다."""
    moved = {src for _player, _unit, src, dst in _MOVE.findall(acts) if src != dst}
    for _player, _unit, loc, count in _BRING_AT_LEAST.findall(cond):
        if int(count) >= 1 and loc in moved:
            return True
    return False


def _wait_paced(acts: str) -> bool:
    """Create 뒤에 Wait 가 있으면 매 프레임 반복이 아니다.

    Wait 는 그 트리거의 실행 자체를 그 시간만큼 묶는다 — Preserve Trigger
    로 다시 조건을 검사하기 전에 Wait 가 끝나야 하므로, 스위치·수량
    잠금 없이도 폭탄피하기의 킬-생성-킬 반복처럼 자연히 박자가 난다.
    """
    return "Wait(" in acts


def check(text: str) -> list[str]:
    """트리거 글의 계약 위반. 비어 있으면 통과."""
    findings: list[str] = []
    triggers = _triggers(text)
    sets = []
    ats = []
    for index, (cond, acts) in enumerate(triggers):
        when = _elapsed(cond)
        for player, unit, value in _SET_TO.findall(acts):
            sets.append((index, player, unit, int(value), when))
        for player, unit, value in _AT_MOST.findall(cond):
            ats.append((index, player, unit, int(value), when))
        if "Preserve Trigger()" in acts:
            for _owner, unit, loc in _CREATE.findall(acts):
                if (_one_shot(cond, acts) or _spawn_capped(cond, unit, loc)
                        or _countdown_gated(cond, text) or _pad_locked(cond, acts)
                        or _wait_paced(acts)):
                    continue
                findings.append(
                    "생성 잠금 없음: Preserve 와 Create 가 있는데 "
                    f"{loc} 의 수량 조건도, Exactly 0 다음 Set To 도, "
                    "스위치 걸쇠도, 카운트다운 재설정도 없습니다")
            paid = ("Subtract" in acts and "Set Resources" in acts) or (
                "Accumulate(" in cond)
            if paid:
                moved = {src for _p, _u, src, dst in _MOVE.findall(acts)
                         if src != dst}
                for _player, _unit, loc, count in _BRING_AT_LEAST.findall(cond):
                    if int(count) < 1 or loc in moved:
                        continue
                    findings.append(
                        "유료 구역이 유닛을 밖으로 옮기지 않습니다. "
                        f"하이퍼 트리거에서 {loc} 에 서 있는 동안 매 프레임 결제됩니다")
    for s_i, s_player, s_unit, s_value, s_when in sets:
        for a_i, a_player, a_unit, a_value, a_when in ats:
            if s_i == a_i or s_player != a_player or s_unit != a_unit:
                continue
            if a_value >= s_value:
                continue
            # 시각이 없는 Set To 는 시각이 있는 At most 보다 먼저 울릴 수 있다.
            # 둘 다 시각이 없으면 다른 걸쇠(웨이브 번호·스위치)의 순서로 본다.
            if s_when is None and a_when is None:
                continue
            if s_when is not None and a_when is None:
                continue
            if s_when is not None and a_when is not None and s_when > a_when:
                continue
            findings.append(
                "죽음 수 점프: "
                f"{s_player} 의 {s_unit} 을 {s_value} 로 찍는 시각({s_when})이 "
                f"At most {a_value} 안내의 시각({a_when})보다 이르거나 같아서 "
                "그 안내가 영영 거짓이 됩니다")
    for path in _WAV.findall(text):
        if _lone_backslash(path) or re.search(r"sound(?![\\/])[A-Za-z]", path):
            findings.append(
                "Play WAV 경로의 역슬래시가 한 겹이라 저장하면 폴더가 붙습니다: "
                + path)
    return findings
