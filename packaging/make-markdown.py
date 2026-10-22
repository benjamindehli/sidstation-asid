#!/usr/bin/env python3
"""Write a Markdown copy of every docs page, for AI tools that read Markdown.

Run from anywhere:  python3 packaging/make-markdown.py [--check]

Each page in docs/sitemap.xml gets an index.md next to its index.html, holding
the page's title and tagline from the header and everything in <main>. Coding
assistants and agents fetch documentation as they work and do better with
Markdown than with a page full of layout, which is the audience this is for.
The protocol and ASID timing pages are the ones most likely to be fetched that
way, by someone writing their own ASID tooling. Search and answer engines read
the HTML, and nothing about the HTML changes here.

The copies are generated rather than written, because a second hand kept copy
of every page would drift, and this site already has checks because of drift
exactly like that. The HTML stays the only thing anyone edits. --check writes
nothing and exits 1 when a copy is missing or out of date, which is what CI
runs. The release workflow regenerates them after stamping, because the
download section in <main> names the version.

Links become absolute URLs on the published site. A Markdown file is often read
somewhere other than where it is served, and a relative link means nothing
there. Images keep their alt text and point at the file the page's <img> uses.

Every page also has to carry a <link rel="alternate" type="text/markdown"> to
its copy, which is how a tool that has the HTML finds the Markdown. A page
without one stops the run, so a new page cannot quietly be left out.

Standard library only, on purpose: the pages use a small, fixed set of elements,
and a converter for exactly those is shorter than pinning one more dependency.
An element outside that set stops the run rather than being dropped, so a page
that grows a new kind of markup fails here instead of losing content.
"""

import argparse
import re
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
SITEMAP = DOCS / "sitemap.xml"
BASE_URL = "https://benjamindehli.github.io/sidstation-asid/"

ALTERNATE = re.compile(r'<link rel="alternate" type="text/markdown" href="index\.md" />')

# Elements that hold other blocks and contribute nothing of their own.
CONTAINERS = {"main", "header", "section", "div", "picture"}
# Elements whose content is read as running text.
INLINE = {"a", "strong", "em", "code", "span"}
# Elements that carry nothing a reader of the Markdown needs: the "On this page"
# list repeats the headings, and the rest is layout or decoration.
SKIPPED = {"source", "svg", "path", "button"}
VOID = {"img", "source", "br", "hr", "meta", "link", "input"}
KNOWN = (
    CONTAINERS
    | INLINE
    | SKIPPED
    | {"h2", "h3", "p", "ul", "ol", "li", "pre", "table", "tr", "th", "td", "img", "nav"}
)


class Node:
    def __init__(self, tag, attrs, parent):
        self.tag = tag
        self.attrs = dict(attrs)
        self.parent = parent
        self.children = []

    def classes(self):
        return self.attrs.get("class", "").split()

    def find_all(self, tag):
        for child in self.children:
            if isinstance(child, Node):
                if child.tag == tag:
                    yield child
                yield from child.find_all(tag)


class TreeBuilder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("root", {}, None)
        self.current = self.root

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs, self.current)
        self.current.children.append(node)
        if tag not in VOID:
            self.current = node

    def handle_startendtag(self, tag, attrs):
        self.current.children.append(Node(tag, attrs, self.current))

    def handle_endtag(self, tag):
        node = self.current
        while node is not None and node.tag != tag:
            node = node.parent
        if node is not None and node.parent is not None:
            self.current = node.parent

    def handle_data(self, data):
        self.current.children.append(data)


def escape(text):
    # An underscore inside a word, as in x86_64, is literal in Markdown already.
    text = re.sub(r"([\\`*\[\]<])", r"\\\1", text)
    return re.sub(r"(?<!\w)_|_(?!\w)", r"\\_", text)


