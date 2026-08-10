#!/usr/bin/env python3
"""Stamp the released version and date into the docs site.

Values in the docs site that go stale on their own, and nothing else keeps
honest:
  docs/index.html   the JSON-LD softwareVersion and dateModified
  docs/sitemap.xml  the lastmod of each page
  docs/llms.txt     the "Current release is X.Y.Z (date)" line
  CITATION.cff      the version and date-released

Two dates, not one. They drifted apart the moment the site started changing
between releases:

--date is when the software was released. It is what dateModified on the
SoftwareApplication node means, and what the llms.txt release line states.
Stamping today's date into either would claim a release that never happened.
It defaults to the release date already in the files, so a manual run cannot
falsify it. The workflow passes the tag's published_at.

--page-date is when the pages themselves last changed, which is all that
sitemap lastmod means. It defaults to today (UTC), so editing the site and
running this leaves an honest freshness signal without touching the release.

Version defaults to CMakeLists.txt, the same place the release workflow reads
it, so the page can never claim a version that was never built.

Run from anywhere:
  python3 packaging/stamp-docs.py [--version X.Y.Z] [--date YYYY-MM-DD]
                                  [--page-date YYYY-MM-DD] [--check]

--check writes nothing and exits 1 if anything is out of date, which is what
makes this usable as a CI guard as well as a fixer. Exits 0 with no changes
when the files are already correct, so the workflow can commit conditionally.
"""

import argparse
import datetime
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKE = os.path.join(ROOT, "CMakeLists.txt")
INDEX = os.path.join(ROOT, "docs", "index.html")
SITEMAP = os.path.join(ROOT, "docs", "sitemap.xml")
LLMS = os.path.join(ROOT, "docs", "llms.txt")
CITATION = os.path.join(ROOT, "CITATION.cff")


def released_date():
    """The release date already stamped in the site, so a run cannot invent one."""
    text = open(LLMS, encoding="utf-8").read()
    m = re.search(r"Current release is \d+\.\d+\.\d+ \((\d{4}-\d{2}-\d{2})\)", text)
    if not m:
        sys.exit(f"no 'Current release is X.Y.Z (date)' line in {LLMS}, pass --date")
    return m.group(1)


def iso_date(value, flag):
    try:
        datetime.date.fromisoformat(value)
    except ValueError:
        sys.exit(f"{flag} must be YYYY-MM-DD, got {value}")
    return value


def project_version():
    text = open(CMAKE, encoding="utf-8").read()
    m = re.search(r"^project\([^)]*?VERSION\s+(\d+\.\d+\.\d+)", text, re.M | re.S)
    if not m:
        sys.exit(f"no project(... VERSION x.y.z) found in {CMAKE}")
    return m.group(1)


def substitute(path, rules):
    """Apply (pattern, replacement) rules, erroring if a pattern matches nothing."""
    original = open(path, encoding="utf-8").read()
    text = original
    for pattern, replacement in rules:
        text, n = re.subn(pattern, replacement, text)
        if n == 0:
            sys.exit(f"pattern not found in {os.path.relpath(path, ROOT)}: {pattern}")
    return original, text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", help="version to stamp, defaults to the CMake project version")
    ap.add_argument("--date", help="release date, defaults to the one already stamped")
    ap.add_argument("--page-date", help="date the pages last changed, defaults to today (UTC)")
    ap.add_argument("--check", action="store_true", help="report drift, write nothing")
    args = ap.parse_args()

    if args.version and not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        sys.exit(f"--version must be X.Y.Z, got {args.version}")
    version = args.version or project_version()
    date = iso_date(args.date, "--date") if args.date else released_date()
    today = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    page_date = iso_date(args.page_date, "--page-date") if args.page_date else today

    targets = [
        (
            INDEX,
            [
                (r'("softwareVersion":\s*")[^"]*(")', rf"\g<1>{version}\g<2>"),
                (r'("dateModified":\s*")[^"]*(")', rf"\g<1>{date}\g<2>"),
            ],
        ),
        (
            SITEMAP,
            [
                # Every page, since a shared stylesheet or nav change touches all
                # of them. lastmod is a freshness hint, not a per byte audit.
                (r"(<lastmod>)[^<]*(</lastmod>)", rf"\g<1>{page_date}\g<2>"),
            ],
        ),
        (
            CITATION,
            [
                (r"(?m)^(version:\s*)\d+\.\d+\.\d+", rf"\g<1>{version}"),
                (r'(?m)^(date-released:\s*")\d{4}-\d{2}-\d{2}(")', rf"\g<1>{date}\g<2>"),
            ],
        ),
        (
            LLMS,
            [
                (
                    r"(Current release is )\d+\.\d+\.\d+ \(\d{4}-\d{2}-\d{2}\)",
                    rf"\g<1>{version} ({date})",
                ),
            ],
        ),
    ]

    stale = []
    for path, rules in targets:
        original, text = substitute(path, rules)
        if original == text:
            continue
        stale.append(os.path.relpath(path, ROOT))
        if not args.check:
            open(path, "w", encoding="utf-8").write(text)

    summary = f"{version} released {date}, pages {page_date}"
    if not stale:
        print(f"docs already stamped at {summary}")
        return 0
    verb = "stale" if args.check else "stamped"
    print(f"{verb} at {summary}: {', '.join(stale)}")
    return 1 if args.check else 0


if __name__ == "__main__":
    sys.exit(main())
