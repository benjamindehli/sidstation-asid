#!/usr/bin/env python3
"""Stamp the released version and the page dates into the docs site.

Values in the docs site that go stale on their own, and nothing else keeps
honest:
  docs/index.html      the JSON-LD softwareVersion and dateModified, and the
                       download section's version, file names and asset URLs
  docs/*/index.html    the JSON-LD dateModified of each article page
  docs/sitemap.xml     the lastmod of each page
  docs/llms.txt        the "Current release is X.Y.Z (date)" line
  CITATION.cff         the version and date-released

The download section is the reason this script matters more than it used to.
Every release asset carries the version in its own file name, so there is no
/releases/latest/download/ URL that stays correct: the page has to name
SidStation-ASID-1.2.0.dmg and the v1.2.0 tag outright. Stamped from the same
version as everything else, those links move with the release. Left to a human
they would be three silent 404s on the one page that matters.

Two kinds of date, because they mean different things:

The release date is when the software shipped. It is what dateModified on the
SoftwareApplication node means, and what the llms.txt release line and
CITATION.cff state. Stamping today's date into any of those would claim a
release that never happened, so --date defaults to the release date already in
the files and a manual run cannot falsify it. The workflow passes the tag's
published_at.

The page dates are when each page last changed, which is all that sitemap
lastmod and an article's dateModified mean. Those come from git rather than
from a flag, one date per page, because that is the only source that stays
right when the site is edited between releases. It did not before: 1.2.0
shipped on 2026-08-12, the pages were edited on the 19th and the 27th, and
every date on the site still said the 12th, because the stamper only ran on
release and only ever dated the landing page.

A page is more than its own file, so its date is the newest of:
  the page's own file
  the stylesheet, the nav script and the pixel font, which every page pulls in
  the screenshots, for the landing page, which is the only page that shows them

Uncommitted edits to a page count as today, so editing the site and stamping it
in the same pass gives the right dates in one commit rather than needing a
second one after. A page that differs from HEAD only in the dates stamped here
does not count as edited, which is what makes a second run a no-op instead of
walking every date forward to today. Commits made by this script are skipped
for the same reason on the committed side.

Full history is required. On a shallow clone every path resolves to the same
commit, so every date would collapse to HEAD's, which looks plausible and is
wrong. Both workflows that run this set fetch-depth: 0 for that reason.

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
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKE = os.path.join(ROOT, "CMakeLists.txt")
DOCS = os.path.join(ROOT, "docs")
INDEX = os.path.join(DOCS, "index.html")
SITEMAP = os.path.join(DOCS, "sitemap.xml")
LLMS = os.path.join(DOCS, "llms.txt")
CITATION = os.path.join(ROOT, "CITATION.cff")

# The site's own address, used to turn a sitemap <loc> back into the file that
# serves it. The sitemap is the list of pages, so there is no second list here
# to fall out of step with it.
BASE_URL = "https://benjamindehli.github.io/sidstation-asid/"

# Pulled in by every page, so a change to any of them changes how all of them
# render and read. Listed rather than parsed out of the HTML: three files that
# every page links is not worth a parser, and a parser that missed one would
# quietly backdate a page instead of failing.
SHARED = (
    "docs/css/docs.css",
    "docs/js/nav.js",
    "docs/assets/fonts/SidStationC64.woff2",
)

# Page content that lives outside the page's own file. Only the landing page
# carries screenshots, and regenerating them changes what that page shows.
EXTRA = {"docs/index.html": ("docs/assets/screenshots",)}

# The commit subject prefix this script's own commits use, from the release
# workflow. Skipped when dating a page: a stamp commit moves the date lines and
# nothing else, so counting it would leave every release looking like an edit
# and fail the next --check on the bot's own push.
STAMP_SUBJECT = "^Stamp "

# Dated JSON-LD node per page. The landing page's dateModified belongs to the
# SoftwareApplication, which is the software's date and not the page's, so it is
# stamped from --date with the rest of the release values. Every other page's
# belongs to a TechArticle, which is the page's own date.
RELEASE_DATED = "docs/index.html"


def git(*args, strip=True, allow_fail=False):
    """Run git in the repo and return its stdout.

    allow_fail returns None instead of exiting, for the questions where a
    non-zero exit is itself the answer, such as asking HEAD for a file that was
    only just added.
    """
    proc = subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        if allow_fail:
            return None
        sys.exit(f"git {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout.strip() if strip else proc.stdout


def require_full_history():
    """Refuse to date anything from a shallow clone, where every path is HEAD."""
    if git("rev-parse", "--is-shallow-repository") == "true":
        sys.exit(
            "this is a shallow clone, so every page would be dated to the last\n"
            "commit. Get the full history first:\n"
            "  git fetch --unshallow\n"
            "In Actions, set fetch-depth: 0 on actions/checkout."
        )


# The values this script writes into a page. A page it has already stamped
# differs from HEAD only in these, which is not a change a reader would ever
# see. They are blanked before comparing, because counting them would make
# stamping a page look like editing it: the run after would date it today, and
# the run after that, forever.
STAMPED_VALUES = (
    r'("dateModified":\s*")[^"]*(")',
    r'("softwareVersion":\s*")[^"]*(")',
)


def without_stamps(text):
    for pattern in STAMPED_VALUES:
        text = re.sub(pattern, r"\g<1>\g<2>", text)
    return text


def stamped_only(path):
    """True if path differs from HEAD in nothing but the values stamped here."""
    full = os.path.join(ROOT, path)
    # Only pages carry stamped values, so any change to the stylesheet, the
    # script, the font or a screenshot is a real one.
    if not path.endswith(".html") or not os.path.isfile(full):
        return False
    head = git("show", f"HEAD:{path}", strip=False, allow_fail=True)
    if head is None:
        return False  # not in HEAD, so the whole file is new
    return without_stamps(head) == without_stamps(open(full, encoding="utf-8").read())


def uncommitted(relpath):
    """Paths under relpath that differ from HEAD or are not tracked at all.

    Two commands rather than `status --porcelain`, whose two column status
    prefix has to be sliced off each line. These print bare paths, so there is
    no column to get wrong, and getting it wrong is quiet: it mangles the path,
    which then looks like a file that is not there.
    """
    changed = git("diff", "--name-only", "HEAD", "--", relpath).splitlines()
    others = git("ls-files", "--others", "--exclude-standard", "--", relpath).splitlines()
    return [p for p in changed + others if p]


def edited_now(relpath):
    """True if relpath has uncommitted changes that are not this script's own."""
    return any(not stamped_only(path) for path in uncommitted(relpath))


