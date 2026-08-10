#!/usr/bin/env python3
"""Stamp the released version and date into the docs site.

Four values in the docs site go stale the moment a release goes out and
nothing else keeps them honest:
  docs/index.html   the JSON-LD softwareVersion and dateModified
  docs/sitemap.xml  the lastmod
  docs/llms.txt     the "Current release is X.Y.Z (date)" line

Version defaults to CMakeLists.txt, the same place the release workflow reads
it, so the page can never claim a version that was never built. Date defaults
to today (UTC), which is the release date when this runs from the workflow.
The workflow passes both explicitly, since it stamps main with the version it
validated against the tag, and main may have moved on since.

Run from anywhere:  python3 packaging/stamp-docs.py [--version X.Y.Z] [--date YYYY-MM-DD] [--check]

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
    ap.add_argument("--date", help="ISO date to stamp, defaults to today (UTC)")
    ap.add_argument("--check", action="store_true", help="report drift, write nothing")
    args = ap.parse_args()

    if args.version and not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        sys.exit(f"--version must be X.Y.Z, got {args.version}")
    version = args.version or project_version()
    if args.date:
        try:
            datetime.date.fromisoformat(args.date)
        except ValueError:
            sys.exit(f"--date must be YYYY-MM-DD, got {args.date}")
        date = args.date
    else:
        date = datetime.datetime.now(datetime.timezone.utc).date().isoformat()

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
                (r"(<lastmod>)[^<]*(</lastmod>)", rf"\g<1>{date}\g<2>"),
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

    if not stale:
        print(f"docs already stamped at {version} / {date}")
        return 0
    verb = "stale" if args.check else "stamped"
    print(f"{verb} at {version} / {date}: {', '.join(stale)}")
    return 1 if args.check else 0


if __name__ == "__main__":
    sys.exit(main())
