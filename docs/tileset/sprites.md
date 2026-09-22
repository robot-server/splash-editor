# 스프라이트 — 유닛이 아닌 것을 놓기

출처: 스타 에디터 아카데미
[[초급1] 스프라이트와 로케이션](https://cafe.naver.com/edac/book5095361/76408) ·
[[팁] 유닛 스프라이트의 Active 효과](https://cafe.naver.com/edac/book5095361/76540).

두 갈래가 있다. **성질이 아주 다르다.**

| | **유닛 스프라이트** | **퓨어 스프라이트** |
| --- | --- | --- |
| 무엇 | 유닛·건물을 스프라이트로 놓은 것 | 이펙트·두뎃·유닛 **껍데기** |
| 게임에서 | **진짜 유닛처럼 논다** | **장식일 뿐.** 클릭도 안 되고 유닛이 통과한다 |
| 플레이어 | 정할 수 있다 | — |
| 트리거로 만들기 | — | **불가능** (카페 답변) |

```sh
splash-cli sprite place 맵 <번호> <x> <y> --owner P --tiles -o 새맵
splash-cli sprite set 맵 <번호> --as-sprite on|off --disabled on|off -o 새맵
splash-cli sprite list 맵
```

## 밀리맵에 중립 건물을 넣는 유일한 방법

밀리맵으로 실행하면 **스타팅 · 중립 자원 · 중립 크리터 말고는 배치한
유닛이 전부 무시된다.** 그런데 **유닛 스프라이트는 남는다.**

> 원하는 유닛의 **유닛 스프라이트**를 **반드시 플레이어 12(중립) 소유**로
> 놓으면, 밀리맵으로 실행해도 중립 건물이 나타난다.

래더·리그 맵의 중립 건물(Temple, Khaydarin Crystal 등)이 이렇게 들어간다.

## 퓨어 스프라이트는 두뎃의 "가림막" 부분이다

나무처럼 **유닛이 뒤로 숨을 수 있는 두뎃**은, 지형 부분과 퓨어
스프라이트 부분이 나뉘어 있다.

- 두뎃을 지형화(`doodad to-terrain`)한 뒤 스프라이트를 지우면 →
  **가림막 없는 납작한 그림**만 남는다.
- 반대로 스프라이트만 남기면 가림막만 생긴다.

> **주의.** SCMDraft2 의 `Options - Map Load - Correct Doodads` 가
> 켜져 있으면, 맵을 열 때 그런 지형을 "잘못된 두뎃" 으로 보고 **자동으로
> 두뎃으로 되돌린다.** 그렇게 두고 싶으면 그 옵션을 꺼야 한다.

## Active 로 바꾸면 이상해진다 — 그리고 **맵을 날릴 수 있다**

유닛 스프라이트는 기본이 `Inactive` 다. `Active` 로 바꾸면 해괴한
모습이 된다 (카페 댓글: "비활성화는 더미데이터 상태, 프로토스 파워
없는 상태").

> **일부 유닛 스프라이트(시즈 모드 시즈탱크 등)를 `Active` 로 바꾸면
> 맵 파일 자체가 먹통이 되고, 그 상태에서 잘못 저장하면 맵 파일을
> 날린다.** 손대지 않는다.

## 놓으면 **튕기는** 스프라이트

아무거나 놓으면 안 된다. 카페가 모아 둔 목록이다.

| 갈래 | 튕기는 것 |
| --- | --- |
| Unit Sprites – Terran | Allan Turret · Duke Turret 1·2 · Goliath Turret · Tank Turret 1·2 |
| Pure – Doodads – Installation | Wall Flame Trap 01·02 · Wall Missile Trap 01·02 |
| Pure – Spells – Protoss | Psionic Storm |
| Pure – Spells – Terran | Lockdown + EMP Shockwave Missile 1·2 · Optical Flare Grenade · Yamato Gun |
| Pure – Spells – Zerg | Broodling Parasite · Consume · Plague Cloud |
| **Pure – Effects – Weapons** | **아래 열 가지 말고 전부 튕긴다** |
| Pure – Neutral – Resources | Vespene Geyser |
| Pure – Protoss | Photon Cannon · Archon Energy · Dark Archon Energy |
| Pure – Terran | Refinery · Civilian · Firebat · Ghost · Marine · Medic · Sarah Kerrigan (Ghost) · Siege Tank Base/Turret (Siege·Tank) · Scanner Sweep |
| Pure – Zerg | Hydralisk · Infested Kerrigan · Lurker |
| Pure – Unknown | Unknown White Circle 1·2·3 |

**Weapons 갈래에서 안 튕기는 것:** Scarab + Anti-Matter Missile
Overlay · Gemini Missiles Trail · Grenade Shot Smoke · Halo Rockets
Trail · Corrosive Acid Hit · Glave Wurm \ Seeker Spores Hit · Glave
Wurm Trail · Needle Spines · Seeker Spores Overlay · Subterranean
Spines.

## 이펙트는 한 번만 나온다

에디터에서는 이펙트가 계속 반복되지만 **게임에서는 시작할 때 한 번
나오고 사라진다.** 스킬 이펙트를 스프라이트로 깔아 두는 것은 쓸모가
거의 없다.

## 관련

- [doodads.md](doodads.md) — 두뎃
- [../game/crashes.md](../game/crashes.md) — 튕기는 원인 모음
- [../trigger/locations.md](../trigger/locations.md) — 로케이션
