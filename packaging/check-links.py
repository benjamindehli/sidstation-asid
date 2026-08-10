#!/usr/bin/env python3
"""Check that every local link in the docs site resolves to a file on disk.

Run from anywhere:  python3 packaging/check-links.py

Walks the HTML in docs/, collects every href, src and srcset candidate, and
resolves each one relative to the file it appears in. Exits 1 listing anything
that does not exist.

This exists because the site has pages at two different depths: index.html sits
at the site root, docs/protocol/index.html one level down, and the stylesheet
refers to the font from a third. The same asset therefore has a different
correct path in each file, which has already produced one 404 that looked fine
in review and only showed up in production. External URLs are not fetched, so
this stays fast and offline: it catches the mistake that is easy to make, not
link rot.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"

# href/src on any element, plus every candidate inside a srcset.
ATTR = re.compile(r'(?:href|src)="([^"]+)"')
SRCSET = re.compile(r'srcset="([^"]+)"')
CSS_URL = re.compile(r'url\("?([^")]+)"?\)')


def is_external(url):
    return url.startswith(("http://", "https://", "//", "mailto:", "data:", "#"))


def references(path):
    """Every local URL referenced by a file, with the fragment stripped."""
    text = path.read_text(encoding="utf-8")
    found = set(ATTR.findall(text)) | set(CSS_URL.findall(text))
    for group in SRCSET.findall(text):
        for candidate in group.split(","):
            parts = candidate.split()
            if parts:
                found.add(parts[0])
    return {u.split("#")[0] for u in found if not is_external(u) and u.split("#")[0]}


def main():
    files = sorted(DOCS.rglob("*.html")) + sorted(DOCS.rglob("*.css"))
    if not files:
        sys.exit(f"no HTML or CSS found under {DOCS.relative_to(ROOT)}")

    broken = []
    checked = 0
    for f in files:
        for url in sorted(references(f)):
            checked += 1
            target = (f.parent / url).resolve()
            # A directory URL such as protocol/ is served by its index.html.
            if target.is_dir():
                target = target / "index.html"
            if not target.exists():
                broken.append((f.relative_to(ROOT), url))

    for src, url in broken:
        print(f"broken: {url}  (referenced by {src})")
    if broken:
        print(f"\n{len(broken)} broken of {checked} local references")
        return 1
    print(f"all {checked} local references resolve, across {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
