# 로케이션 — 트리거가 세상을 보는 창

출처: 스타 에디터 아카데미
[[초급1] 스프라이트와 로케이션](https://cafe.naver.com/edac/book5095361/76408)
+ 유즈맵 329장 실측.

유즈맵 판정의 **97%가 `Bring`** 이고, `Bring` 은 로케이션으로 센다.
실측 유즈맵은 **이름 붙인 로케이션이 중앙 114개**다. 로케이션이 곧
맵의 구조다.

```sh
splash-cli location add 맵 <left> <top> <right> <bottom> --tiles --name "이름" -o 새맵
splash-cli location elevation 맵 <번호> 저지대,중지대,고지대,저공,중공,고공 -o 새맵
splash-cli location invert 맵 <번호> on -o 새맵
splash-cli location list 맵
```

## 이름을 함부로 짓지 않는다

카페가 먼저 못박는 것이 이것이다 — **쓸 용도에 맞게 알아보기 쉬운
이름**을 붙이고, **되도록 영어**로 쓴다.

트리거 텍스트에서 로케이션은 **이름으로** 가리킨다. 이름이 `Location 5`
면 트리거를 읽을 수 없고, 나중에 로케이션을 하나 지우면 번호가 밀려
엉뚱한 곳을 가리킨다.

로케이션 이름도 **스트링을 먹는다** — 스트링은 1024개 제한이다
([briefing-strings.md](briefing-strings.md)).

## 높이 조건 (Affects layers) — 잘 안 쓰는데 강하다

로케이션마다 **어느 높이의, 지상인지 공중인지**에만 걸리게 정할 수 있다.

| 칸 | 무엇 |
| --- | --- |
| Low | 가장 낮은 지형 (Jungle 의 Dirt, Space 의 Low Platform) |
| Med | 중간 지형 (Jungle 의 High Dirt·Temple, Badlands 의 Dirt) |
| High | 높은 지형 (Jungle 의 High Temple, Ice 의 High Outpost) |
| Ground / Air | 지상 / 공중 |

- `Low/Med/High Ground` 만 켜면 → **지상 유닛에만** 걸린다.
- `Low Ground/Air` 만 켜면 → **낮은 지형에 있는 것에만** 걸린다.
- 조건뿐 아니라 **액션에도 걸린다.** `Air` 만 켠 로케이션에
  `Kill Unit At Location` 을 쓰면 **공중 유닛만** 죽는다.

> **`Air` 에는 수송선에 탄 지상 유닛도 들어간다.** 드랍십에 실린
> 마린을 공중으로 센다. 태워서 빠져나가는 것을 막으려면 이것을
> 계산에 넣는다.

공중 유닛을 판정에서 빼고 싶을 때, 고지대에 올라간 유닛만 세고 싶을 때
쓴다. **트리거를 늘리지 않고 판정을 좁히는 방법**이다.

> 높이 이름이 타일셋마다 가리키는 지형이 다르다. 어느 지형이 어느
> 높이인지는 [../tileset/terrain-types.md](../tileset/terrain-types.md).

## 음수 로케이션 (Invert) — **더 깐깐한 판정**

출처: 스타 에디터 아카데미
[[중급6] 로케이션 심화](https://cafe.naver.com/edac/book5095361/76715).

`Invert X` 는 좌·우 좌표를, `Invert Y` 는 위·아래 좌표를 **서로
바꾼다.** 왼쪽 좌표가 오른쪽보다 커지는 **음수 로케이션**이 된다.

내가 "네모 바깥" 이라고 잘못 적어 두었다. **바깥이 아니다.**

| | 인식되는 때 |
| --- | --- |
| 보통 로케이션 | 유닛과 **닿기만 하면** |
| **음수 로케이션** | **로케이션이 유닛 안에 완전히 들어가야** |

- `Invert X` 만 → **좌우로** 조금이라도 삐져나가면 인식 안 됨.
  위아래로는 나가도 된다.
- `Invert Y` 만 → **위아래로** 삐져나가면 인식 안 됨.
- `Invert XY` → 어느 쪽으로든 삐져나가면 인식 안 됨.

로케이션을 유닛 충돌 크기와 비슷하게 잡으면 **유닛이 조금만 움직여도
판정이 풀린다.** 점 로케이션보다 **더 미세한 충돌 판정**을 만들 때
쓴다.

```sh
splash-cli location invert 맵 <번호> on -o 새맵
```

## `Anywhere`

맵 전체를 덮는 로케이션이다. **`Bring` 에 `Anywhere` 를 쓸 바엔
`Command` 를 쓴다** — 범위 계산이 없어 가볍고 뜻도 분명하다.

## 로케이션은 움직인다

`Move Location` 액션은 로케이션을 **유닛 위로** 옮긴다. 유닛을 따라
다니는 판정 구역을 만들 수 있다.

> **유닛이 없으면** 로케이션이 **(d)로케이션의 한가운데**로 간다.
> 없어질 수 있는 유닛에 붙였다면 이 동작을 계산에 넣는다.

트랩류 유닛에는 `Move Location` 이 **안 먹는다**
([../unit/quirks.md](../unit/quirks.md)).

## 인식 범위

`Bring` 이 유닛을 "안에 있다" 로 세는 기준은 유닛 크기와 로케이션
경계가 얽힌다 → [bring-command.md](bring-command.md).

## 관련

- [conditions.md](conditions.md) — 로케이션을 쓰는 조건들
- [api.md](api.md) — 인자 순서
- [../tileset/sprites.md](../tileset/sprites.md) — 스프라이트