def last_changed(relpath, today):
    """The date relpath last changed, as YYYY-MM-DD."""
    if edited_now(relpath):
        return today
    dated = git(
        "log", "-1", "--format=%cs", "--invert-grep", f"--grep={STAMP_SUBJECT}", "--", relpath
    )
    # Falls through for a path only ever touched by stamp commits, and then for
    # one git has never seen, which is a file added but not yet committed.
    return dated or git("log", "-1", "--format=%cs", "--", relpath) or today


def page_date(relpath, today):
    """The newest date among the page and everything it shows, YYYY-MM-DD.

    ISO dates sort as strings, which is the whole reason for this format.
    """
    deps = (relpath,) + SHARED + EXTRA.get(relpath, ())
    return max(last_changed(dep, today) for dep in deps)


def sitemap_pages():
    """[(loc, relpath)] for every page in the sitemap, in document order."""
    text = open(SITEMAP, encoding="utf-8").read()
    pages = []
    for loc in re.findall(r"<loc>([^<]+)</loc>", text):
        if not loc.startswith(BASE_URL):
            sys.exit(f"{loc} in sitemap.xml is not under {BASE_URL}")
        relpath = os.path.join("docs", loc[len(BASE_URL) :], "index.html")
        if not os.path.exists(os.path.join(ROOT, relpath)):
            sys.exit(f"{loc} in sitemap.xml has no page at {relpath}")
        pages.append((loc, relpath))
    if not pages:
        sys.exit(f"no <loc> entries found in {SITEMAP}")
    return pages


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


def released_date():
    """The release date already stamped in the site, so a run cannot invent one."""
    text = open(LLMS, encoding="utf-8").read()
    m = re.search(r"Current release is \d+\.\d+\.\d+ \((\d{4}-\d{2}-\d{2})\)", text)
    if not m:
        sys.exit(f"no 'Current release is X.Y.Z (date)' line in {LLMS}, pass --date")
    return m.group(1)


