# 영웅 유닛 — **"같은 유닛의 센 판" 이 아니다**

유즈맵에서 같은 모양의 유닛을 일반·영웅 둘 다 쓸 때,
**"센 건 영웅, 약한 건 일반"** 으로 가르면 틀린다. 특성이 다르다.

실측: `measure_heroes.py` 가 게임 데이터(`units.dat`·`weapons.dat`·
`flingy.dat`)에서 영웅·일반 **37짝**을 짝지어 잰 것. 표는 [data/heroes.json](../../.agents/skills/starcraft-map/data/heroes.json).

짝은 **두 갈래로** 찾는다. 처음에는 겉모습(`flingy`)만 봤는데
**사라 케리건이 빠졌다** — 케리건은 생김새가 달라 flingy 가 77 이고
일반 고스트는 74 다. 그래서 이름의 괄호도 본다
("Sarah Kerrigan (Ghost)" → Terran Ghost).

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

## 37짝에서 실제로 나온 것

| | 영웅이 일반과 |
| --- | --- |
| 이동 속도 | **37짝 전부 같다** |
| 지상 사거리 | 28짝 같다 (7짝만 다르다) |
| 시야 | 26짝 같다 |
| 공격 주기 | **4짝만** 영웅이 빠르다 |
| 공격력 업그레이드 번호 | **34짝 같다 — 영웅도 공격력 업이 먹는다** |

> **기본 이동 속도가 영웅이라고 빠른 일은 없다.** 겉모습이 같으면
> `flingy.dat` 을 같이 쓰므로 기본 속도가 같다 — 35짝 모두 그렇다.

영웅은 대개 **업그레이드를 마친 값이 처음부터 박혀** 나온다. 그런데
**짐 레이너(벌쳐)는 이동 속도만은 그렇지 않다** — 일반 벌쳐와 같은
기본 속도로 시작한다. 그리고 **이온 추진기를 올리면 둘 다 빨라진다.**

그러니 "영웅이라 이미 업이 되어 있다" 도, "영웅은 업을 못 받는다" 도
통째로는 틀린 말이다. **유닛마다 다르다.**

## 영웅이 오히려 **나쁜** 자리

이것이 "센 건 영웅" 이 틀린 증거다.

| 영웅 | 일반보다 나쁜 값 |
| --- | --- |
| **Sarah Kerrigan (Ghost)** | 사거리 **7 → 6타일** (공중도) |
| Samir Duran (Ghost) | 사거리 **7 → 6타일** (공중도) |
| Alexei Stukov (Ghost) | 사거리 **7 → 6타일** |
| Infested Duran | 사거리 **7 → 6타일** |
| Raszagal (Corsair) | 방패 **80 → 60**, 방어력 **1 → 0** |
| Gantrithor (Carrier) | 시야 **11 → 9** |
| Arcturus Mengsk (Battlecruiser) | 시야 **11 → 8** |

> 라자갈이 일반 커세어보다 **약한** 것은 우연이 아니다. 라자갈은
> 이야기에서 **다크 템플러**다 — 인게임 유닛만 커세어일 뿐 값은 그쪽
> 결로 잡혀 있다. 커세어 영웅으로 쓸 수는 있지만, **일반 커세어보다
> 단단할 것으로 기대하면 안 된다.**

고스트 영웅 **넷 모두 사거리가 한 칸 짧다.** 사라 케리건도 그렇다. 저격 맵에서 "고스트 영웅이
세니까" 로 골랐다면 사거리에서 지는 유닛을 놓은 셈이다.

## 영웅이 실제로 좋은 자리

| 영웅 | 일반보다 나은 값 | 쓸 자리 |
| --- | --- | --- |
| Jim Raynor (Marine) | 사거리 **4 → 5타일** | 사거리가 중요한 방어 |
| Hunter Killer (Hydralisk) | 사거리 **4 → 5타일** | 같다 |
| Tassadar · Aldaris (Templar) | 사거리 **0 → 3타일** — 원거리 공격이 **생긴다** | 아예 다른 유닛이다 |
| Fenix (Dragoon) | 공격 주기 **30 → 22** (36% 빠름) | 화력 |
| Jim Raynor (Vulture) | 공격 주기 **30 → 22**, 피해 20→30 | 화력 |

> 하이 템플러 영웅(타사다르(템플러)·알다리스(템플러))은 **일반 하이 템플러에 없는
> 3칸 사거리 공격**이 있다. 사거리 0 은 "때리지 못한다" 는 뜻이다 —
> 이건 세고 약함이 아니라 **다른 유닛**이다.

