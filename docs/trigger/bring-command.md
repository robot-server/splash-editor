# `Bring` 과 `Command` — 무엇을 세고 무엇을 안 세나

출처: 스타 에디터 아카데미 [팁] Bring과 Command의 인식 범위
(`cafe.naver.com/edac/book5095361/96809`).

`Bring` 은 실측 유즈맵 판정의 97%를 차지한다. 그런데 **견줌(AtMost /
Exactly / AtLeast)에 따라 세는 것이 달라진다.** 서로 보수가 아니다.

## 표

| | 수송선에 탄 유닛 | 건설 중인 건물 | 코쿤·에그·러커 에그 | 생산 중인 유닛 |
| --- | --- | --- | --- | --- |
| **Bring** AtMost | ✗ 안 셈 | ✓ 셈 | ✓ 셈 | ✗ |
| **Bring** Exactly/AtLeast | ✓ 셈 | ✗ | ✗ | ✗ |
| **Command** AtMost | ✓ 셈 | ✓ 셈 | ✓ 셈 | ✓ **셈** |
| **Command** Exactly/AtLeast | ✓ 셈 | ✗ | ✗ | ✗ |

> 변태 중인 에그·러커 에그·코쿤은 **"생산 중인 유닛" 으로 치지 않는다.**
> 따로 센다.

## 그래서 이런 일이 생긴다

```
Bring("Current Player", "Terran Marine", "transport", At most, 0)
  → 마린을 태운 드랍쉽이 그 자리에 있어도 "없다" 로 참이 된다

Bring("Current Player", "Zerg Cocoon", "loc", At least, 1)
  → 코쿤이 100마리 있어도 거짓

Bring("Current Player", "Zerg Cocoon", "loc", At most, 1)
  → 코쿤이 1마리 이하면 참 (센다)

Command("Current Player", "Buildings", Exactly, 0)
  → 건물이 다 부서지고 건설 중인 것만 남으면 참

Command("Current Player", "Protoss Scout", At most, 0)
  → 스카웃을 생산 중인 스타게이트가 있으면 거짓
```

## 맵을 짤 때 무엇을 조심하나

**"전멸했나" 판정.** `Command(…, "Men", At most, 0)` 은 생산 중인
유닛까지 세므로, 생산 큐가 남아 있으면 참이 안 된다. 반대로
`Bring(…, At most, 0)` 은 수송선에 탄 병력을 못 봐서 **살아 있는데
전멸로 친다.**

| 하려는 것 | 쓸 것 |
| --- | --- |
| 이 자리에 병력이 있나 | `Bring(…, At least, 1)` |
| 이 자리가 비었나 (수송선까지 봐야 함) | `Command` 쪽을 쓰거나 둘을 합친다 |
| 진짜 전멸했나 | `Command(…, "Men", At most, 0)` |
| 저글링이 몇 마리 이하인가 (알까지) | `Bring/Command … At most` |

**부정을 만들 때 견줌만 뒤집지 않는다.** `At least 1` 의 반대는
`At most 0` 이 아니다 — 세는 것이 다르다.

## 관련

- [death-counts.md](death-counts.md) — 죽은 수로 세는 쪽
- [execution.md](execution.md) — 트리거 순서와 웨잇 꼬임
- [recipes.md](recipes.md) — 바로 쓰는 조각
