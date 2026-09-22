# 점수 — 리더보드가 세는 것

출처: 스타 에디터 아카데미
[[팁] 유닛별 킬 스코어 목록](https://cafe.naver.com/edac/book5095361/76475)
+ 게임 데이터 실측.

실측 유즈맵의 **82%가 리더보드**를 쓴다. 그 가운데 `Leaderboard
(Points)` 는 **점수**로 줄을 세우는데, 그 점수가 어디서 나오는지 알아야
"왜 저 사람이 1등이지" 를 설계할 수 있다.

## 리더보드로 안 보여 줘도 점수는 쌓인다

`Score` 조건과 `Set Score` 액션이 그 값을 읽고 쓴다.

| `points` 갈래 | 무엇을 세나 |
| --- | --- |
| `kills` | 죽인 **유닛** 점수 |
| `razings` | 부순 **건물** 점수 |
| `kills and razings` | 둘 다 |
| `units` | 가진 유닛 점수 |
| `buildings` | 가진 건물 점수 |
| `units and buildings` | 둘 다 |
| `custom` | **트리거로만 쓰는 값** |

**`custom` 이 유즈맵의 기본**이다. 게임이 주는 점수와 섞이지 않는다.

## 만들 때와 부술 때가 다르다

`units.dat` 에 **`buildScore`** 와 **`destroyScore`** 가 따로 있다.
**171가지가 서로 다르고, 대개 부술 때가 두 배다.**

| 유닛 | 가지고 있으면 | 부수면 |
| --- | ---: | ---: |
| Terran Marine | 50 | **100** |
| Terran Siege Tank (Tank) | 350 | **700** |
| Terran Science Vessel | 625 | **1250** |
| Gui Montag (Firebat) | **0** | 400 |

> **영웅은 `buildScore` 가 0 인 것이 있다.** 가지고 있어도 `units` 점수가
> 안 오른다. "유닛 점수" 로 순위를 매기는 맵에서 영웅을 주면 **순위에
> 안 잡힌다.**

## 값이 큰 것

| 유닛 | 부수면 |
| --- | ---: |
| 배틀크루저 영웅 넷 (히페리온 · 노라드II · 멩스크 · 듀갈) | **4800** |
| Danimoth (Arbiter) | 4100 |
| Infested Kerrigan (Infested Terran) | 4000 |
| Gantrithor (Carrier) | 3800 |
| Terran Battlecruiser | 2400 |

| 건물 | 부수면 |
| --- | ---: |
| **Zerg Overmind** | **10000** |
| Xel'Naga Temple · Protoss Temple · Ion Cannon · Mature Crysalis | 5000 |

## 카페 표와 대조했다

카페가 손으로 적어 둔 테란 유닛 14가지를 게임 데이터와 견줬다 —
**14개 모두 맞는다.** 그러니 표를 따로 옮겨 적지 않는다. 필요할 때
데이터에서 읽는다.

```sh
splash-cli unit-stats "$SC_INSTALL" --json   # build_score · destroy_score
```

## 맵이 점수를 바꿀 수 있나

**없다.** `unitdef set` 에 점수 칸이 없다. 게임 데이터에 박혀 있어
DatEdit 쪽 이야기다 ([../tools/editors.md](../tools/editors.md)).

점수로 순위를 매기려면 **`custom` 점수를 트리거로 직접 쌓는 편**이
낫다 — 유닛마다 얼마인지 외울 필요가 없고 설계한 대로 나온다.

## 관련

- [../trigger/actions.md](../trigger/actions.md) — `Leaderboard` · `Set Score`
- [../trigger/conditions.md](../trigger/conditions.md) — `Score` 조건
- [settings.md](settings.md) — 맵이 고칠 수 있는 것