## 때리는 값이 그 유닛에 없을 때가 있다

이걸 모르면 비교가 통째로 틀린다. 실제로 틀렸다 — 앨런 셰자르(골리앗)를
"체력만 늘었다" 로 읽고 있었는데 공격력이 두 배였다.

| 어디에 있나 | 누가 | 무슨 뜻인가 |
| --- | --- | --- |
| 제 무기 | 대부분 | 그대로 본다 |
| **아랫유닛(포탑)** | 골리앗 · 시즈탱크와 그 영웅 | **몸통과 머리가 따로다.** 무기는 머리에 있다 |
| **따로 나오는 유닛** | 캐리어 · 리버와 그 영웅 | 인터셉터 · 스캐럽이 때린다 |

### 몸통과 머리가 따로인 유닛

| 유닛 | 포탑 | 포탑 지상 피해 |
| --- | --- | ---: |
| Terran Goliath | Goliath Turret | 12 |
| **Alan Schezar (Goliath)** | Alan Schezar Turret | **24** |
| Terran Siege Tank (Tank) | Siege Tank Turret | 30 |
| **Edmund Duke (Tank)** | Edmund Duke Turret | **70** |

`unitdef set` 으로 골리앗 공격력을 바꾸려면 **포탑 쪽을 고쳐야** 한다.
몸통에는 무기가 없다.

### 캐리어와 리버는 영웅이라도 공격력이 같다

| 유닛 | 제 무기 | 때리는 것 | 피해 |
| --- | --- | --- | ---: |
| Protoss Carrier | **없음** | Protoss Interceptor | 6 |
| **Gantrithor (Carrier)** | **없음** | Protoss Interceptor | **6 (같다)** |
| Protoss Reaver | **없음** | Protoss Scarab | 100 |
| **Warbringer (Reaver)** | **없음** | Protoss Scarab | **100 (같다)** |

인터셉터와 스캐럽은 **영웅판이 아예 없다.** 그래서 간트리서는 일반
캐리어와 화력이 똑같다 — 다른 것은 체력 300→800, 방패 150→500,
그리고 시야는 오히려 11→9 로 **짧다.**

## 무기를 나눠 쓰는 유닛

`weapons.dat` 한 칸을 둘 이상이 쓰면 **한쪽만 고칠 수 없다.**

| 무기 | 쓰는 유닛 |
| --- | --- |
| 69 | **Tassadar (Templar) · Aldaris (Templar)** |
| 21 | Arcturus Mengsk · Norad II · Gerard DuGalle (배틀크루저) |
| 97 · 98 | Left/Right Wall Missile Trap · Flame Trap |

타사다르(템플러)와 알다리스(템플러)는 **공격력을 공유한다.** 둘을 서로 다른 세기로
쓰려던 계획은 처음부터 안 된다.

### 배틀크루저 영웅 넷

| 유닛 | 무기 | 지상 피해 | 체력 | 시야 |
| --- | ---: | ---: | ---: | ---: |
| Terran Battlecruiser | 19/20 | 25 | 500 | 11 |
| **Arcturus Mengsk** | **21/22** | 50 | 1000 | **8** |
| **Norad II** | **21/22** | 50 | 700 | 11 |
| **Gerard DuGalle** | **21/22** | 50 | 700 | 11 |
| Hyperion | 23/24 | **30** | 850 | 11 |

멩스크 · 노라드2(배틀크루저) · 듀갈 **셋이 한 무기를 나눠 쓴다.** 히페리온만 제
무기를 갖고 있는데, 정작 피해는 **30 으로 셋(50)보다 낮다.**
그리고 멩스크만 시야가 **8** 이다 — 혼자 세 칸 짧다.

영웅 이름값으로 고르면 여기서 어긋난다.

## 능력치 말고도 다른 것

### 이름을 줄여 쓰면 다른 유닛이 된다

"듀란" 이라고 쓰면 어느 쪽인지 알 수 없다. 겹치는 이름이 **아홉
갈래**다. 글에도 코드에도 **전체 이름을 쓴다.**

