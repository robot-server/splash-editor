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

리마스터로 대부분 괜찮아졌지만 **프로브·아콘 등은 여전히 튕긴다.**

> **시즈탱크(시즈 모드)** 는 에디터를 먹통으로 만들고, 그 상태에서 잘못
> 저장하면 **맵 파일을 날린다.** 손대지 않는다.

## 5. 그 밖에 내가 겪은 것

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
