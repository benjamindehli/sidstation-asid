#!/usr/bin/env python3
"""Build the responsive screenshot set for the docs site.

Reads the full resolution PNGs in assets/screenshots/ and writes AVIF and WebP
derivatives at several widths into docs/assets/screenshots/, which is what the
<picture> elements in docs/index.html point at.

Run from anywhere:  python3 packaging/make-screenshots.py [--check] [--force]
Needs:              pip install -r requirements-dev.txt

--check writes nothing and exits 1 if any derivative is missing or older than
its source, which makes this usable as a CI guard. --force re-encodes even when
everything is up to date, which is what you want after changing QUALITY.

Two formats, not three. The <img> fallback is WebP rather than JPEG because the
site already served WebP to every visitor before this existed, and a JPEG tier
would add roughly 280K per screenshot to the repo to serve the few percent of
browsers that have WebP but not AVIF. AVIF is the win worth having: at these
widths it lands 5-10% under WebP for the same visual quality, on flat UI panels
with hard edges.

The sources are the plugin window at its native 1444x1204, so 1444 is the
sharpest tier that exists. Widths wider than a given source are skipped rather
than upscaled, which means this list already has room in it: re-capture the
screenshots on a HiDPI display at 2888x2408 and the 1952 tier starts being
produced on the next run, with no change here. The <picture> markup would then
need 1952 adding to its srcset to actually serve it.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "assets/screenshots"
OUT_DIR = ROOT / "docs/assets/screenshots"

# 976 is the widest the image is ever displayed: .wrap in docs/css/docs.css is
# 1024px with 24px of padding either side, and .shot img is width 100%. 488 is
# that halved, for narrow phones. 1444 covers HiDPI displays as far as the
# source allows. See the note above about 1952.
#
# These track .wrap. Widen it again and the 1x tier has to follow, or a 1x
# display skips the tier meant for it and pulls the 1444 instead, because the
# sizes attribute asks for more pixels than the tier holds.
WIDTHS = [488, 976, 1444, 1952]

# Chosen by encoding this repo's screenshots and comparing against the source at
# 100% zoom. Below AVIF 60 the dot matrix LED font starts to smear.
QUALITY = {"AVIF": 60, "WEBP": 78}
EXT = {"AVIF": "avif", "WEBP": "webp"}


def targets(src):
    """Every (path, width, format) this source should produce."""
    with Image.open(src) as im:
        source_width = im.width
    for width in WIDTHS:
        if width > source_width:
            continue
        for fmt in QUALITY:
            yield OUT_DIR / f"{src.stem}_{width}.{EXT[fmt]}", width, fmt


def stale(src, out):
    return not out.exists() or out.stat().st_mtime < src.stat().st_mtime


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--check", action="store_true", help="report what is out of date, write nothing"
    )
    ap.add_argument("--force", action="store_true", help="re-encode even when up to date")
    args = ap.parse_args()

    sources = sorted(SRC_DIR.glob("*.png"))
    if not sources:
        sys.exit(f"no PNG sources in {SRC_DIR.relative_to(ROOT)}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    outdated = []
    for src in sources:
        resized = {}
        for out, width, fmt in targets(src):
            if not (args.force or stale(src, out)):
                continue
            outdated.append(out.relative_to(ROOT))
            if args.check:
                continue
            if width not in resized:
                with Image.open(src) as im:
                    height = round(width * im.height / im.width)
                    resized[width] = im.convert("RGB").resize((width, height), Image.LANCZOS)
            # method 6 is WebP's slowest, smallest setting; speed 4 is AVIF's
            # balance point. Encoding happens rarely, so favour file size.
            tuning = {"method": 6} if fmt == "WEBP" else {"speed": 4}
            resized[width].save(out, fmt, quality=QUALITY[fmt], **tuning)

    # Derivatives of a source that has been renamed or deleted would otherwise
    # sit in docs/ forever and get published.
    expected = {out for src in sources for out, _, _ in targets(src)}
    orphans = sorted(p.relative_to(ROOT) for p in OUT_DIR.iterdir() if p not in expected)

    if args.check:
        if outdated or orphans:
            for p in outdated:
                print(f"out of date: {p}")
            for p in orphans:
                print(f"orphaned:    {p}")
            print("run: make images")
            return 1
        print(f"screenshots up to date ({len(expected)} files from {len(sources)} sources)")
        return 0

    for p in orphans:
        (ROOT / p).unlink()
        print(f"removed orphan {p}")
    if outdated:
        print(f"wrote {len(outdated)} files from {len(sources)} sources")
    else:
        print("screenshots already up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
