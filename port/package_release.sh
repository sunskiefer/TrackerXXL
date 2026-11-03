#!/usr/bin/env bash
# Package TrackerXXL as the real release zip (install.sh, uninstall.sh, ARMv7 build, skin, INSTALL.md, mpc-plugin.json).
#   port/package_release.sh <version> [release.py dir]       (default release tools: mpc-vst-plugins @ 3845bea, the apostrophe-safe release.py)
# Run port/skin/gen/build_skin.py and port/platform/build.sh first. ARMv7 only; no desktop build exists.
set -euo pipefail
VER="${1:?version, e.g. 0.1.0}"
here="$(cd "$(dirname "$0")" && pwd)"
TOOLS="${2:-${MPC_RELEASE_TOOLS:?set MPC_RELEASE_TOOLS to the tools folder of sunskiefer/mpc-vst-plugins @ 3845bea (the apostrophe-safe release.py)}}"
PLAT="$here/platform"
NAME="L'Cronx - VST - TrackerXXL"          # the apostrophe lives only inside double quotes
PKG="$PLAT/build/pkg"
rm -rf "$PKG" && mkdir -p "$PKG/skin/$NAME" "$PKG/extra/TrackerXXL"
cp -r "$here/skin/build/Plugin Skins" "$PKG/skin/$NAME/"
cat > "$PKG/skin/$NAME/version.xml" <<'XML'
<?xml version='1.0' encoding='utf-8'?>
<plugincontent version="1.0">
	<identifier>L'Cronx.vst.trackerxxl</identifier>
	<version>1.0.0.0</version>
</plugincontent>
XML
for d in Projects Samples Themes Fonts; do mkdir -p "$PKG/extra/TrackerXXL/$d"; printf 'Put your %s here.\n' "$d" > "$PKG/extra/TrackerXXL/$d/README.txt"; done
python3 "$TOOLS/gen_vst.py" "$PLAT/build/full/vst.json" >/dev/null
cp "$PLAT/build/full/build/pluginlist-entry.xml" "$PKG/"
mkdir -p "$here/../dist"
python3 "$TOOLS/release.py" --so "$PLAT/build/arm/trackerxxl.so" --skin "$PKG/skin/$NAME" --entry "$PKG/pluginlist-entry.xml" \
  --version "$VER" \
  --about "TrackerXXL: the Polyend Tracker+ on Force and MPC, with the LXRXXL ZYNTH synth voices, LXRXXL BAZZLINE bass sounds and LXRXXL drums." \
  --repo sunskiefer/TrackerXXL --license LicenseRef-LXR02-Open \
  --extra "$PKG/extra/TrackerXXL:TrackerXXL" --user-data TrackerXXL -o "$here/../dist"
ls -l "$here/../dist"