| 줄인 이름 | 실제로는 |
| --- | --- |
| 듀란 | Samir Duran (Ghost) · **Infested Duran** |
| 케리건 | Sarah Kerrigan (Ghost) · **Infested Kerrigan (Infested Terran)** |
| 제라툴 | Zeratul (Dark Templar) · **Tassadar/Zeratul (Archon)** |
| 타사다르 | Tassadar (Templar) · **Tassadar/Zeratul (Archon)** |
| 페닉스 | Fenix (Zealot) · **Fenix (Dragoon)** |
| 짐 레이너 | Jim Raynor (Marine) · **Jim Raynor (Vulture)** |
| 쿠쿨자 | Kukulza (Mutalisk) · **Kukulza (Guardian)** |
| 노라드2 | Norad II (Battlecruiser) · **Norad II (Crashed)** — 뒤엣것은 무기가 없다 |
| 다크 템플러 | Protoss Dark Templar · Protoss Dark Templar (Hero) · Zeratul (Dark Templar) |
| 앨런 셰자르 | Alan Schezar (Goliath) · **Alan Schezar Turret** — 무기는 포탑에 |
| 에드먼드 듀크 | 탱크 모드/시즈 모드 × 몸통/포탑 = **넷** |

같은 이름인데 성질이 정반대인 것이 많다. 사미르 듀란과 감염된 듀란은
업그레이드 종족부터 다르고, 페닉스는 질럿 판과 드라군 판이 사거리부터
다르다.

### 감염된 듀란은 업그레이드가 두 종족에 걸쳐 있다

| 유닛 | 공격력 업그레이드 | 방어력 업그레이드 |
| --- | --- | --- |
| Terran Ghost · 사라 케리건 · **사미르 듀란** · 스투코프 | Terran Infantry Weapons | Terran Infantry Armor |
| **Infested Duran** | **Terran Infantry Weapons** | **Zerg Carapace** |
| Infested Kerrigan | Zerg Melee Attacks | Zerg Carapace |

**감염된 듀란만 갈라져 있다.** 공격력은 테란 업그레이드를 따르고
방어력은 저그 업그레이드를 따른다. 유즈맵에서 업그레이드를 파는
구조라면, 이 유닛은 **두 종족 건물을 다 지어야** 온전히 강해진다.

`splash-cli unit-stats --json` 의 `ground_dmg_upgrade` · `armor_upgrade`
로 본다. `splash-cli upgrade list` 가 번호를 이름으로 풀어 준다.

### 데이터로는 못 재는 규칙

게임에 박힌 것이라 dat 에 없다. 아는 것:

| | |
| --- | --- |
| **핵 발사** | **일반 고스트만 된다.** 영웅 고스트(사라 케리건 · 사미르 듀란 · 스투코프)는 못 쏜다 |
| **벙커 탑승** | 감염된 듀란은 **못 들어간다** |
| **변환(모프)** | 영웅은 안 된다 (아래) |

핵을 쓰는 유즈맵이라면 고스트 영웅을 놓으면 안 된다. 세 보인다고
바꿔 놓으면 핵이 발사되지 않는다.



### 이그드라실은 인구수를 30 채운다

| 유닛 | 채우는 인구수 |
| --- | ---: |
| Zerg Overlord | 8 |
| **Yggdrasill (Overlord)** | **30** |

거의 네 배다. 유즈맵에서 인구수를 풀어 주고 싶을 때 오버로드를 여럿
놓는 대신 이그드라실 하나를 놓으면 된다. `units.dat` 의
`supplyProvided` 이고, **맵이 못 고치는 값**이다.

`splash-cli unit-stats --json` 의 `supply_provided` 로 본다.

## 영웅판이 **아예 없는** 유닛

"영웅으로 바꿔 쓰면 되지" 가 안 통하는 자리다. 16기 있다.

| | |
| --- | --- |
| **브루드워에서 더해진 유닛** | Terran Medic · Terran Valkyrie · Zerg Lurker · Zerg Devourer · Protoss Dark Archon |
| 일꾼·수송 | Terran SCV · Zerg Drone · Protoss Probe · Terran Dropship · Protoss Shuttle · Protoss Observer |
| 그 밖 | Terran Civilian · Spider Mine · Zerg Broodling · Zerg Scourge |

브루드워에서 새로 들어온 유닛은 **영웅이 만들어지지 않았다.** 메딕·
럴커·발키리·다크 아콘을 세게 쓰고 싶으면 `unitdef set` 으로 체력·
방어력을 올리는 수밖에 없다 — 사거리·주기는 못 고친다.

### 다크 템플러는 영웅이 하나인데 일반이 둘이다

목록에 `Protoss Dark Templar` 가 "영웅 없음" 으로 뜨는데, 없는 것이
아니라 **일반 쪽이 둘**이라 짝이 어긋난 것이다.

| 번호 | 이름 | 무엇 |
| --- | --- | --- |
| 61 | Protoss Dark Templar | **브루드워**에서 정상으로 생산하는 것 |
| 74 | Protoss Dark Templar (Hero) | **오리지널** 것. 생산이 안 된다 |
| 75 | Zeratul (Dark Templar) | 영웅. 74번과 **같은 겉모습** |

