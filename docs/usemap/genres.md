# 장르마다 무엇이 들어가는가

유즈맵 563장(트리거 30개 초과)을 이름으로 갈라, **장르마다 몇 %의 맵이
그 동작·조건을 쓰는지** 셌다. 생성기를 장르마다 따로 쓰는 대신 이 표를
보고 [부품](../../.agents/skills/starcraft-map/scripts/scmap.py) 을 조립한다.

**장르 분포** — 키우기/RPG 30% · 디펜스 13% · 컨트롤 6% · 좀비 6% ·
퀴즈 3% · 탈출 2% · 블러드 2% · 그 밖에 1% 미만이 넷.
**그리고 32%(182장)는 이름으로 못 갈랐다.** 앞서 이 자리에 "나머지
1% 미만" 이라고 적었는데 틀린 말이었다 — 갈린 것이 전부의 3분의 2다.

`data/genres.json` 에 원자료가 있다.

> **이 표를 믿기 전에 세 가지를 알아 둔다.**
>
> 1. **표를 목표로 삼지 않는다.** "90% 가 쓰니 나도 넣는다" 가 아니라
>    "이 장르가 왜 그걸 쓰는가" 를 보고 필요하면 쓴다. 수치를 맞추려다
>    알맹이 없는 맵을 만든 적이 여러 번이다.
> 2. **세는 것이 "쓴 맵 %" 라 역할을 모른다.** `Modify Unit Hit Points`
>    를 쓰는 맵의 절반 가까이가 세 번 이하로 쓴다 — 보스 체력을 한 번
>    맞추는 것과 회복 구역을 운영하는 것이 똑같이 1로 세어졌다.
> 3. **표본이 적은 장르는 법칙이 아니라 표본 그 자체다.** 땅따먹기 5장,
>    술래잡기 4장, 축구 4장에서 나온 "100%" 는 그 네댓 장의 이야기다.
>
> 분류도 이름에 든 낱말로 한 것이라 "당첨된 좀비 키우기" 는 키우기로,
> "좀비 랜덤 디펜스" 는 디펜스로 들어갔다. 알맹이가 뽑기인 맵이 그렇게
> 사라진다.

## 이름 말고 트리거로 갈라 보면

이름 분류가 못 미더워 **트리거만 보고** 같은 딱지를 맞출 수 있는지 쟀다
(`scripts/classify_genre.py`, 유즈맵 484장). 장르마다 "어떤 동작·조건을
쓰는가" 를 벡터로 만들고, 하나를 빼고 나머지의 평균과 견줘 맞춘다.

    전체 정확도 76% (246/323).  아무렇게나 찍으면 10%.

| 장르 | 표본 | 맞춤 | 가장 많이 헷갈린 것 |
| --- | --- | --- | --- |
| 땅따먹기 | 5 | 100% | — |
| 디펜스 | 47 | 89% | 탈출 2장 |
| 컨트롤 | 37 | 81% | 축구 2장 |
| 키우기 | 146 | 78% | **좀비 16장** |
| 좀비 | 35 | 74% | 탈출 3장 |
| 블러드 | 17 | 64% | 컨트롤 3장 |
| 탈출 | 17 | 64% | 축구 2장 |
| 퀴즈 | 8 | 62% | 좀비 2장 |
| 축구·야구 | 7 | 28% | 키우기 2장 |
| 술래잡기 | 4 | **0%** | 좀비 3장 |

**읽는 법.** 표본이 서른 장을 넘는 장르는 트리거만 보고도 가려진다 —
그 장르가 실제로 다른 일을 한다는 뜻이다. 표본이 한 자리인 장르는
가려지지 않는다. 술래잡기 4장이 전부 좀비로 붙은 것은 "술래잡기는
좀비의 변종" 이라는 뜻일 수도, 표본이 없다는 뜻일 수도 있다 —
**넷으로는 알 수 없다.** 그 장르의 "100%" 를 법칙처럼 쓰지 않는다.

