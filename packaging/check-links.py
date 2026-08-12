#!/usr/bin/env python3
"""Check that every local link in the docs site and the Markdown resolves.

Run from anywhere:  python3 packaging/check-links.py

Walks the HTML in docs/ and the Markdown in the repository, collects every href,
src and srcset candidate, and resolves each one relative to the file it appears
in. Exits 1 listing anything that does not exist.

This exists because the site has pages at two different depths: index.html sits
at the site root, docs/protocol/index.html one level down, and the stylesheet
refers to the font from a third. The same asset therefore has a different
correct path in each file, which has already produced one 404 that looked fine
in review and only showed up in production. External URLs are not fetched, so
this stays fast and offline: it catches the mistake that is easy to make, not
link rot.

The Markdown is here because the README embeds the same generated screenshots
the site does, and the widths in packaging/make-screenshots.py are tied to the
site's layout. Changing .wrap changes which files exist, and images-check will
not notice: it compares the generated set against its sources, and knows nothing
about who points at it. That combination has already broken the README's
screenshots once.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"

# GitHub Pages publishes docs/ at this path, so a root-absolute URL resolves
# against it rather than against the file the URL sits in. 404.html is written
# entirely in these on purpose: it is served for any missing path, so a relative
# URL there would resolve against whatever path was missed.
SITE_PREFIX = "/sidstation-asid/"

# Directories with nothing of ours in them: installed tooling, build output, the
# vendored JUCE checkout and the copyrighted manual working directory.
SKIP_DIRS = {"node_modules", "build", ".venv", "temp", ".git"}

# href/src on any element, plus every candidate inside a srcset.
ATTR = re.compile(r'(?:href|src)="([^"]+)"')
SRCSET = re.compile(r'srcset="([^"]+)"')
CSS_URL = re.compile(r'url\("?([^")]+)"?\)')
# [text](target) and ![alt](target), with an optional "title" after the target.
MD_LINK = re.compile(r'!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')


def is_external(url):
    return url.startswith(("http://", "https://", "//", "mailto:", "data:", "#"))


def references(path):
    """Every local URL referenced by a file, with the fragment stripped."""
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".md":
        found = set(MD_LINK.findall(text))
    else:
        found = set(ATTR.findall(text)) | set(CSS_URL.findall(text))
        for group in SRCSET.findall(text):
            for candidate in group.split(","):
                parts = candidate.split()
                if parts:
                    found.add(parts[0])
    return {u.split("#")[0] for u in found if not is_external(u) and u.split("#")[0]}


def resolve(f, url):
    """The file a URL points at, or None if it leaves the published site."""
    if url.startswith("/"):
        if not url.startswith(SITE_PREFIX):
            return None
        return DOCS / url[len(SITE_PREFIX) :]
    return f.parent / url


def sources():
    """The files whose links are checked, HTML and CSS first, then Markdown."""
    files = sorted(DOCS.rglob("*.html")) + sorted(DOCS.rglob("*.css"))
    files += sorted(p for p in ROOT.rglob("*.md") if not SKIP_DIRS & set(p.relative_to(ROOT).parts))
    return files


def main():
    files = sources()
    if not files:
        sys.exit(f"no HTML, CSS or Markdown found under {ROOT}")

    broken = []
    checked = 0
    for f in files:
        for url in sorted(references(f)):
            checked += 1
            found = resolve(f, url)
            if found is None:
                broken.append((f.relative_to(ROOT), url))
                continue
            target = found.resolve()
            # A directory URL such as protocol/ is served by its index.html. In
            # Markdown a link to a directory is a link to the directory, which
            # is what GitHub renders it as, so there it stands on its own.
            if target.is_dir() and f.suffix != ".md":
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
