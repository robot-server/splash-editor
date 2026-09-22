# epScript — EUD 를 글로 쓰기

> **먼저 [limits.md](limits.md) 를 읽는다.** EUD 는 사용자 허락을 받고
> 쓴다.

출처: 스타 에디터 아카데미 `인강으로 배우는 eps`
([기본 틀](https://cafe.naver.com/edac/book5121557/) ·
[변수](https://cafe.naver.com/edac/book5121557/) ·
[출력](https://cafe.naver.com/edac/book5121557/) ·
[once](https://cafe.naver.com/edac/book5121557/) ·
[랜덤](https://cafe.naver.com/edac/book5121557/)) ·
[[상급3]](https://cafe.naver.com/edac/book5095361/76915).

## 왜 이것을 쓰나

EUD 를 클래식 트리거로 손으로 짜면 CP 트릭 같은 것을 사람이 관리해야
한다. **epScript 는 그것을 글로 쓰게 해 준다.**

| | |
| --- | --- |
| **eudplib** | 파이썬 문법 |
| **epScript** | 자바스크립트 문법. **더 간결하고 요즘 이쪽으로 간다** |

카페도 **epScript 를 권한다.** 우리 CLI 는 이렇게 얹는다.

```sh
splash-cli eud build 맵 -o 새맵 --script a.eps
splash-cli eud which          # euddraft 를 어디서 찾았나
```

## 기본 틀 — 세 함수

```js
function onPluginStart() {
    // 맵이 시작될 때 딱 한 번
}

function beforeTriggerExec() {
    // 트리거가 돌기 **전**, 맵이 끝날 때까지 무한 반복
}

function afterTriggerExec() {
    // 트리거가 돌고 **난 뒤**, 역시 무한 반복
}
```

**클래식 트리거와 섞여 돈다.** `before` 는 그 주기의 트리거보다 먼저,
`after` 는 나중이다. 데스값을 트리거가 바꾸기 전에 읽고 싶으면
`before`, 바꾼 뒤를 보려면 `after` 다.

## 변수

```js
var zerg = 0;                       // 선언 + 값
var zerg;                           // 값은 나중에
var drone, overlord, zergling = 1, 2, 3;   // 한 줄에 여럿
const MAX = 100;                    // 상수
```

무한 반복 안에서 **한 번만** 돌리려면 `once`:

```js
function beforeTriggerExec() {
    once CreateUnit(1, "Terran Marine", "Anywhere", P1);
    once { /* 여러 줄을 한 번만 */ }
}
```

## 화면에 찍어 보기

```js
simpleprint(zerg);                          // 값 하나
simpleprint(zerg, terran, protoss);         // 여럿
simpleprint("zerg: ", zerg, " terran: ", terran);   // 글과 섞어
```

**디버깅의 기본**이다. 클래식 트리거에는 이런 것이 없어 데스값을
리더보드에 띄워 보곤 했다.

## 랜덤 — 진짜 난수가 생긴다

```js
function onPluginStart() {
    randomize();                 // **반드시** 한 번 부른다
}
...
const result = rand() % 3;       // 0·1·2 중 하나
```

> **`randomize()` 를 빼면 값이 매번 같게 고정된다.**

클래식 트리거의 스위치 이진법([../trigger/random.md](../trigger/random.md))
이 필요 없어진다. **EUD 를 쓰기로 했다면 랜덤은 이쪽이 훨씬 낫다.**

## 문법 낱개

| | |
| --- | --- |
| 주석 | `//` |
| 문장 끝 | `;` |
| 조건 | `if` · `else` |
| 견줌 | `==` `!=` `<` `<=` `>` `>=` |
| 논리 | `&&` `\|\|` |
| 반복 | `for` · `while`, 제어는 `break` · `continue` |
| 셈 | `+=` · `++` 같은 것이 된다 |

클래식 트리거에 **없던 것이 전부 생긴다** — 변수 · 반복 · 함수.
n×m 을 줄이려고 짜던 조립식 트리거
([../trigger/composition.md](../trigger/composition.md))도 `for` 하나로
끝난다.

## 트리거를 그대로 부른다

```js
CreateUnit(1, "Terran Marine", "Anywhere", P1);
```

클래식 액션·조건 이름을 그대로 쓴다. **인자 순서는
[../trigger/api.md](../trigger/api.md) 의 text 쪽**을 따른다.

## 언제 쓰나 — 그리고 언제 쓰지 않나

| | |
| --- | --- |
| **쓸 만한 자리** | 랜덤이 많이 필요할 때, 같은 일을 플레이어·유닛마다 되풀이할 때, 화면에 값을 보여 줄 때 |
| **쓰지 않는 자리** | 클래식 트리거로 충분한 맵. EUD 는 **판본을 타고** 사용자 허락이 필요하다 |

→ [limits.md](limits.md) · [how-it-works.md](how-it-works.md)

## 관련

- [limits.md](limits.md) — **안전 규칙**
- [how-it-works.md](how-it-works.md) — 주소와 EPD
- [unit-struct.md](unit-struct.md) — 유닛 한 마리 다루기
- [../tools/editors.md](../tools/editors.md) — EUD Draft