장르를 가르는 트리거 (그 장르에서 쓰는 비율 − 나머지에서 쓰는 비율):

| 장르 | 남들보다 많이 쓰는 것 |
| --- | --- |
| 디펜스 | `Order` · `Run AI Script At Location` · `Leader Board Kills` |
| 키우기 | `Leader Board Points` +0.52 · `Modify Unit Shield/Hit Points` +0.50 · `Run AI Script At Location` +0.49 · `Center View` +0.35 |
| 좀비 | `Countdown Timer` +0.63 · `Transmission` +0.54 · `Set Countdown Timer` +0.53 · `Set Deaths` +0.49 |
| 블러드 | `Kill` +0.71 · `Opponents` +0.66 · `Leader Board Kills` +0.64 |
| 퀴즈 | `Switch` +0.36 · `Leader Board Points` +0.28 · `Draw` +0.27 — 그리고 `Set Resources` −0.91, `Create Unit` −0.74 (**퀴즈는 유닛을 안 준다**) |

원자료는 `data/genre-fingerprints.json` 이다.

**이름으로 안 갈린 161장을 지문으로 붙여 보면** 키우기 19 · 좀비 13 ·
컨트롤 10 · 디펜스 10 · 블러드 8 … 이고, **96장은 1등과 2등이 붙어 있어
가릴 수 없다.** 유즈맵의 5분의 1은 장르가 없거나 섞여 있다는 뜻이다.
장르를 골라 만들라고 하기 전에 그 사실을 알고 있어야 한다.

---

## 키우기 / RPG — 표본 170장

| | 중앙값 |
| --- | --- |
| 유닛 | **1015** |
| 트리거 | **232** |
| 브리핑 쓰는 맵 | 91% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Create Unit` | 99% | | `Bring` | 99% |
| `Preserve Trigger` | 98% | | `Always` | 97% |
| `Display Text Message` | 98% | | `Command` | 90% |
| `Set Resources` | 95% | | `Score` | 88% |
| `Wait` | 95% | | `Switch` | 75% |
| `Move Unit` | 94% | | `Accumulate` | 74% |
| `Center View` | 92% | | `Deaths` | 66% |
| `Modify Unit Hit Points` | 90% | | `Elapsed Time` | 65% |
| `Victory` | 89% | | `Kill` | 56% |
| `Set Score` | 86% | |  |  |
| `Remove Unit At Location` | 85% | |  |  |
| `Kill Unit At Location` | 83% | |  |  |
| `Order` | 80% | |  |  |
| `Run AI Script At Location` | 78% | |  |  |
| `Defeat` | 78% | |  |  |
| `Leader Board Points` | 77% | |  |  |
| `Set Switch` | 77% | |  |  |
| `Create Unit with Properties` | 77% | |  |  |
| `Set Alliance Status` | 73% | |  |  |
| `Modify Unit Shield Points` | 71% | |  |  |

---

## 디펜스 · 막기 — 표본 78장

| | 중앙값 |
| --- | --- |
| 유닛 | **294** |
| 트리거 | **188** |
| 브리핑 쓰는 맵 | 69% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Preserve Trigger` | 100% | | `Always` | 100% |
| `Create Unit` | 100% | | `Bring` | 98% |
| `Wait` | 98% | | `Accumulate` | 87% |
| `Defeat` | 97% | | `Command` | 64% |
| `Set Resources` | 94% | | `Switch` | 62% |
| `Display Text Message` | 94% | | `Score` | 60% |
| `Order` | 93% | | `Kill` | 58% |
| `Leader Board Kills` | 92% | | `Elapsed Time` | 55% |
| `Victory` | 82% | |  |  |
| `Kill Unit At Location` | 80% | |  |  |
| `Move Unit` | 76% | |  |  |
| `Run AI Script` | 75% | |  |  |
| `Set Invincibility` | 73% | |  |  |
| `Kill Unit` | 70% | |  |  |