def substitute(text, rules, where):
    """Apply (pattern, replacement) rules, erroring if a pattern matches nothing."""
    for pattern, replacement in rules:
        text, n = re.subn(pattern, replacement, text)
        if n == 0:
            sys.exit(f"pattern not found in {where}: {pattern}")
    return text


# Only the date is replaced, so whatever time and UTC offset a value carries is
# left alone. Replacing the whole value would flatten a full timestamp down to a
# bare date on the first run, which is what the old single pattern did.
DATE_ONLY = r'("{key}":\s*")\d{{4}}-\d{{2}}-\d{{2}}(T[^"]*"|")'


def date_rule(key, date):
    return (DATE_ONLY.format(key=key), rf"\g<1>{date}\g<2>")


def stamp_sitemap(text, dates):
    """Set each <url> block's lastmod from the date of the page it points at."""
    seen = []

    def one_url(match):
        block = match.group(0)
        loc = re.search(r"<loc>([^<]+)</loc>", block)
        if not loc:
            sys.exit("a <url> block in sitemap.xml has no <loc>")
        if "<lastmod>" not in block:
            sys.exit(f"the <url> block for {loc.group(1)} has no <lastmod>")
        seen.append(loc.group(1))
        return re.sub(r"(<lastmod>)[^<]*(</lastmod>)", rf"\g<1>{dates[loc.group(1)]}\g<2>", block)

    text = re.sub(r"<url>.*?</url>", one_url, text, flags=re.S)
    missing = set(dates) - set(seen)
    if missing:
        sys.exit(f"sitemap.xml <loc> outside a <url> block: {', '.join(sorted(missing))}")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", help="version to stamp, defaults to the CMake project version")
    ap.add_argument("--date", help="release date, defaults to the one already stamped")
    ap.add_argument("--page-date", help="date for uncommitted edits, defaults to today (UTC)")
    ap.add_argument("--check", action="store_true", help="report drift, write nothing")
    args = ap.parse_args()

    if args.version and not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        sys.exit(f"--version must be X.Y.Z, got {args.version}")
    version = args.version or project_version()
    date = iso_date(args.date, "--date") if args.date else released_date()
    today = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    page_today = iso_date(args.page_date, "--page-date") if args.page_date else today

    require_full_history()
    pages = sitemap_pages()
    dates = {loc: page_date(relpath, page_today) for loc, relpath in pages}

    targets = [
        (
            INDEX,
            [
                (r'("softwareVersion":\s*")[^"]*(")', rf"\g<1>{version}\g<2>"),
                date_rule("dateModified", date),
                # The download section, which names the release outright: the
                # visible version, then the tag in each asset URL, then the file
                # names, which appear twice each (once as the link target, once
                # as the code element under it). Each of these replaces every
                # occurrence, so adding a fourth platform needs no change here.
                (r"(Version )\d+\.\d+\.\d+", rf"\g<1>{version}"),
                (r"(releases/download/v)\d+\.\d+\.\d+", rf"\g<1>{version}"),
                (r"(SidStation-ASID-)\d+\.\d+\.\d+", rf"\g<1>{version}"),
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
    # Every page but the landing one carries its own date, in a TechArticle.
    for loc, relpath in pages:
        if relpath != RELEASE_DATED:
            targets.append((os.path.join(ROOT, relpath), [date_rule("dateModified", dates[loc])]))

    stale = []
    for path, rules in targets:
        original = open(path, encoding="utf-8").read()
        text = substitute(original, rules, os.path.relpath(path, ROOT))
        if original == text:
            continue
        stale.append(os.path.relpath(path, ROOT))
        if not args.check:
            open(path, "w", encoding="utf-8").write(text)

    original = open(SITEMAP, encoding="utf-8").read()
    text = stamp_sitemap(original, dates)
    if original != text:
        stale.append(os.path.relpath(SITEMAP, ROOT))
        if not args.check:
            open(SITEMAP, "w", encoding="utf-8").write(text)

    summary = f"{version} released {date}"
    if not stale:
        print(f"docs already stamped at {summary}")
        return 0
    if args.check:
        print(f"stale at {summary}: {', '.join(stale)}")
        print("run `make stamp` and commit the result")
        return 1
    print(f"stamped at {summary}: {', '.join(stale)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
