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

공중 유닛을 판정에서 빼고 싶을 때, 고지대에 올라간 유닛만 세고 싶을 때
쓴다. **트리거를 늘리지 않고 판정을 좁히는 방법**이다.

> 높이 이름이 타일셋마다 가리키는 지형이 다르다. 어느 지형이 어느
> 높이인지는 [../tileset/terrain-types.md](../tileset/terrain-types.md).

## 안팎 뒤집기 (Invert)

`location invert` 를 켜면 그 로케이션은 **네모 바깥**을 뜻한다.
"경기장 밖으로 나가면" 같은 판정을 로케이션 하나로 짤 수 있다.

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
