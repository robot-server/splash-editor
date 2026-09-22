# 유닛 설정 — 유즈맵의 98%가 고친다

`unitdef set` (CHK 의 `UNIS`·`UNIx`) 은 **맵마다 유닛 능력치를 새로
정하는 자리**다. 내 맵은 여태 하나도 안 고쳤다. 실측은 반대다.

실측: 유즈맵 **479장** 전수. `measure_unitdefs.py` ·
[data/unitdefs.json](../../.agents/skills/starcraft-map/data/unitdefs.json).

| | 값 |
| --- | --- |
| 유닛 설정을 **고치는 맵** | **473장 / 479장 = 98%** |
| 고치는 유닛 **종류 수** | 중앙 **90종** |
| 가장 많이 건드리는 칸 | 체력 > 생산시간 > 미네랄 > 방어력 > 가스 > 방패 |

> 유즈맵은 "스타크래프트 유닛으로 노는 맵" 이 아니다. **유닛을 다시
> 정의한 맵**이다. 마린 체력 40 을 그대로 두고 만든 유즈맵은 거의 없다.

## 가장 자주 고치는 유닛

중앙값이지 정답은 아니다. 값의 크기보다 **무엇을 고치는지**를 본다.

| 유닛 | 고친 맵 | 체력 중앙 | 미네랄 중앙 |
| --- | ---: | ---: | ---: |
| Terran Marine | 417 | 250 | 50 |
| Terran Ghost | 403 | 400 | 25 |
| Zerg Hydralisk | 401 | 200 | 75 |
| Zerg Zergling | 396 | 120 | 50 |
| Jim Raynor (Marine) | 390 | 1112 | 50 |
| Terran Firebat | 390 | 420 | 50 |
| Protoss Zealot | 370 | 275 | 100 |
| Terran Goliath | 369 | 700 | 100 |
| Protoss Dragoon | 368 | 400 | 125 |
| Terran Siege Tank (Tank) | 367 | 699 | 150 |
| Hunter Killer (Hydralisk) | 367 | 800 | 150 |
| Terran Civilian | 366 | 40 | 0 |

마린 체력이 **원래 40인데 중앙값 250** 이다. 여섯 배다. 유즈맵은
유닛을 그대로 쓰지 않는다.

## 고칠 수 있는 다섯 가지뿐

```sh
splash-cli unitdef set 맵 "Terran Marine" \
    --default off --hp 250 --minerals 50 --build-time 120 --in-place
```

| 칸 | 뜻 |
| --- | --- |
| `--hp` | 체력. **표시값**으로 적는다 (안에서 ×256 된다) |
| `--shields` | 방패 |
| `--armor` | 방어력 |
| `--build-time` | 생산 시간 (1/15초) |
| `--minerals` · `--gas` | 값 |

**`--default off` 를 빼면 아무 일도 안 일어난다.** "기본값 따름" 칸이
켜져 있으면 나머지 값을 적어도 게임이 안 본다.

**여기 없는 것은 맵이 못 고친다** — 사거리·공격 주기·시야·이동 속도.
그건 유닛을 **다른 번호로 바꿔서** 푸는 수밖에 없다.
→ [heroes.md](heroes.md)

## 고를 때 묻는 것

1. 이 맵에서 플레이어가 **오래 살아야 하는가**? → 체력
2. **자주 사야 하는가**? → 미네랄·생산시간을 낮춘다
3. **단단함이 중요한가**? → 방어력 (피해 감산이라 체력보다 세게 먹는다)
4. 사거리·속도가 중요한가? → **여기서는 못 한다.** 유닛을 바꾼다

## 관련

- [heroes.md](heroes.md) — 맵이 못 고치는 값과 영웅 유닛
- [../chk/sections.md](../chk/sections.md) — UNIS·UNIx 의 자리 배치
- [../usemap/dopamine.md](../usemap/dopamine.md) — 유즈맵 재미 구조