---

## 컨트롤 · 전투 — 표본 38장

| | 중앙값 |
| --- | --- |
| 유닛 | **254** |
| 트리거 | **258** |
| 브리핑 쓰는 맵 | 81% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Preserve Trigger` | 100% | | `Always` | 100% |
| `Create Unit` | 100% | | `Bring` | 97% |
| `Set Resources` | 100% | | `Accumulate` | 89% |
| `Display Text Message` | 97% | | `Switch` | 73% |
| `Create Unit with Properties` | 94% | | `Elapsed Time` | 63% |
| `Order` | 92% | | `Command` | 60% |
| `Wait` | 89% | | `Kill` | 55% |
| `Defeat` | 89% | |  |  |
| `Victory` | 89% | |  |  |
| `Modify Unit Hanger Count` | 86% | |  |  |
| `Kill Unit At Location` | 84% | |  |  |
| `Kill Unit` | 84% | |  |  |
| `Center View` | 76% | |  |  |
| `Set Switch` | 73% | |  |  |
| `Move Unit` | 71% | |  |  |

---

## 좀비 — 표본 35장

| | 중앙값 |
| --- | --- |
| 유닛 | **1145** |
| 트리거 | **373** |
| 브리핑 쓰는 맵 | 91% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Create Unit` | 97% | | `Always` | 97% |
| `Preserve Trigger` | 97% | | `Bring` | 97% |
| `Kill Unit At Location` | 94% | | `Command` | 94% |
| `Order` | 94% | | `Countdown Timer` | 85% |
| `Victory` | 91% | | `Switch` | 85% |
| `Display Text Message` | 91% | | `Deaths` | 82% |
| `Defeat` | 91% | | `Elapsed Time` | 77% |
| `Set Alliance Status` | 91% | | `Accumulate` | 77% |
| `Set Resources` | 85% | | `Score` | 71% |
| `Play WAV` | 85% | |  |  |
| `Modify Unit Hit Points` | 85% | |  |  |
| `Wait` | 85% | |  |  |
| `Center View` | 82% | |  |  |
| `Move Location` | 82% | |  |  |
| `Set Switch` | 82% | |  |  |
| `Move Unit` | 82% | |  |  |
| `Remove Unit At Location` | 80% | |  |  |
| `Run AI Script` | 80% | |  |  |
| `Set Deaths` | 77% | |  |  |
| `Set Invincibility` | 77% | |  |  |
| `Give Units to Player` | 77% | |  |  |
| `Kill Unit` | 77% | |  |  |

---

## 퀴즈 — 표본 17장

| | 중앙값 |
| --- | --- |
| 유닛 | **156** |
| 트리거 | **1497** |
| 브리핑 쓰는 맵 | 47% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Display Text Message` | 100% | | `Always` | 100% |
| `Preserve Trigger` | 94% | | `Switch` | 94% |
| `Set Switch` | 94% | | `Command` | 88% |
| `Set Score` | 94% | | `Score` | 88% |
| `Run AI Script` | 94% | | `Bring` | 76% |
| `Kill Unit At Location` | 94% | | `Deaths` | 64% |
| `Leader Board Points` | 88% | | `Never` | 52% |
| `Create Unit` | 82% | | `Memory` | 52% |
| `Move Unit` | 70% | |  |  |
| `Create Unit with Properties` | 70% | |  |  |
| `Center View` | 70% | |  |  |

---

## 탈출 — 표본 15장

| | 중앙값 |
| --- | --- |
| 유닛 | **140** |
| 트리거 | **90** |
| 브리핑 쓰는 맵 | 53% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Preserve Trigger` | 86% | | `Bring` | 86% |
| `Display Text Message` | 86% | | `Command` | 80% |
| `Defeat` | 86% | | `Always` | 73% |
| `Wait` | 86% | | `Switch` | 73% |
| `Kill Unit At Location` | 86% | | `Score` | 53% |
| `Create Unit` | 80% | |  |  |
| `Set Switch` | 73% | |  |  |

