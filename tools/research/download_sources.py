#!/usr/bin/env python3
"""Download source maps from scmscx and Star Editor Academy articles from Naver Cafe.

Uses scmscx's public search/map endpoints and public Naver Cafe article pages.
Only downloads material reachable without bypassing a login or access restriction.
"""

from __future__ import annotations

import argparse
import html
from html.parser import HTMLParser
import json
import re
import sys
import time
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode, urljoin, urlparse
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "_local-corpus"
USER_AGENT = "SplashEditorResearch/1.0 (source-corpus downloader)"


def fetch(url: str, timeout: int = 40) -> tuple[bytes, str]:
    request = Request(url, headers={"User-Agent": USER_AGENT, "Accept": "*/*"})
    with urlopen(request, timeout=timeout) as response:
        return response.read(), response.headers.get_content_charset() or "utf-8"


def get_json(url: str) -> dict | list:
    raw, charset = fetch(url)
    return json.loads(raw.decode(charset, errors="replace"))


def save_if_missing(path: Path, data: bytes, force: bool) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not force:
        return False
    path.write_bytes(data)
    return True


def scmscx_search(query: str, offset: int, sort: str) -> dict:
    params = {"offset": offset, "sort": sort}
    base = "https://scmscx.com/api/uiv2/search"
    if query:
        base += "/" + quote(query, safe="")
    return get_json(base + "?" + urlencode(params))


def download_scmscx(args: argparse.Namespace, output: Path) -> None:
    selected: dict[str, dict] = {}
    for url in args.map_url:
        match = re.search(r"/map/([A-Za-z0-9]+)(?:[/?#]|$)", url)
        if not match:
            raise ValueError(f"unsupported scmscx map URL: {url}")
        selected[match.group(1)] = {"id": match.group(1), "filename": ""}

    if args.query:
        if args.limit is None or args.limit < 1:
            raise ValueError("search requires --limit; inspect candidates, then pass reviewed --map-url values")
        offset = 0
        while len(selected) < args.limit:
            result = scmscx_search(args.query, offset, args.sort)
            rows = result.get("maps", [])
            if not rows:
                break
            for row in rows:
                selected.setdefault(row["id"], row)
                offset += 1
                if len(selected) >= args.limit:
                    break
            if len(rows) < 300:
                break

    if not selected:
        raise ValueError("no map source specified; pass one or more --map-url values or --query with --limit")
    if args.list_only:
        for map_id, row in selected.items():
            label = row.get("filename") or "search candidate"
            print(f"https://scmscx.com/map/{map_id}\t{label}")
        return

    saved = 0
    for map_id, row in selected.items():
        info = get_json(f"https://scmscx.com/api/uiv2/map_info/{map_id}")
        mpq_hash = (info.get("meta") or {}).get("mpq_hash")
        if not mpq_hash:
            print(f"skip {map_id}: map blob unavailable", file=sys.stderr)
            continue
        filename = row.get("filename") or (info.get("meta") or {}).get("filename") or ""
        suffix = Path(filename).suffix.lower()
        if suffix not in (".scm", ".scx"):
            suffix = ".scx"
        dest = output / "scmscx" / "maps" / f"{map_id}{suffix}"
        if dest.exists() and not args.force:
            continue
        try:
            blob, _ = fetch(f"https://scmscx.com/api/maps/{mpq_hash}")
            if save_if_missing(dest, blob, args.force):
                saved += 1
                print(f"saved {dest.relative_to(output)} — {map_id}")
        except (HTTPError, URLError, TimeoutError) as exc:
            print(f"skip {map_id}: download failed: {exc}", file=sys.stderr)
        time.sleep(args.delay)
    print(f"scmscx: saved {saved}; requested maps {len(selected)}")