class Renderer:
    def __init__(self, page_url, source):
        self.page_url = page_url
        self.source = source

    def fail(self, message):
        sys.exit(f"{self.source}: {message}")

    def inline(self, node, raw=False):
        out = []
        for child in node.children:
            if isinstance(child, str):
                out.append(child if raw else escape(child))
                continue
            tag = child.tag
            if tag not in KNOWN:
                self.fail(f"<{tag}> is not handled, add it to make-markdown.py")
            if tag in SKIPPED:
                continue
            if tag == "code":
                out.append("`" + self.inline(child, raw=True) + "`")
            elif tag == "strong":
                out.append("**" + self.inline(child).strip() + "**")
            elif tag == "em":
                out.append("*" + self.inline(child).strip() + "*")
            elif tag == "a":
                text = self.inline(child).strip()
                out.append(f"[{text}]({urljoin(self.page_url, child.attrs['href'])})")
            elif tag == "span":
                out.append(self.inline(child))
            else:
                self.fail(f"<{tag}> inside running text is not handled")
        return "".join(out)

    def text(self, node):
        return re.sub(r"\s+", " ", self.inline(node)).strip()

    def image(self, img):
        src = img.attrs["src"]
        alt = escape(img.attrs.get("alt", ""))
        return f"![{alt}]({urljoin(self.page_url, src)})"

    def blocks(self, node):
        out = []
        pending = Node("p", {}, node)

        def flush():
            if any(isinstance(c, Node) or c.strip() for c in pending.children):
                out.append(self.text(pending))
            pending.children.clear()

        for child in node.children:
            if isinstance(child, str) or child.tag in INLINE:
                pending.children.append(child)
                continue
            flush()
            out.extend(self.block(child))
        flush()
        return out

    def block(self, node):
        tag = node.tag
        if tag not in KNOWN:
            self.fail(f"<{tag}> is not handled, add it to make-markdown.py")
        if tag in SKIPPED or (tag == "nav" and "toc" in node.classes()):
            return []
        if tag in CONTAINERS:
            return self.blocks(node)
        if tag == "h2":
            return ["## " + self.text(node)]
        if tag == "h3":
            return ["### " + self.text(node)]
        if tag == "p":
            return [self.text(node)]
        if tag == "img":
            return [self.image(node)]
        if tag in ("ul", "ol"):
            items = [c for c in node.children if isinstance(c, Node)]
            lines = []
            for n, item in enumerate(items, 1):
                if item.tag != "li":
                    self.fail(f"<{item.tag}> directly inside <{tag}>")
                marker = f"{n}." if tag == "ol" else "-"
                lines.append(f"{marker} {self.text(item)}")
            return ["\n".join(lines)]
        if tag == "pre":
            code = self.inline(node, raw=True).strip("\n")
            return ["```\n" + code + "\n```"]
        if tag == "table":
            rows = []
            for tr in node.find_all("tr"):
                cells = [
                    self.text(c).replace("|", "\\|") for c in tr.children if isinstance(c, Node)
                ]
                rows.append("| " + " | ".join(cells) + " |")
                if len(rows) == 1:
                    rows.append("|" + " --- |" * len(cells))
            return ["\n".join(rows)]
        self.fail(f"<{tag}> outside running text is not handled")


def pages():
    """(page URL, HTML path) for every page in the sitemap."""
    text = SITEMAP.read_text(encoding="utf-8")
    result = []
    for loc in re.findall(r"<loc>([^<]+)</loc>", text):
        if not loc.startswith(BASE_URL):
            sys.exit(f"sitemap.xml has a page outside {BASE_URL}: {loc}")
        result.append((loc, DOCS / loc[len(BASE_URL) :] / "index.html"))
    return result


def convert(url, path):
    source = path.relative_to(ROOT).as_posix()
    html = path.read_text(encoding="utf-8")
    if not ALTERNATE.search(html):
        sys.exit(f'{source} has no <link rel="alternate" type="text/markdown" href="index.md" />')

    builder = TreeBuilder()
    builder.feed(html)
    renderer = Renderer(url, source)

    h1 = next(builder.root.find_all("h1"), None)
    main = next(builder.root.find_all("main"), None)
    if h1 is None or main is None:
        sys.exit(f"{source} needs an <h1> and a <main>")
    tag = next((p for p in builder.root.find_all("p") if "tag" in p.classes()), None)

    parts = [
        f"<!-- Generated from {source} by packaging/make-markdown.py. Edit the HTML, then run make markdown. -->",
        "# " + renderer.text(h1),
    ]
    if tag is not None:
        parts.append(renderer.text(tag))
    parts.append(f"Web page: {url}")
    parts.extend(renderer.blocks(main))
    return "\n\n".join(p for p in parts if p) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--check", action="store_true", help="report missing or stale copies, write nothing"
    )
    args = ap.parse_args()

    stale = []
    for url, path in pages():
        target = path.with_name("index.md")
        text = convert(url, path)
        if target.exists() and target.read_text(encoding="utf-8") == text:
            continue
        stale.append(target.relative_to(ROOT).as_posix())
        if not args.check:
            target.write_text(text, encoding="utf-8")

    if not stale:
        print("Markdown copies are up to date")
        return 0
    if args.check:
        print(f"stale: {', '.join(stale)}")
        print("run `make markdown` and commit the result")
        return 1
    print(f"wrote {', '.join(stale)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
