# 두뎃 — 갈래가 지형 종류 이름과 같다

**어떻게 쟀나.** 갈래·크기는 `doodad list --catalogue` 가 설치본에서
그대로 읽어 준다. "걷기를 막는가" 는 `measure_doodads.py` 가 빈 맵에
전부 놓아 보고 덮인 칸의 걷기 비트를 확인해 `data/doodad-walk.json`
에 남긴다.

## 어느 지형에 놓나 — 갈래를 보면 된다

두뎃 갈래 이름은 [지형 종류 이름](terrain-types.md)과 **정확히 같다**.

```
Dirt · High Dirt · Grass · High Grass · Rocky Ground · Asphalt · Water
Snow · High Snow · Ice · Outpost · Sand Dunes · Compound · Basilica …
```

그래서 **High Dirt 바닥에는 갈래가 "High Dirt" 인 두뎃을 놓는다.**
Dirt 두뎃을 고지대에 올리면 안 어울린다 — 실제로 그렇게 만들어
지적받았다.

```python
gname = scmap.group_terrain_name(ts)        # 타일 그룹 → 지형 이름
kind  = gname.get(tile_value >> 4)          # 이 자리의 지형 이름
pool  = [d for d in cli.doodad_catalogue() if d["kind"] == kind]
```

`scmap.decorate_rim()` 이 이 방식으로 고른다.

더 엄밀한 표도 게임에 있다 — `dddata.bin` 의 `DoodadPlacibility` 는
두뎃 칸마다 **놓을 수 있는 타일 그룹**을 적어 둔다 (0 이면 아무 데나).
`doodad place` 가 그 표를 본다. 갈래 이름으로 고르는 것은 그 표의
사람이 읽을 수 있는 요약이다.

## 타일셋마다 몇 종인가

| 타일셋 | 두뎃 종 | 걷기를 안 막는 것 | 갈래 |
| --- | --- | --- | --- |
| badlands | 284 | 188 | Dirt 60, Structure Wall 50, Cliff 46, High Dirt 38, Grass 21, High Grass 21 |
| space | 284 | 186 | Platform Wall 40, Platform 37, Elevated Catwalk Ramps 34, Low Platform Wall 34, Rusty Pit Wall 34, High Plating 29 |
| installation | 64 | 26 | Substructure 22, Wall 20, Substructure Wall 16, Plating 4, Floor 2 |
| ashworld | 134 | 93 | Dirt 45, High Dirt 45, Cliff 38, Shale 6 |
| jungle | 278 | 166 | Dirt 60, Cliff 46, High Dirt 38, High Temple Wall 34, Temple Wall 34, High Ruins 12 |
| desert | 364 | 235 | Cliff 46, High Sand Dunes 41, High Sandy Sunken Pit 41, Sand Dunes 41, Sandy Sunken Pit 41, Compound 39 |
| ice | 296 | 175 | Cliff 44, Snow 42, High Outpost 40, Outpost 40, Ice 38, High Snow 28 |
| twilight | 327 | 185 | Cliff 50, Dirt 43, Basilica 39, High Basilica 39, High Dirt 38, High Sunken Ground 34 |

## 걷기를 막는 두뎃이 절반 가까이 된다

바위·잔해는 밟고 지나갈 수 없다. 방 안에 아무거나 뿌리면 **동선이
끊긴다.** `measure_doodads.py` 가 미리 재 두었으니 `walk` 가 참인 것만
쓴다.

```python
safe = scmap.doodad_walk_table(ts)          # 번호 → {w,h,kind,walk,tiles}
pool = [d for d in cat if safe.get(d["id"], {}).get("walk")]
```

놓고 나서 길찾기로 확인하고 되돌리는 식으로 해 봤는데, 밀도를 못 올려
실측의 15분의 1(48칸 대 중앙 868칸)에서 멈추고 맵 하나에 1분이 넘게
걸렸다. **미리 재 두는 쪽이 맞다.**

## 기준점은 **가운데**다

`doodad place <x> <y>` 의 `(x, y)` 는 왼위가 아니라 **한가운데**다.
6x6 두뎃을 (20, 20) 에 놓으면 **(17,17)~(22,22)** 를 덮는다. 찍어 보고
확인했다.

```
두뎃 78 (6x6) 을 (20,20) 에 놓고 (16,16) 부터 읽은 그림
   ..............
   .000000.......      ← (17,17) 에서 시작한다
   .000000.......
   .0000RR.......
   .000RRR.......      ← (20,20) 은 여기, 한가운데
   .000RR0.......
   .000000.......
```

**앞서 이걸 왼위로 알고 썼다.** 모든 두뎃이 제 크기의 절반만큼 밀려
놓여 방 테두리를 뚫거나 벽 위에 얹혔다. 방 안으로 두 칸 물렸는데도
구멍이 나던 까닭이 이것이다.

```python
scmap.place_doodad(cli, did, left, top, w, h)   # 왼위로 준다
scmap.doodad_anchor(left, top, w, h)            # 왼위 → 가운데
scmap.doodad_topleft(cx, cy, w, h)              # 가운데 → 왼위
```

또 하나: **선언된 크기와 실제로 쓰는 칸이 다르다.** 6x6 이라고 적혀
있어도 3x3 만 쓰는 것이 있다. 자리를 잡을 때는 선언 크기로 잡되,
겹침을 볼 때도 선언 크기로 본다.

## 놓을 때 지키는 것 — 셋 다 실제로 틀려 봤다

| 지킬 것 | 안 지키면 |
| --- | --- |
| 방 테두리에서 **두 칸 안으로** 물린다 | 두뎃이 벽을 뚫어 방에 구멍이 난다. 두뎃은 밑에 무엇이 있든 제 타일을 쓴다 |
| 두뎃끼리 **겹치지 않게** | 앞 두뎃이 반만 덮여 조각이 남고, 그 조각이 못 걷는 타일이라 길이 막힌다 |
| 그 자리 **지형에 맞는 갈래**만 | 흙 두뎃이 고지대에 올라가 안 어울린다 |

## DD2 와 MTXM — 두뎃은 두 군데에 있다

에디터에서 놓은 두뎃은 저장할 때 **지형(MTXM)으로 눌러 담긴다.**
그래서 실제 맵은 DD2 항목이 비어 있어도 타일에는 두뎃이 남아 있다.

| 자 | 실측 유즈맵 (상위 76장) |
| --- | --- |
| DD2 항목 수 | 중앙 **0개** |
| MTXM 두뎃 타일 (그룹 ≥ 1024) | 중앙 **868칸**, 사분위 398~3640, **92%가 씀** |
| 그중 걷기 경계 두 칸 안 | 중앙 **89%** (아무 칸이나면 54%) |

**DD2 만 세면 "유즈맵은 두뎃을 안 쓴다" 는 틀린 결론이 나온다.**
실제로 그렇게 적어 두었다가 그림을 보고서야 알았다.

만든 뒤 `doodad to-terrain` 으로 항목을 지우고 지형만 남기면 실제 맵과
같은 꼴이 된다.