class ArticlePage(HTMLParser):
    BLOCKS = {"p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "blockquote"}

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title: list[str] = []
        self.body: list[str] = []
        self._in_title = False
        self._body_depth = 0
        self._in_body = False
        self._skip = 0

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attrs = dict(attrs)
        if tag in {"script", "style", "noscript"}:
            self._skip += 1
            return
        if tag == "h2" and "post_header" in (attrs.get("class") or ""):
            self._in_title = True
        if tag == "div" and attrs.get("id", "").startswith("post_"):
            self._in_body = True
            self._body_depth = 1
        elif self._in_body and not self._skip and tag == "div":
            self._body_depth += 1
        if self._in_body and not self._skip and tag in self.BLOCKS:
            self.body.append("\n")
        if self._in_body and not self._skip and tag == "img":
            alt = attrs.get("alt")
            if alt:
                self.body.append(f"[이미지: {alt}]")

    def handle_endtag(self, tag: str) -> None:
        if tag in {"script", "style", "noscript"} and self._skip:
            self._skip -= 1
            return
        if tag == "h2":
            self._in_title = False
        if self._in_body and not self._skip and tag in self.BLOCKS:
            self.body.append("\n")
        if self._in_body and tag == "div":
            self._body_depth -= 1
            if self._body_depth <= 0:
                self._in_body = False

    def handle_data(self, data: str) -> None:
        if self._skip:
            return
        text = " ".join(data.split())
        if not text:
            return
        if self._in_title:
            self.title.append(text)
        if self._in_body:
            self.body.append(text)

    def text(self) -> tuple[str, str]:
        title = " ".join(" ".join(self.title).split())
        body = re.sub(r"[ \t]+", " ", "".join(self.body))
        body = re.sub(r" *\n *", "\n", body).strip()
        return title, body


def article_url(url: str) -> tuple[str, str] | None:
    match = re.search(r"/book(\d+)/(\d+)(?:[/?#]|$)", url)
    if not match:
        return None
    return match.group(1), match.group(2)


def page_read_url(article: str) -> str:
    raw, charset = fetch(article)
    text = raw.decode(charset, errors="replace")
    match = re.search(r'<iframe[^>]+id=["\']cafe_main["\'][^>]+src=["\']([^"\']+)', text, re.I)
    if not match:
        # Attribute order differs in some page variants.
        match = re.search(r'<iframe(?=[^>]*id=["\']cafe_main["\'])(?=[^>]*src=["\']([^"\']+))[^>]*>', text, re.I)
        if match:
            src = match.group(1)
        else:
            raise ValueError("Naver Cafe article frame was not found (login or access may be required)")
    else:
        src = match.group(1)
    return urljoin("https://cafe.naver.com", html.unescape(src))


def download_article(url: str) -> tuple[str, str]:
    frame_url = page_read_url(url)
    raw, charset = fetch(frame_url)
    parser = ArticlePage()
    parser.feed(raw.decode(charset, errors="replace"))
    return parser.text()


def toc_entries(path: Path) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        fields = line.rsplit("\t", 1)
        if len(fields) == 2 and fields[1].startswith("http"):
            entries.append((fields[0].strip(), fields[1].strip()))
    return entries


def naver_list_urls(url: str) -> list[str]:
    raw, charset = fetch(url)
    text = raw.decode(charset, errors="replace")
    found = re.findall(r'''href=["']((?:https?://cafe\.naver\.com)?/[^"'<>\s]*/book\d+/\d+(?:\?[^"'<>\s]*)?)["']''', text, re.I)
    return list(dict.fromkeys(urljoin("https://cafe.naver.com", html.unescape(item)) for item in found))


def discover_naver_urls(cafe_url: str, delay: float) -> list[str]:
    home, charset = fetch(cafe_url)
    text = home.decode(charset, errors="replace")
    club_match = re.search(r'g_sClubId\s*=\s*["\'](\d+)', text)
    if not club_match:
        raise ValueError("could not find the Cafe ID on the provided Cafe home page")
    club_id = club_match.group(1)
    slug = urlparse(cafe_url).path.strip("/").split("/")[0]
    shelf_url = f"https://cafe.naver.com/BookShelf.nhn?clubid={club_id}"
    shelf, charset = fetch(shelf_url)
    shelf_text = shelf.decode(charset, errors="replace")
    book_ids = list(dict.fromkeys(re.findall(rf'''/{re.escape(slug)}/book(\d+)''', shelf_text, re.I)))
    if not book_ids:
        raise ValueError("no Cafe books were found on the public bookshelf")
    urls: list[str] = []
    for book_id in book_ids:
        book_url = f"https://cafe.naver.com/{slug}/book{book_id}"
        urls.extend(naver_list_urls(book_url))
        time.sleep(delay)
    return list(dict.fromkeys(urls))


def download_naver(args: argparse.Namespace, output: Path) -> None:
    urls: list[str] = []
    entries_by_book: dict[str, dict[str, str]] = {}
    modes = [
        bool(args.toc), bool(args.all_toc), bool(args.article_url),
        bool(args.list_url), bool(args.cafe_url),
    ]
    if sum(modes) > 1:
        raise ValueError("choose exactly one Cafe source type per run")
    if not any(modes):
        raise ValueError(
            "no Cafe source specified; provide --cafe-url, --list-url, "
            "--article-url, --toc, or --all-toc"
        )
    for toc in args.toc:
        for title, url in toc_entries(toc):
            urls.append(url)
            ids = article_url(url)
            if ids:
                entries_by_book.setdefault(ids[0], {})[url] = title
    if args.all_toc:
        for toc in sorted((output / "edac").glob("**/_toc.tsv")):
            for title, url in toc_entries(toc):
                urls.append(url)
                ids = article_url(url)
                if ids:
                    entries_by_book.setdefault(ids[0], {})[url] = title
    urls.extend(args.article_url)
    for listing in args.list_url:
        urls.extend(naver_list_urls(listing))
    if args.cafe_url:
        urls.extend(discover_naver_urls(args.cafe_url, args.delay))
    urls = list(dict.fromkeys(urls))
    if not urls:
        raise ValueError("the specified Cafe source contained no article URLs")
    if args.list_only:
        print("\n".join(urls))
        print(f"Naver Cafe: discovered {len(urls)} article URLs")
        return
    saved = 0
    for index, url in enumerate(urls):
        ids = article_url(url)
        if not ids:
            print(f"skip unsupported Naver Cafe URL: {url}", file=sys.stderr)
            continue
        book_id, article_id = ids
        dest_dir = output / "edac" / f"book{book_id}"
        existing = list(dest_dir.glob(f"*_{article_id}.txt"))
        dest = existing[0] if existing else dest_dir / f"{index:04d}_{article_id}.txt"
        if dest.exists() and not args.force:
            first_line = dest.read_text(encoding="utf-8", errors="replace").splitlines()[:1]
            title = first_line[0].removeprefix("# ").strip() if first_line else article_id
            entries_by_book.setdefault(book_id, {}).setdefault(url, title)
            continue
        try:
            title, body = download_article(url)
            if not body:
                raise ValueError("article body was empty (login, deleted, or access restricted)")
            content = f"# {title or 'Naver Cafe article'}\n# {url}\n\n{body}\n"
            if save_if_missing(dest, content.encode("utf-8"), args.force):
                saved += 1
                print(f"saved {dest.relative_to(output)}")
            entries_by_book.setdefault(book_id, {})[url] = title or article_id
        except (HTTPError, URLError, TimeoutError, ValueError) as exc:
            print(f"skip {url}: {exc}", file=sys.stderr)
        time.sleep(args.delay)
    for book_id, entries in entries_by_book.items():
        toc_path = output / "edac" / f"book{book_id}" / "_toc.tsv"
        toc_path.parent.mkdir(parents=True, exist_ok=True)
        toc_path.write_text("".join(f"{title}\t{url}\n" for url, title in entries.items()), encoding="utf-8")
    print(f"Naver Cafe: saved {saved}; found {len(urls)} article URLs")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help="local corpus destination (default: repository _local-corpus)")
    parser.add_argument("--force", action="store_true", help="replace files that already exist")
    parser.add_argument("--delay", type=float, default=0.5,
                        help="polite delay between requests in seconds")
    sources = parser.add_subparsers(dest="source", required=True)

    scmscx = sources.add_parser("scmscx", help="download maps from scmscx.com")
    scmscx.add_argument("--query", help="explicit site search query used only to discover candidate maps")
    scmscx.add_argument("--sort", default="timeuploadednew",
                        choices=("timeuploadednew", "timeuploadedold", "lastmodifiednew", "lastmodifiedold", "scenario", "scenariodesc", "filename", "filenamedesc"))
    scmscx.add_argument("--limit", type=int,
                        help="maximum number of search candidates to list; required with --query")
    scmscx.add_argument("--map-url", action="append", default=[],
                        help="reviewed scmscx map page to download; repeatable")
    scmscx.add_argument("--list-only", action="store_true",
                        help="list candidate URLs without downloading map files")

    naver = sources.add_parser("naver", help="download Star Editor Academy Cafe articles")
    naver.add_argument("--toc", action="append", type=Path, default=[],
                       help="existing tab-separated title/URL index; repeatable")
    naver.add_argument("--all-toc", action="store_true",
                       help="use every _toc.tsv already present under the output's edac directory")
    naver.add_argument("--list-url", action="append", default=[],
                       help="public Cafe page whose links contain article URLs; repeatable")
    naver.add_argument("--article-url", action="append", default=[],
                       help="article URL to download; repeatable")
    naver.add_argument("--cafe-url",
                       help="public Cafe home to discover; required when no list/article/TOC source is supplied")
    naver.add_argument("--list-only", action="store_true",
                       help="discover/print article URLs without downloading them")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output = args.output.expanduser().resolve()
    try:
        if args.source == "scmscx":
            if args.limit is not None and args.limit < 1:
                raise ValueError("--limit must be greater than zero")
            download_scmscx(args, output)
        else:
            download_naver(args, output)
    except (HTTPError, URLError, TimeoutError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
