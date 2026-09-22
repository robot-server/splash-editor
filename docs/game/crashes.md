# 맵이 게임을 튕기게 하는 것들

**밸런스보다 앞선다.** 아래 중 하나라도 있으면 맵이 안 열리거나
게임이 죽는다.

출처: 스타 에디터 아카데미
[[팁] 맵 실행시 주요 튕김 원인](https://cafe.naver.com/edac/book5095361/76533)
(2025-09 갱신) + 직접 겪은 것.

> 리마스터 패치로 **일부는 더 이상 안 튕긴다** (대부분의 베타 시절 더미
> 유닛 배치 등). 아래는 그 뒤에도 남아 있는 것들이다.

## 1. 맵 바깥으로 튀어나온 유닛·건물

`scmap.Cli.place()` 가 막는다.

## 2. 놓으면 튕기는 유닛·스프라이트

### 유닛 / Unit Sprites — Terran

```
Allan Turret
Duke Turret type 1, 2
Goliath Turret
Tank Turret type 1, 2
```

이 넷(여섯)은 `scmap.CRASHING_UNITS` 에 들어 있다.

### Pure Sprites — 아래는 놓으면 튕긴다

| 갈래 | 튕기는 것 |
| --- | --- |
| Doodads / Installation | Wall Flame Trap 01·02, Wall Missile Trap 01·02 |
| Effects / Spells / Protoss | Psionic Storm |
| Effects / Spells / Terran | Lockdown + EMP Shockwave Missile (1·2), Optical Flare Grenade, Yamato Gun |
| Effects / Spells / Zerg | Broodling Parasite, Consume, Plague Cloud |
| Units / Neutral / Resources | **Vespene Geyser** (스프라이트로 놓는 경우) |
| Units / Protoss / Buildings | Photon Cannon |
| Units / Protoss / Ground | Archon Energy, Dark Archon Energy |
| Units / Terran / Buildings | Refinery |
| Units / Terran / Ground | Civilian, Firebat, Ghost, Marine, Medic, Sarah Kerrigan, Siege Tank Base(Siege·Tank), Siege Tank Turret(Siege·Tank) |
| Units / Terran / Special | Scanner Sweep |
| Units / Zerg / Ground | Hydralisk, Infested Kerrigan, Lurker |
| Unknown | Unknown White Circle 1·2·3 |

**Effects / Weapons 는 아래 열 가지 말고 전부 튕긴다:**

```
Scarab + Anti-Matter Missile Overlay   Gemini Missiles Trail (Wraith)
Grenade Shot Smoke (Vulture)           Halo Rockets Trail (Valkyrie)
Corrosive Acid Hit (Devourer)          Glave Wurm \ Seeker Spores Hit
Glave Wurm Trail (Mutalisk)            Needle Spines (Hydralisk)
Seeker Spores Overlay (Spore Colony)   Subterranean Spines (Lurker)
```

> 유닛(Unit)과 **순수 스프라이트(Pure Sprite)** 는 다른 것이다. 같은
> 이름이라도 스프라이트로 놓으면 튕기는 것이 많다.

## 3. 건물 체력을 0 으로 설정

정확히는 **체력이 닳으면 불·피 이펙트가 나는 건물**의 체력을 0 으로
잡으면 튕긴다. 그런 효과가 없는 스페셜 건물(프로토스 템플, 파워
제네레이터)은 괜찮다.

생산 시간 0 은 리마스터에서 괜찮아졌다 (생산된 유닛이 빨피가 되는
부작용은 있다).

## 4. Active(Disable) 시킨 일부 Unit Sprite

놓기만 해도 튕기는 스프라이트 목록은
[../tileset/sprites.md](../tileset/sprites.md) 에 옮겨 적었다.

리마스터로 대부분 괜찮아졌지만 **프로브·아콘 등은 여전히 튕긴다.**

> **시즈탱크(시즈 모드)** 는 에디터를 먹통으로 만들고, 그 상태에서 잘못
> 저장하면 **맵 파일을 날린다.** 손대지 않는다.

## 5. 유닛 번호에 딸린 튕김

출처: 스타 에디터 아카데미
[[팁] 유닛별 고유 특성들 (exe 영역)](https://cafe.naver.com/edac/book5095361/88226).

| 하면 | |
| --- | --- |
| **고스트류에 "단일 개체" 를 켜 두고** 그 고스트가 죽거나 `Remove` 되면 | **튕긴다** |
| **미네랄 필드 1·2·3 · 베스핀 간헐천을 유닛화**하면 | **튕긴다** |
| **건물의 최대 체력을 0 으로** 두면 | **튕긴다** ([[기초6]](https://cafe.naver.com/edac/book5095361/76373)) |

고스트류는 영웅까지 포함이다 — 일반 고스트 · 사라 케리건(고스트) ·
사미르 듀란(고스트) · 알렉세이 스투코프(고스트) · 감염된 듀란.
자세한 것은 [../unit/quirks.md](../unit/quirks.md).

## 6. 지형을 잘게 찍어 맵이 안 열린다

> `The map could not be loaded because it had too many obstructions.`

길찾기 덩어리(Pathfinder Region)가 너무 많아졌을 때 난다.
**직사각형으로 지형을 찍을 때 걸린다** — ISOM 으로 만들면 덩어리가
크게 뭉쳐 잘 안 걸린다
([[기초5]](https://cafe.naver.com/edac/book5095361/76372)).
자세한 것은 [../usemap/terrain.md](../usemap/terrain.md).

## 7. 지형·배치 때문에 튕기는 것

출처: 스타 에디터 아카데미 `에디터의 모/든/것`
[스타가 팅기는 증상들](https://cafe.naver.com/edac/book1678930/48509).

| 하면 | |
| --- | --- |
| **못 걷는 지형이 맵 타일의 66% 이상** | **튕긴다** |
| **맵의 5시 · 7시 구석에 유닛 배치** | **시작하자마자 튕긴다.** 트리거로 만들어도 안 나온다 |
| 유닛·건물을 왼쪽·오른쪽 **아래 구석**에 놓아 맵 밖으로 나가게 | 튕긴다. 화면 아래 인터페이스 영역까지 생각해야 한다 |
| 맵 크기가 **64·96·128·192·256 밖** | 튕긴다 (256×20 은 된다) |
| **플레이어 13 이상**의 유닛을 아무렇게나 배치 | 튕긴다 |

> **본문은 "건설 불가" 라고 적지만 댓글이 "이동 불가" 로 정정한다.**
> 그쪽이 맞다 — 검게 뚫든 물로 막든 **걸을 수 없으면 센다.**
>
> **내 맵 여섯 중 넷이 넘겼다.**
>
> | 맵 | 못 걷는 칸 |
> | --- | ---: |
> | Hydra RPG | **93.6%** |
> | OX Quiz | **88.9%** |
> | Zombie | **80.2%** |
> | Wave Defense | **70.5%** |
> | Control Arena | 61.6% |
> | Square Defense | 55.4% |
> | ISOM 으로 새로 만든 RPG | 32.0% |
> | 밀리맵 | 15.5% |
>
> `verify_map.py` 가 66% 를 넘으면 `!!`, 58% 를 넘으면 `?` 로 잡는다.

## 8. 설정값 때문에 튕기는 것

| 하면 | |
| --- | --- |
| **생산 시간(Build Time)을 0** 으로 한 유닛을 생산 | 튕긴다 |
| **트리거로 만드는 유닛·건물의 체력이 0** | 튕긴다 |
| 건물의 **최대 체력이 0** | 튕긴다 |
| **벙커·서플라이 말고** `Set Doodad State` 의 Disable | 튕긴다 |
| **한글 이름의 사운드 파일**을 재생 | 튕긴다 |
| 유닛 **시야**를 너무 크게 | 튕긴다 |

> **시야 문턱이 자료마다 다르다.** DatEdit 강좌는 **12 이상**,
> 이 글은 **16 이상**이라고 적는다. **가르지 못했다** — 안전하게
> 11 이하로 둔다.

## 9. 현재 판본이 모르는 유닛

**외형을 못 그리는 유닛이 시야에 들어오거나 맵에 있으면 튕긴다.**

| |
| --- |
| Unit ID **227 을 넘는** 코드 유닛 |
| Unused Terran Bldg · Unused Zerg Bldg (1·5) · Protoss Unused |
| Terran/Zerg/Protoss Marker |
| Independent Command Center · Independent Starport |
| Cantina · Cave · Cave-in · Jump Gate · Khaydarin Crystal Formation · Mining Platform · Ruins |
| **Alan Turret · Duke Turret · Goliath Turret · Tank Turret** |

이 목록은 내가 잰 것과 겹친다 — `starEditAvailabilityFlags` 가
`0x0000` 인 유닛들이다 ([../unit/heroes.md](../unit/heroes.md)).
**에디터가 놓게 해 준다고 게임이 받아 주는 것은 아니다.**

`Ctrl + F` 로 찾을 수 있다.

## 10. EUD 때문에 튕기는 것

> **[../eud/limits.md](../eud/limits.md) 의 안전 규칙이 먼저다.**

| 하면 | |
| --- | --- |
| 크기·이미지를 바꾼 EUD 유닛과 **에디터로 미리 배치한 같은 유닛이 겹칠 때** | 튕긴다. EUD 크기 변경은 **트리거로 만든 유닛에만** 걸린다 |
| 유닛 크기를 **0×0** 으로 | 무작위로 드랍 |
| 탑승 필요량을 바꾸고 **와이어프레임 Tranwire 크기를 안 고치면** | 튕긴다 |
| 투사체 지연시간을 너무 길게 (255 등) | 목표에 못 닿아 튕긴다 |
| 초상화를 **`Talking Portrait -` 종류**로 | 튕긴다 |
| 이미지를 고치고 **그림자 프레임이 본체와 다르면** | 튕긴다 |
| 외형이 **스크립트와 안 맞으면** (저글링 공중공격, 마린 마법 등) | 튕긴다 |
| 스크립트에 마법을 안 넣고 **스펠을 쓰게 하면** | 튕긴다 |
| **건물 스크립트 수정** | 웬만하면 하지 않는다. 건물에 Lifting 을 주면 드랍 |
| 최대 수량까지 만들었다 부수는 자리에 **사이오닉 스톰** | 튕긴다 |
| 플레이어 색이 정상이 아닌 **깃발을 클릭** | 튕긴다 (P1~12 만 깃발 초상화가 있다) |

## 11. 카페 댓글에서 더 나온 것

같은 글의 댓글과 다른 책에서 모은 것. **댓글이 본문을 정정하거나
보태는 일이 잦다.**

| 하면 | |
| --- | --- |
| **핵 미사일을 로케이션 이동으로 파괴**하면 | 튕긴다 |
| **맵 리빌러 자동 배치**가 맵 밖으로 나가면 | 튕긴다. 좌측 목록에서 찾아 지운다 |
| 유닛이 **클로킹을 시작하는 도중에 플레이어가 보면** | 튕긴다 |
| 유닛 **그림자 프레임이 본체보다 적으면** | 튕긴다 |
| 본체는 "그래픽 회전" 을 켜고 **그림자는 안 켜면** | 튕긴다 |
| **시즈 모드 탱크 아랫부분**을 갈거나 골리앗 위아래를 바꾸면 | 튕긴다 |

> **저그 건물 Disable 이 튕기는 것을 일부러 쓰기도 한다** — 맵핵으로
> 열어 보는 사람만 튕기게 만드는 방어다. 카페에 "성큰 디시블" 예가
> 있다. **다만 이것은 남의 게임을 튕기게 하는 것**이므로 이 저장소에서는
> 쓰지 않는다 ([../eud/limits.md](../eud/limits.md) 의 안전 규칙).

## 12. 그 밖에 내가 겪은 것

| 증상 | 원인 |
| --- | --- |
| 게임이 시작되지 않음 | 유즈맵에 스타팅 포인트가 없다 |
| 대기실에서 스타팅 없는 자리 | 사람 슬롯 수 ≠ 스타팅 수 |
| 자원이 0 으로 보임 | 자원 유효 비트가 꺼져 있다 |
| 배치한 유닛이 통째로 무시됨 | 슬롯 종족이 "선택 가능" 이다 (밀리 취급) |
| 밀리맵에서 자원이 안 나옴 | 자원 주인이 중립(플레이어 12) 이 아니다 |
| 지상 유닛이 갇힘 | 스타팅끼리 걸어서 안 닿는다 |

마지막 줄은 눈으로 못 본다 — **미니타일 길찾기로만** 안다
(`scmap.walk_grid` / `walk_reachable`).
