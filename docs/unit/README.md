# 유닛

유닛을 **고르는 것**과 **고치는 것**.

| 글 | 무엇 |
| --- | --- |
| [settings.md](settings.md) | 맵이 정하는 유닛 능력치. 유즈맵 98%가 쓴다 |
| [heroes.md](heroes.md) | 영웅은 "센 판"이 아니다. 맵이 못 고치는 값들 |
| [damage.md](damage.md) | 공격 형태 × 덩치. 적힌 피해가 그대로 안 들어간다 |

## 한 줄 요약

**맵이 고칠 수 있는 것은 다섯 가지뿐이다** — 체력·방패·방어력·생산시간·값.
사거리·공격 주기·시야·이동 속도는 게임 데이터에 박혀 있어 못 고친다.
그 값이 필요하면 **유닛 번호를 바꾸는 수밖에 없다.**

```sh
splash-cli unit-stats "$SC_INSTALL"          # 게임 데이터 전체
python3 $S/measure_heroes.py                 # 영웅 ↔ 일반 35짝 비교
python3 $S/measure_unitdefs.py <맵폴더>       # 실측 맵이 무엇을 고치나
```
