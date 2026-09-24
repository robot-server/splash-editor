# 원본 자료 내려받기

이 도구는 학습 후보를 로컬에 가져온다. 내려받았다는 이유만으로 맵 장르나 지식 패턴을 분류하지 않는다. 실제 제작 규칙은 맵 렌더·배치·트리거를 기능별로 확인한 뒤 `docs/`에 적는다.

## 스에아 카페 주소를 생략한 경우

`--cafe-url`을 생략해도 `--toc`, `--all-toc`, `--list-url`, `--article-url` 중 하나로 자료 범위를 지정했으면 진행한다. 특히 `--all-toc`는 `--output` 아래의 `edac/**/_toc.tsv`에 이미 적힌 주소를 사용하므로 카페를 새로 탐색하지 않는다. 이 입력도 없으면 어느 카페를 뜻하는지 알 수 없어 오류로 중단한다. 공개 글을 책장부터 새로 발견할 때만 `--cafe-url`에 카페 주소를 지정한다. 스크립트는 공개 책장의 도서 링크를 따라가 각 도서의 글 URL을 모으고, 로그인 없이 열리는 글 본문을 내려받는다. 주제별 필터는 하지 않는다. 로그인·삭제·접근 제한으로 본문을 읽지 못한 글은 건너뛰며 다른 자료로 바꾸지 않는다.

자료 범위는 `--cafe-url`, `--list-url`, 반복 가능한 `--article-url`, `--toc`, 또는 기존 목차 전체를 쓰는 `--all-toc` 중 하나로 지정한다. `--list-only`는 발견한 글 주소만 출력한다. 지정한 공개 책장에 접근할 수 없으면 오류로 멈추며, 임의의 자료로 대체하지 않는다.

## scmscx 맵 선택

검색 결과의 최신순이나 이름만으로 기능 구현 사례라고 판단하지 않는다. 검색은 후보를 찾는 용도로만 쓰고, 맵 페이지에서 목적에 맞는 배치·트리거 사례를 확인한 다음 선택한 URL만 내려받는다. 확인한 주소를 직접 받을 때는 `--map-url`을 반복 지정한다. 검색 결과를 살펴보기만 할 때는 `--query`와 `--limit`에 `--list-only`를 더한다. `--query`에는 반드시 검색어와 양수 한도를 지정해야 한다.

검색어와 한도는 후보를 좁히는 입력일 뿐 제작 지식의 근거가 아니다. 이름, 검색 순위, 조회·다운로드 수, 파일 수로 기능을 추정하지 않는다. 밀리맵은 시작 위치·자원 포켓·고저·통로를 렌더와 배치에서 확인하고, 유즈맵은 같은 맵의 조건·액션·상태·로케이션을 이어 읽는다. 구체적 사례는 [자료 근거 원칙](../method/corpus.md)을 따른다.

## 저장 위치와 접근 제한

기본 출력은 저장소의 무시된 `_local-corpus/`다. `--output`으로 다른 로컬 위치를 지정할 수 있으며, 원본 파일과 카페 본문 덤프는 커밋하지 않는다. 네트워크 오류, 로그인 벽, 삭제된 글은 추측이나 다른 사이트로 대체하지 말고 출처와 접근 상태를 기록한다.

```sh
python3 tools/research/download_sources.py naver --cafe-url "https://cafe.naver.com/edac" --list-only
python3 tools/research/download_sources.py scmscx --query "사용할 검색어" --limit 20 --list-only
python3 tools/research/download_sources.py scmscx --map-url "https://scmscx.com/map/맵ID"
python3 tools/research/download_sources.py naver --article-url "https://cafe.naver.com/edac/book.../..."
```
