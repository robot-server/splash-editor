# 영웅 유닛 — **"같은 유닛의 센 판" 이 아니다**

유즈맵에서 같은 모양의 유닛을 일반·영웅 둘 다 쓸 때,
**"센 건 영웅, 약한 건 일반"** 으로 가르면 틀린다. 특성이 다르다.

실측: `measure_heroes.py` 가 게임 데이터(`units.dat`·`weapons.dat`·
`flingy.dat`)에서 **같은 겉모습(flingy)** 을 쓰는 영웅·일반 **35짝**을
짝지어 잰 것. 표는 [data/heroes.json](../../.agents/skills/starcraft-map/data/heroes.json).

```sh
splash-cli unit-stats "$SC_INSTALL"            # 사람이 읽는 표
splash-cli unit-stats "$SC_INSTALL" --json     # 기계가 읽는 표
python3 $S/measure_heroes.py                   # 영웅 ↔ 일반 짝 비교
```

## 맵이 고칠 수 있는 것과 없는 것

여기가 갈림길이다. 맵의 유닛 설정(`unitdef set`, CHK 의 UNIS·UNIx)이
건드리는 것은 **다섯 가지뿐**이다.

| 맵이 고칠 수 있다 | 맵이 **못** 고친다 |
| --- | --- |
| 체력 | **사거리** |
| 방패 | **공격 주기(쿨다운)** |
| 방어력 | **시야 · 먼저 무는 거리** |
| 생산 시간 | **이동 속도** |
| 미네랄 · 가스 값 | 덩치(소·중·대형), 스플래시, 특수 능력 |

그러니 **체력이 세서 영웅을 고르는 것은 헛수고다** — 그건 일반 유닛에
`unitdef set --hp` 로 주면 된다. 영웅을 고르는 까닭은 오른쪽 칸에
있어야 한다.

## 35짝에서 실제로 나온 것

| | 영웅이 일반과 |
| --- | --- |
| 이동 속도 | **35짝 전부 같다** |
| 지상 사거리 | 28짝 같다 (7짝만 다르다) |
| 시야 | 26짝 같다 |
| 공격 주기 | **4짝만** 영웅이 빠르다 |
| 공격력 업그레이드 번호 | **33짝 같다 — 영웅도 공격력 업이 먹는다** |

> **영웅이 더 빠른 일은 없다.** 겉모습이 같으면 `flingy.dat` 을 같이
> 쓰므로 기본 이동 속도가 같다. 벌쳐 영웅 짐 레이너가 일반 벌쳐보다
> 느려 보이는 것은 일반 벌쳐만 **이온 추진기 업그레이드**를 받기
> 때문이다 — 업그레이드는 유닛 **번호**에 붙는데 영웅은 다른 번호다.

## 영웅이 오히려 **나쁜** 자리

이것이 "센 건 영웅" 이 틀린 증거다.

| 영웅 | 일반보다 나쁜 값 |
| --- | --- |
| Samir Duran (Ghost) | 사거리 **7 → 6타일** (공중도) |
| Alexei Stukov (Ghost) | 사거리 **7 → 6타일** |
| Infested Duran | 사거리 **7 → 6타일** |
| Raszagal (Corsair) | 방패 **80 → 60**, 방어력 **1 → 0** |
| Gantrithor (Carrier) | 시야 **11 → 9** |
| Arcturus Mengsk (Battlecruiser) | 시야 **11 → 8** |

고스트 영웅 셋은 **사거리가 한 칸 짧다.** 저격 맵에서 "고스트 영웅이
세니까" 로 골랐다면 사거리에서 지는 유닛을 놓은 셈이다.

## 영웅이 실제로 좋은 자리

| 영웅 | 일반보다 나은 값 | 쓸 자리 |
| --- | --- | --- |
| Jim Raynor (Marine) | 사거리 **4 → 5타일** | 사거리가 중요한 방어 |
| Hunter Killer (Hydralisk) | 사거리 **4 → 5타일** | 같다 |
| Tassadar · Aldaris (Templar) | 사거리 **0 → 3타일** — 원거리 공격이 **생긴다** | 아예 다른 유닛이다 |
| Fenix (Dragoon) | 공격 주기 **30 → 22** (36% 빠름) | 화력 |
| Jim Raynor (Vulture) | 공격 주기 **30 → 22**, 피해 20→30 | 화력 |

