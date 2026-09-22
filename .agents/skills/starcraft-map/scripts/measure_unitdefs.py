#!/usr/bin/env python3
"""유즈맵이 **유닛 능력치를 실제로 어떻게 고치는가**를 전수로 잰다.

실측 유즈맵의 98% 가 UNIS/UNIx 를 건드린다. 그런데 "무엇을 어떻게" 는
표가 없었다 — 그래서 내 맵은 하나도 안 고치고 있었다. 이 스크립트가
`data/unitdefs.json` 을 만든다.

UNIS(4048) · UNIx(4168) 자리 (유닛 228종):

    0      228*1   기본값 따름 (0 이면 맵이 고친 것)
    228    228*4   체력 (1/256 단위)
    1140   228*2   방패
    1596   228*1   방어력
    1824   228*2   생산 시간 (1/15초)
    2280   228*2   미네랄
    2736   228*2   가스
    3192   228*2   이름 스트링 번호
    3648   W*2     무기 기본 피해   (UNIS W=100, UNIx W=130)
    ...    W*2     무기 업그레이드 증가량
"""
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

OUT = os.path.join(HERE, "..", "data", "unitdefs.json")
NUNIT = 228


def chk_sections(path: str, tmp: str) -> dict:
    """CHK 를 뽑아 섹션 이름 → 몸통 으로 읽는다. 뒤에 나온 것이 이긴다."""
    out = os.path.join(tmp, "x.chk")
    subprocess.run([scmap.find_cli(), "chk", path, out],
                   capture_output=True, check=True)
    buf = open(out, "rb").read()
    secs, i = {}, 0
    while i + 8 <= len(buf):
        # 섹션 이름은 4바이트 고정이라 "DIM " 처럼 뒤에 빈칸이 붙는다.
        # 그대로 키로 쓰면 secs["DIM"] 이 안 잡힌다.
        name = buf[i:i + 4].decode("latin-1").strip()
        ln = struct.unpack_from("<i", buf, i + 4)[0]
        i += 8
        if ln < 0:
            # 음수 길이는 **앞 섹션을 잘라 내는 수법**이다. 몸통은 없다.
            # 앞서 여기서 i 를 뒤로 되돌렸는데, 그러면 같은 자리를
            # 영원히 다시 읽어 무한 루프가 된다. 머리만 넘기고 간다.
            continue
        secs[name] = buf[i:i + ln]
        i += ln
    return secs


def read_unitdefs(body: bytes) -> dict:
    """유닛 번호 → 고친 값들. 기본값을 따르는 유닛은 안 담는다."""
    if len(body) < 3648:
        return {}
    dflt = body[0:NUNIT]
    hp = struct.unpack_from("<228I", body, 228)
    sh = struct.unpack_from("<228H", body, 1140)
    ar = body[1596:1596 + NUNIT]
    bt = struct.unpack_from("<228H", body, 1824)
    mi = struct.unpack_from("<228H", body, 2280)
    ga = struct.unpack_from("<228H", body, 2736)
    out = {}
    for u in range(NUNIT):
        if dflt[u]:
            continue
        out[u] = {"hp": hp[u] // 256, "shields": sh[u], "armor": ar[u],
                  "build_time": bt[u], "minerals": mi[u], "gas": ga[u]}
    return out


def unit_names() -> dict[int, str]:
    """유닛 번호 → 이름. CLI 가 아는 이름을 그대로 받는다."""
    r = subprocess.run([scmap.find_cli(), "unit", "types"],
                       capture_output=True, text=True)
    out = {}
    for line in r.stdout.splitlines():
        line = line.strip()
        if not line or not line[0].isdigit():
            continue
        num, _, rest = line.partition(" ")
        try:
            out[int(num)] = rest.strip()
        except ValueError:
            pass
    return out


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description="유즈맵의 유닛 설정을 전수로 잰다")
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--kind", default="usemap")
    a = ap.parse_args(argv)

    paths = []
    for d in a.dirs:
        for f in sorted(os.listdir(d)):
            if f.lower().endswith((".scx", ".scm")):
                paths.append(os.path.join(d, f))

    names = unit_names()
    n_map = 0
    n_custom = 0
    per_map_count = []
    # 유닛별: 몇 장이 고쳤나 / 어떤 값으로
    touched = collections.Counter()
    hp_vals = collections.defaultdict(list)
    cost_vals = collections.defaultdict(list)
    field_hit = collections.Counter()

    with tempfile.TemporaryDirectory() as tmp:
        for p in paths:
            try:
                secs = chk_sections(p, tmp)
            except Exception:
                continue
            body = secs.get("UNIx") or secs.get("UNIS")
            if not body:
                continue
            n_map += 1
            defs = read_unitdefs(body)
            if not defs:
                continue
            n_custom += 1
            per_map_count.append(len(defs))
            for u, v in defs.items():
                touched[u] += 1
                hp_vals[u].append(v["hp"])
                cost_vals[u].append((v["minerals"], v["gas"]))
                for k in ("hp", "shields", "armor", "build_time",
                          "minerals", "gas"):
                    if v[k]:
                        field_hit[k] += 1

    def med(xs):
        xs = sorted(xs)
        return xs[len(xs) // 2] if xs else 0

    print(f"유닛 설정 섹션이 있는 맵 {n_map}장 중 "
          f"{n_custom}장({100*n_custom//max(n_map,1)}%)이 고친다")
    print(f"고치는 유닛 종류 중앙값 {med(per_map_count)}종")
    print("\n가장 자주 고치는 유닛 25종 (맵 수 / 체력 중앙값):")
    for u, n in touched.most_common(25):
        print(f"  {names.get(u, str(u)):28s} {n:4d}장  "
              f"체력 {med(hp_vals[u]):6d}  "
              f"미네랄 {med([c[0] for c in cost_vals[u]]):5d}")

    data = {
        "_about": "유즈맵이 유닛 능력치(UNIS·UNIx)를 어떻게 고치는지 전수 "
                  "실측. measure_unitdefs.py 가 만든다. touched 는 그 유닛을 "
                  "고친 맵 수, hp_median 은 고친 맵들의 체력 중앙값.",
        "_n_maps": n_map,
        "_n_customized": n_custom,
        "_types_median": med(per_map_count),
        "_field_hits": dict(field_hit),
        "units": {str(u): {"name": names.get(u, str(u)), "maps": n,
                           "hp_median": med(hp_vals[u]),
                           "minerals_median": med([c[0] for c in cost_vals[u]]),
                           "gas_median": med([c[1] for c in cost_vals[u]])}
                  for u, n in touched.most_common(80)},
    }
    path = os.path.normpath(OUT)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"\n썼습니다: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