겉모습(flingy)이 61번은 188, 74·75번은 46 이다. 그래서 **제라툴(다크 템플러)을
쓰면 일반 다크 템플러(61번)와 모양이 다르게** 나온다. 나란히 놓을
생각이면 74번을 일반으로 써야 모양이 맞는다.

74번은 이름에 (Hero) 가 붙어 있는데도 units.dat 의 영웅 깃발이 꺼져
있다 — **이름을 믿지 말고 깃발을 본다.**

## 영웅은 변환(모프)이 안 된다

게임에 박힌 규칙이라 dat 으로는 못 잰다. 아는 것:

| 영웅 | 못 하는 것 |
| --- | --- |
| Tassadar (Templar) · Aldaris (Templar) | 아콘 변환 |
| Zeratul (Dark Templar) | 다크 아콘 변환 |
| Hunter Killer (Hydralisk) | 럴커 변환 |
| Kukulza (Mutalisk) | 가디언 변환 |

**그래도 영웅 가디언은 쓸 수 있다** — `Kukulza (Guardian)` 이 별도
유닛(56번)으로 있다. 변환시키지 말고 **그 유닛을 직접 놓는다.**
영웅으로 변환 연출을 짜려면 트리거로 지우고 새로 만들어야 한다.

> **아직 안 쟀다.** 이 표는 게임을 아는 사람에게 들은 것이다. dat 의
> 모프 깃발로는 안 갈린다 — 일반 하이 템플러와 타사다르(템플러)가
> 같은 값이다.

## 사거리 업그레이드는 데이터에 없다

`weapons.dat` 의 `maximumRange` 는 **기본 사거리**다. 드라군의
시경(Singularity Charge) 처럼 사거리를 올리는 업그레이드는 dat 이 아니라
게임 실행 파일에 박혀 있어 **여기서 잴 수 없다.**

- 잰 것: 페닉스(드라군)와 일반 드라군의 **기본 사거리는 128픽셀로 같다.**
- 밖의 자료는 엇갈린다. [나무위키](https://namu.wiki/w/%ED%94%BC%EB%8B%89%EC%8A%A4(%EC%8A%A4%ED%83%80%ED%81%AC%EB%9E%98%ED%94%84%ED%8A%B8%20%EC%8B%9C%EB%A6%AC%EC%A6%88))
  는 브루드워에서 페닉스(드라군)가 6이라 하고, 오리지널에서는 5라고 적는다.
  [StrategyWiki](https://strategywiki.org/wiki/StarCraft/Hero_Units) 는
  영웅이 "대개 모든 업그레이드를 갖고 나온다" 고 쓴다.
- **아직 확실히 못 정했다.** 게임을 돌려 재기 전에는 단정하지 않는다.
  사거리가 걸린 맵을 만들 때는 두 유닛을 나란히 놓고 직접 보는 편이 낫다.

잰 것 하나는 확실하다 — 짐 레이너(마린)의 **기본 사거리는 5타일**이고
일반 마린은 4타일이다. U-238 이 짐 레이너에게도 먹는지는 **아직 안
쟀다.** [Liquipedia](https://liquipedia.net/starcraft/Jim_Raynor_(Marine))
는 안 먹는다고 적지만, 같은 얘기를 벌쳐 이속업에 대해 했다가 틀렸다
(이온 추진기는 짐 레이너 벌쳐에도 먹는다). **밖의 자료를 그대로
옮기지 않는다.**

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

0. 공격 형태(일반·폭발·진동)가 맞는가? → [damage.md](damage.md)
1. 이 자리에서 **못 고치는 값** 중 무엇이 중요한가? (사거리·주기·시야·속도)
2. 그 값이 영웅과 일반 중 **어느 쪽이 나은가**? → `heroes.json` 을 본다
3. 체력·방어력만 필요한가? → **일반 유닛에 `unitdef set` 을 쓴다**
4. 공격력 업그레이드를 팔 맵인가? → 영웅도 받는다 (33/35짝)

## 관련

- [damage.md](damage.md) — 공격 형태 × 덩치. 적힌 피해가 그대로 안 들어간다
- [../chk/sections.md](../chk/sections.md) — UNIS·UNIx 가 담는 것
- [../usemap/essentials.md](../usemap/essentials.md) — 유즈맵 필수품
- [../tools/cli-cookbook.md](../tools/cli-cookbook.md) — `unit-stats` 쓰기
