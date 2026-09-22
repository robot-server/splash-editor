# 소리 — **wav 를 쓸 까닭이 없다**

출처: 스타 에디터 아카데미
[[팁] 사운드 삽입 및 용량 조절](https://cafe.naver.com/edac/book5095361/76435)
+ 실측.

실측 유즈맵에서 `Play WAV` 를 쓰는 맵은 브리핑 기준 24% 다. 그런데
**소리는 맵 용량을 통째로 좌우한다.**

## ogg 를 쓴다

리마스터부터 **ogg 를 지원한다.** wav 는 비압축이라 맵이 그만큼 커진다.

| 같은 2분짜리 음원 | 용량 |
| --- | ---: |
| wav | **19,967 KB** |
| 원본 mp3 | 4,524 KB |
| ogg 품질 10 | 6,200 KB (**오히려 늘어난다**) |
| ogg 품질 5 | 2,293 KB |
| **ogg 품질 0** | **870 KB** |

> 품질 0 (약 64 kb/s) 까지 줄여도 **음질 차이를 느끼기 어렵다.**
> ogg 를 지원하는 마당에 wav 를 쓸 까닭이 없다.

Audacity 로 `파일 → 내보내기 → OGG로 내보내기`. 품질은 0~10 이고
0 이 약 64 kb/s, 10 이 500 kb/s 다. wav 로 내보낼 일이 있으면
**`WAV(마이크로소프트) signed 16비트 PCM`** 으로 한다.

## 우리 CLI 는 ogg 를 그대로 받는다

명령 이름이 `sound`(WAV)로 되어 있지만 **ogg 도 들어간다.** 직접 넣어
확인했다.

```sh
splash-cli sound add 맵 음악.ogg --in-place
splash-cli sound list 맵
splash-cli sound extract 맵 <번호> 나온.wav
splash-cli sound remove 맵 <번호> --in-place
```

```
  0  staredit\wav\a.wav  8864 바이트
  1  staredit\wav\a.ogg  1527 바이트
```

## 게임에 원래 있는 소리는 **용량을 안 먹는다**

SCMDraft2 의 `Available game sound files (Virtual Sounds)` 에서 고르는
소리는 스타 데이터 안에 있는 것이라 **맵 용량에 포함되지 않는다.**

효과음은 되도록 이쪽에서 고른다. 직접 넣는 것은 **음악처럼 게임에 없는
것만**.

## 쓰기

`Play WAV` 액션은 **현재 플레이어에게만** 들린다 — 트리거 플레이어
체크에 걸린 사람만.

**음악을 되풀이하려면** 데스값을 써서 끝날 때쯤 다시 재생한다. 반복
기능이 따로 없다 → [../trigger/skills.md](../trigger/skills.md) 의
데스값 타이머와 같은 얼개다.

`Transmission` 액션은 소리 + 초상화 + 글 + 대기를 한꺼번에 한다
→ [../trigger/actions.md](../trigger/actions.md).

## 에디터 함정

SCMDraft2 사운드 편집기에서 **`Browse` 로 파일을 불러오지 않는다.**
`Batch Import` 를 쓴다.

- `Browse` 는 잘못 누르면 **에디터가 튕긴다.**
- 일부 형식은 `Browse` 로 넣으면 **소리가 깨진 채로 들어간다.**

## 관련

- [../trigger/actions.md](../trigger/actions.md) — `Play WAV` · `Transmission`
- [../trigger/briefing-strings.md](../trigger/briefing-strings.md) — 브리핑
- [dopamine.md](dopamine.md) — 유즈맵 재미 구조
