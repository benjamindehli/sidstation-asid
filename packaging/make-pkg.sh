#!/usr/bin/env bash
# Build a macOS installer package for SidStation ASID. It installs the AU and
# VST3 into the system plug-in folders and the Standalone into /Applications.
#
#   make-pkg.sh <version> <artefacts-dir> <output.pkg>
#
# <artefacts-dir> is the JUCE Release artefacts folder, i.e. it contains
# AU/, VST3/ and Standalone/ subfolders. If INSTALLER_IDENTITY is set in the
# environment (a "Developer ID Installer" identity), the product is signed.
#
# The AU, VST3 and Standalone all share one CFBundleIdentifier. The macOS
# installer keys bundle handling on that id, so a single package holding all
# three collapses them into one and installs only some. To avoid that, each
# bundle goes in its own component package (distinct package id + install
# location), and productbuild combines them.
set -euo pipefail

VERSION="${1:?usage: make-pkg.sh <version> <artefacts-dir> <output.pkg>}"
ART="${2:?missing artefacts dir}"
OUT="${3:?missing output path}"

NAME="SidStation ASID"
IDBASE="com.dehlimusikk.sidstationasid"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"  # repo root (for the LICENSE)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Build one component package for a single bundle, non-relocatable, installing to
# a fixed location. Args: <bundle path> <install location> <pkg id> <out pkg>.
build_component() {
    local src="$1" loc="$2" id="$3" out="$4"
    local root="$work/root-$id"
    mkdir -p "$root"
    cp -R "$src" "$root/"
    local plist="$work/plist-$id.plist"
    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null; do
        i=$((i + 1))
    done
    pkgbuild --root "$root" --component-plist "$plist" \
        --identifier "$id" --version "$VERSION" --install-location "$loc" "$out"
}

build_component "$ART/AU/$NAME.component"   "/Library/Audio/Plug-Ins/Components" "$IDBASE.au"   "$work/au.pkg"
build_component "$ART/VST3/$NAME.vst3"      "/Library/Audio/Plug-Ins/VST3"       "$IDBASE.vst3" "$work/vst3.pkg"
build_component "$ART/Standalone/$NAME.app" "/Applications"                      "$IDBASE.app"  "$work/app.pkg"

# Combine the three into a product archive that shows a title and the licence.
cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>SidStation ASID $VERSION</title>
    <license file="LICENSE.txt"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <choices-outline>
        <line choice="default"/>
    </choices-outline>
    <choice id="default" title="SidStation ASID">
        <pkg-ref id="$IDBASE.au"/>
        <pkg-ref id="$IDBASE.vst3"/>
        <pkg-ref id="$IDBASE.app"/>
    </choice>
    <pkg-ref id="$IDBASE.au" version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$IDBASE.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$IDBASE.app" version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
XML

resources="$work/resources"
mkdir -p "$resources"
cp "$ROOT/LICENSE" "$resources/LICENSE.txt"

# Sign the product when an installer identity is given, otherwise build an
# unsigned package (an if/else avoids expanding an empty array, which errors
# under `set -u` on the macOS system bash 3.2).
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
    productbuild --distribution "$work/distribution.xml" --package-path "$work" \
        --resources "$resources" --sign "$INSTALLER_IDENTITY" "$OUT"
else
    echo "make-pkg: no INSTALLER_IDENTITY set, building an UNSIGNED package" >&2
    productbuild --distribution "$work/distribution.xml" --package-path "$work" \
        --resources "$resources" "$OUT"
fi

test -f "$OUT" || { echo "make-pkg: productbuild did not create $OUT" >&2; exit 1; }
echo "Built $OUT"
