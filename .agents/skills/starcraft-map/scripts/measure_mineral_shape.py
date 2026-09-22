"""공식 밀리맵 본진의 **미네랄 배치 모양**을 잰다.

카페 `에디터의 모/든/것` 의 실험에 따르면 1자형 486 · 꽈배기 520 ·
퍼트리기 520 · **L자형 552** 로 채취율이 13.6% 벌어진다. 내 표는
183곳을 평균 내 만든 것이라 **모양이 뭉개졌을 수 있다** — 맵마다
미네랄 줄의 방향이 달라 평균을 내면 줄로 모인다.

그래서 평균이 아니라 **본진마다 모양을 따로** 본다.
"""
import os, sys, math, collections, subprocess, re
sys.path.insert(0, ".agents/skills/starcraft-map/scripts")
import scmap

D = sys.argv[1]
cli_path = scmap.find_cli()
shapes = collections.Counter()
samples = []
files = [f for f in sorted(os.listdir(D)) if f.lower().endswith((".scx", ".scm"))]
for fn in files[:120]:
    p = os.path.join(D, fn)
    r = subprocess.run([cli_path, "unit", "list", p, "--limit", "4000"],
                       capture_output=True, text=True)
    if r.returncode:
        continue
    starts, mins = [], []
    for line in r.stdout.splitlines():
        m = re.match(r"\s*\d+\s+(\d+),\s*(\d+)\s+P\s*\d+\s+(.+?)\s*\(\d+\)", line)
        if not m:
            continue
        x, y, name = int(m.group(1)), int(m.group(2)), m.group(3).strip()
        if "Start Location" in name:
            starts.append((x, y))
        elif "Mineral Field" in name:
            mins.append((x, y))
    for sx, sy in starts:
        near = [(x - sx, y - sy) for x, y in mins
                if abs(x - sx) < 400 and abs(y - sy) < 400]
        if len(near) < 6:
            continue
        # 주성분: 좌표의 퍼짐이 한 축에 몰리면 줄, 두 축에 퍼지면 L/덩이
        xs = [a for a, _ in near]; ys = [b for _, b in near]
        sx_ = max(xs) - min(xs); sy_ = max(ys) - min(ys)
        long_, short = max(sx_, sy_), min(sx_, sy_)
        ratio = short / long_ if long_ else 0
        if ratio < 0.35:
            shapes["1자형(줄)"] += 1
        elif ratio < 0.75:
            shapes["L자형"] += 1
        else:
            shapes["덩이·퍼트리기"] += 1
        samples.append((fn, len(near), round(ratio, 2)))

print(f"본진 {sum(shapes.values())}곳 ({len(files[:120])}장에서)")
for k, v in shapes.most_common():
    print(f"  {k:16s} {v:4d}곳  {100*v//max(sum(shapes.values()),1)}%")
