#!/usr/bin/env python3
"""scmscx 맵을 **CHK 섹션 단위로** 다시 훑는다.

지형·유닛·트리거는 이미 쟀다. 여기서는 그동안 안 본 칸들을 본다 —
스위치 · 스트링 · 유닛 속성(CUWP) · 세력 · 소리 · 업그레이드 설정.
**한계에 얼마나 가까이 가는지**를 알아야 내 맵이 어디서 터질지 안다.

    python3 measure_anatomy.py <맵폴더>... [--kind usemap]
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import scmap  # noqa: E402

OUT = os.path.join(HERE, "..", "data", "anatomy.json")


def chk_sections(path: str, tmp: str) -> dict:
    out = os.path.join(tmp, "x.chk")
    subprocess.run([scmap.find_cli(), "chk", path, out],
                   capture_output=True, check=True)
    buf = open(out, "rb").read()
    secs, i = {}, 0
    while i + 8 <= len(buf):
        name = buf[i:i + 4].decode("latin-1").strip()
        ln = struct.unpack_from("<i", buf, i + 4)[0]
        i += 8
        if ln < 0:
            continue
        secs[name] = buf[i:i + ln]
        i += ln
    return secs


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0] if len(b) >= o + 2 else 0


def measure(secs: dict) -> dict | None:
    dim = secs.get("DIM")
    if not dim or len(dim) < 4:
        return None
    w, h = struct.unpack_from("<HH", dim, 0)
    r = {"w": w, "h": h}

    # --- 스트링: 몇 개를 쓰나. 1024 제한에 얼마나 가까운가 ---
    strx = secs.get("STRx")
    strs = secs.get("STR")
    if strx:
        r["str_count"] = struct.unpack_from("<I", strx, 0)[0] if len(strx) >= 4 else 0
        r["str_bytes"] = len(strx)
        r["str_extended"] = True
    elif strs:
        r["str_count"] = u16(strs, 0)
        r["str_bytes"] = len(strs)
        r["str_extended"] = False

    # --- 스위치 이름: 256칸 중 몇 개에 이름을 붙였나 ---
    swnm = secs.get("SWNM")
    if swnm:
        ids = struct.unpack_from(f"<{min(256, len(swnm)//4)}I", swnm, 0)
        r["switches_named"] = sum(1 for x in ids if x)

    # --- CUWP: 유닛 속성. 한 맵에 64가지 제한 ---
    cuwp = secs.get("UPRP")
    upus = secs.get("UPUS")
    if upus:
        r["cuwp_used"] = sum(1 for b in upus[:64] if b)
    elif cuwp:
        r["cuwp_used"] = sum(1 for k in range(min(64, len(cuwp) // 20))
                             if any(cuwp[k * 20:(k + 1) * 20]))

    # --- 세력 ---
    forc = secs.get("FORC")
    if forc and len(forc) >= 20:
        r["force_of_player"] = list(forc[:8])
        r["force_flags"] = list(forc[16:20])
        r["forces_used"] = len({f for f in forc[:8]})

    # --- 소리 ---
    wav = secs.get("WAV")
    if wav:
        ids = struct.unpack_from(f"<{len(wav)//4}I", wav, 0)
        r["sounds"] = sum(1 for x in ids if x)

    # --- 업그레이드·기술 설정을 건드렸나 ---
    for key, name in (("UPGx", "upgrades_custom"), ("UPGS", "upgrades_custom"),
                      ("TECx", "tech_custom"), ("TECS", "tech_custom")):
        b = secs.get(key)
        if b and name not in r:
            # 첫 칸이 "기본값 따름" 배열이다. 0 이면 맵이 고친 것.
            n = 61 if key.startswith("UPG") else 44
            r[name] = sum(1 for x in b[:n] if x == 0)

    # --- 플레이어 슬롯 ---
    ownr = secs.get("OWNR")
    side = secs.get("SIDE")
    # OWNR 값 (Sc::Player::SlotType). 처음에 5·6 을 사람으로 잡았다가
    # 틀렸다 — **5 가 컴퓨터, 6 이 열림(사람)** 이다. cli_common.cpp 의
    # kSlotTypes 가 정본이다.
    #   0 사용 안 함 · 1 컴퓨터(게임) · 2 사람(게임) · 3 구조 대상
    #   5 컴퓨터 · 6 열림 · 7 중립 · 8 닫힘
    if ownr and len(ownr) >= 8:
        slots = list(ownr[:8])
        r["humans"] = sum(1 for s in slots if s in (2, 6))
        r["computers"] = sum(1 for s in slots if s in (1, 5))
        r["rescuable"] = sum(1 for s in slots if s == 3)
    # SIDE: 0 저그 · 1 테란 · 2 프로토스 · 3 독립 · 4 중립
    #       **5 선택 가능** · 6 무작위 · 7 사용 안 함
    if side and len(side) >= 8:
        r["race_user_select"] = sum(1 for x in side[:8] if x == 5)

    # --- 브리핑 트리거 수 (MBRF 는 개수 칸이 없다) ---
    mbrf = secs.get("MBRF")
    if mbrf:
        r["briefing_triggers"] = len(mbrf) // 2400
    trig = secs.get("TRIG")
    if trig:
        r["triggers"] = len(trig) // 2400
    return r


def q(vals, p):
    vals = sorted(v for v in vals if v is not None)
    return vals[min(len(vals) - 1, int(len(vals) * p))] if vals else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description="CHK 섹션을 다시 훑는다")
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--kind", default="usemap")
    a = ap.parse_args(argv)

    paths = []
    for d in a.dirs:
        for f in sorted(os.listdir(d)):
            if f.lower().endswith((".scx", ".scm")):
                paths.append(os.path.join(d, f))

    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for p in paths:
            try:
                r = measure(chk_sections(p, tmp))
            except Exception:
                continue
            if r:
                rows.append(r)

    print(f"{a.kind} {len(rows)}장\n")
    out = {"_about": "scmscx 맵을 CHK 섹션 단위로 훑은 표. "
                     "measure_anatomy.py 가 만든다. 스위치·스트링·유닛 "
                     "속성(CUWP)·세력·소리·업그레이드 설정처럼 그동안 "
                     "안 보던 칸을 본다 — **한계에 얼마나 가까이 가는가**.",
           "_n": len(rows), "_kind": a.kind}

    def stat(key, label, unit=""):
        vals = [r[key] for r in rows if key in r]
        if not vals:
            return
        out[key] = {"n": len(vals), "median": q(vals, 0.5),
                    "q1": q(vals, 0.25), "q3": q(vals, 0.75),
                    "p90": q(vals, 0.9), "max": max(vals)}
        print(f"  {label:26s} 중앙 {q(vals,0.5):6} · 90% {q(vals,0.9):6} · "
              f"최대 {max(vals):6}{unit}  ({len(vals)}장)")

    stat("str_count", "스트링 개수")
    stat("str_bytes", "스트링 바이트")
    stat("switches_named", "이름 붙인 스위치")
    stat("cuwp_used", "유닛 속성(CUWP) 가짓수")
    stat("sounds", "소리 개수")
    stat("upgrades_custom", "고친 업그레이드")
    stat("tech_custom", "고친 기술")
    stat("forces_used", "쓰는 세력 수")
    stat("humans", "사람 슬롯(열림)")
    stat("computers", "컴퓨터 슬롯")
    stat("rescuable", "구조 대상 슬롯")
    stat("triggers", "트리거")
    stat("briefing_triggers", "브리핑 트리거")

    ext = [r for r in rows if r.get("str_extended")]
    out["str_extended_pct"] = 100 * len(ext) // max(len(rows), 1)
    print(f"\n  STRx(확장 스트링)를 쓰는 맵  {out['str_extended_pct']}%")

    us = [r["race_user_select"] for r in rows if "race_user_select" in r]
    if us:
        out["race_user_select_pct"] = 100 * sum(1 for x in us if x) // len(us)
        print(f"  종족이 '선택 가능' 인 슬롯이 있는 맵  "
              f"{out['race_user_select_pct']}%")

    # 세력 깃발이 실제로 어떻게 켜져 있나
    fl = collections.Counter()
    for r in rows:
        for f in r.get("force_flags", []):
            fl[f] += 1
    if fl:
        out["force_flags"] = dict(fl.most_common(8))
        print(f"  세력 깃발 값 분포  {dict(fl.most_common(5))}")

    path = os.path.normpath(OUT)
    prev = {}
    if os.path.exists(path):
        try:
            prev = json.load(open(path, encoding="utf-8"))
        except Exception:
            prev = {}
    prev[a.kind] = out
    prev["_about"] = out["_about"]
    with open(path, "w", encoding="utf-8") as f:
        json.dump(prev, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
