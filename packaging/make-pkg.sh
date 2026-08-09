#!/usr/bin/env bash
# Build a macOS installer package for SidStation ASID. It installs the AU and
# VST3 into the system plug-in folders and the Standalone into /Applications.
#
#   make-pkg.sh <version> <artefacts-dir> <output.pkg>
#
# <artefacts-dir> is the JUCE Release artefacts folder, i.e. it contains
# AU/, VST3/ and Standalone/ subfolders. If INSTALLER_IDENTITY is set in the
# environment (a "Developer ID Installer" identity), the product is signed.
set -euo pipefail

VERSION="${1:?usage: make-pkg.sh <version> <artefacts-dir> <output.pkg>}"
ART="${2:?missing artefacts dir}"
OUT="${3:?missing output path}"

NAME="SidStation ASID"
IDENTIFIER="com.dehlimusikk.sidstationasid"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"  # repo root (for the LICENSE)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Lay the three bundles out at their real install destinations under a staging
# root, then build one component package rooted at "/".
staging="$work/staging"
mkdir -p "$staging/Library/Audio/Plug-Ins/Components" \
         "$staging/Library/Audio/Plug-Ins/VST3" \
         "$staging/Applications"
cp -R "$ART/AU/$NAME.component"   "$staging/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/VST3/$NAME.vst3"      "$staging/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/Standalone/$NAME.app" "$staging/Applications/"

pkgbuild --root "$staging" \
    --identifier "$IDENTIFIER" \
    --version "$VERSION" \
    --install-location "/" \
    "$work/component.pkg"

# Wrap it in a product archive so the installer shows a title and the licence.
cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>SidStation ASID $VERSION</title>
    <license file="LICENSE.txt"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <choices-outline>
        <line choice="default"/>
    </choices-outline>
    <choice id="default">
        <pkg-ref id="$IDENTIFIER"/>
    </choice>
    <pkg-ref id="$IDENTIFIER" version="$VERSION" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
XML

resources="$work/resources"
mkdir -p "$resources"
cp "$ROOT/LICENSE" "$resources/LICENSE.txt"

sign=()
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
    sign=(--sign "$INSTALLER_IDENTITY")
fi

productbuild \
    --distribution "$work/distribution.xml" \
    --package-path "$work" \
    --resources "$resources" \
    "${sign[@]}" \
    "$OUT"

echo "Built $OUT"