> 하이 템플러 영웅(타사다르·알다리스)은 **일반 하이 템플러에 없는
> 3칸 사거리 공격**이 있다. 사거리 0 은 "때리지 못한다" 는 뜻이다 —
> 이건 세고 약함이 아니라 **다른 유닛**이다.

## 사거리 업그레이드는 데이터에 없다

`weapons.dat` 의 `maximumRange` 는 **기본 사거리**다. 드라군의
시경(Singularity Charge) 처럼 사거리를 올리는 업그레이드는 dat 이 아니라
게임 실행 파일에 박혀 있어 **여기서 잴 수 없다.**

- 잰 것: 페닉스(드라군)와 일반 드라군의 **기본 사거리는 128픽셀로 같다.**
- 밖의 자료는 엇갈린다. [나무위키](https://namu.wiki/w/%ED%94%BC%EB%8B%89%EC%8A%A4(%EC%8A%A4%ED%83%80%ED%81%AC%EB%9E%98%ED%94%84%ED%8A%B8%20%EC%8B%9C%EB%A6%AC%EC%A6%88))
  는 브루드워에서 페닉스가 6이라 하고, 오리지널에서는 5라고 적는다.
  [StrategyWiki](https://strategywiki.org/wiki/StarCraft/Hero_Units) 는
  영웅이 "대개 모든 업그레이드를 갖고 나온다" 고 쓴다.
- **아직 확실히 못 정했다.** 게임을 돌려 재기 전에는 단정하지 않는다.
  사거리가 걸린 맵을 만들 때는 두 유닛을 나란히 놓고 직접 보는 편이 낫다.

마린은 확실하다 — 짐 레이너(마린)는 **기본 5타일**이고 U-238 을 받지
않는다. 업그레이드를 마친 일반 마린도 5타일이므로 **둘이 같아진다**
([Liquipedia](https://liquipedia.net/starcraft/Jim_Raynor_(Marine))).

## 컴퓨터가 굴리는 영웅

`units.dat` 의 AI 스크립트 칸(`compAIIdle`·`returntoIdle`·`attackUnit`·
`attackMove`)은 **영웅과 일반이 같다** — 잰 35짝 모두 `2/2/10/2` 다.
그러니 "영웅은 끝까지 쫓아간다" 를 이 칸으로는 설명할 수 없다.

쫓아가는 거리를 정하는 것은 units.dat 의 **`targetAcquisitionRange`
(먼저 무는 거리)** 와 게임 안의 복귀 규칙이다. 실측에서 이 값이 다른
영웅이 있으므로 [data/heroes.json](../../.agents/skills/starcraft-map/data/heroes.json)
의 `target_range` 를 보고 고른다.

> **아직 안 잰 것.** 컴퓨터가 쥔 영웅이 끝까지 쫓아가는지는 게임을
> 돌려 봐야 안다. 데이터만으로는 못 가른다 — 여기에 적어 두고, 잰
> 뒤에 고친다.

## 고를 때 묻는 것

1. 이 자리에서 **못 고치는 값** 중 무엇이 중요한가? (사거리·주기·시야·속도)
2. 그 값이 영웅과 일반 중 **어느 쪽이 나은가**? → `heroes.json` 을 본다
3. 체력·방어력만 필요한가? → **일반 유닛에 `unitdef set` 을 쓴다**
4. 공격력 업그레이드를 팔 맵인가? → 영웅도 받는다 (33/35짝)

## 관련

- [../chk/sections.md](../chk/sections.md) — UNIS·UNIx 가 담는 것
- [../usemap/essentials.md](../usemap/essentials.md) — 유즈맵 필수품
- [../tools/cli-cookbook.md](../tools/cli-cookbook.md) — `unit-stats` 쓰기