---

## 블러드 — 표본 15장

| | 중앙값 |
| --- | --- |
| 유닛 | **298** |
| 트리거 | **134** |
| 브리핑 쓰는 맵 | 93% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Preserve Trigger` | 100% | | `Always` | 100% |
| `Victory` | 100% | | `Kill` | 93% |
| `Create Unit` | 100% | | `Command` | 73% |
| `Leader Board Kills` | 93% | | `Opponents` | 66% |
| `Create Unit with Properties` | 86% | | `Bring` | 53% |
| `Defeat` | 80% | |  |  |

---

## 땅따먹기 — 표본 5장

| | 중앙값 |
| --- | --- |
| 유닛 | **41** |
| 트리거 | **128** |
| 브리핑 쓰는 맵 | 100% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Leader Board Control` | 100% | | `Always` | 100% |
| `Wait` | 100% | | `Commands the Most At` | 100% |
| `Leader Board Kills` | 100% | | `Bring` | 100% |
| `Preserve Trigger` | 100% | | `Accumulate` | 100% |
| `Remove Unit At Location` | 100% | | `Command` | 100% |
| `Create Unit` | 100% | | `Opponents` | 100% |
| `Set Resources` | 100% | | `Score` | 100% |
| `Display Text Message` | 100% | | `Elapsed Time` | 100% |
| `Defeat` | 100% | |  |  |
| `Victory` | 100% | |  |  |
| `Kill Unit At Location` | 100% | |  |  |
| `Set Score` | 100% | |  |  |
| `Remove Unit` | 100% | |  |  |
| `Kill Unit` | 100% | |  |  |
| `Create Unit with Properties` | 100% | |  |  |
| `Order` | 100% | |  |  |
| `Move Unit` | 100% | |  |  |
| `Run AI Script At Location` | 100% | |  |  |
| `Set Alliance Status` | 100% | |  |  |

---

## 술래잡기 — 표본 4장

| | 중앙값 |
| --- | --- |
| 유닛 | **292** |
| 트리거 | **102** |
| 브리핑 쓰는 맵 | 50% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Wait` | 100% | | `Always` | 100% |
| `Preserve Trigger` | 100% | | `Bring` | 100% |
| `Set Alliance Status` | 100% | | `Elapsed Time` | 100% |
| `Create Unit` | 100% | | `Switch` | 50% |
| `Display Text Message` | 100% | | `Deaths` | 50% |
| `Set Countdown Timer` | 100% | | `Accumulate` | 50% |
| `Center View` | 100% | | `Score` | 50% |
| `Move Location` | 100% | | `Countdown Timer` | 50% |
| `Run AI Script` | 100% | |  |  |

---

## 축구 · 스포츠 — 표본 4장

| | 중앙값 |
| --- | --- |
| 유닛 | **298** |
| 트리거 | **810** |
| 브리핑 쓰는 맵 | 50% |

**거의 다 쓰는 동작** (쓴 맵 비율)

| 동작 | % | | 조건 | % |
| --- | --- | --- | --- | --- |
| `Set Memory` | 75% | | `Deaths` | 75% |
| `Display Text Message` | 75% | | `Always` | 75% |
| `Draw` | 75% | | `Elapsed Time` | 75% |
| `Set Deaths` | 75% | | `Switch` | 75% |
| `Give Units to Player` | 75% | | `Bring` | 75% |
| `Preserve Trigger` | 75% | | `Command` | 75% |
| `Play WAV` | 75% | | `Custom` | 50% |
| `Set Score` | 75% | | `Memory` | 50% |
| `Move Unit` | 75% | | `Accumulate` | 50% |
| `Create Unit` | 75% | |  |  |
| `Center View` | 75% | |  |  |
| `Run AI Script` | 75% | |  |  |
| `Order` | 75% | |  |  |
| `Create Unit with Properties` | 75% | |  |  |
