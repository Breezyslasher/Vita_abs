#!/usr/bin/env bash
# Rename downloaded CI artifacts into the release-asset naming convention:
#
#     VitaABS.<FVER>-<platform suffix>
#
# This convention is a CONTRACT with the in-app updater
# (src/utils/app_update.cpp assetSuffix()): the updater picks the asset whose
# name ends with its platform's suffix, so any rename here must keep
# assetSuffix() in lockstep. Used by both the tagged-release and the
# pre-release upload jobs so their assets can never drift apart.
#
# Expects: $FVER set, cwd = directory containing the downloaded artifact dirs.
# Produces: ./release/ with the renamed files.
set -euo pipefail

: "${FVER:?FVER must be set}"

mkdir -p release

# Vita VPK  (suffix: .vpk)
for f in $(find . -type f -name "*.vpk" -not -name "*updater*"); do
    cp "$f" "release/VitaABS.${FVER}.vpk"
done

# Android APKs (Gradle names them VitaABS-{abi}.apk; suffix: -{abi}.apk)
for apk in $(find . -type f -name "*.apk"); do
    abi=$(basename "$apk" .apk | sed 's/VitaABS-//')
    [ -n "$abi" ] && cp "$apk" "release/VitaABS.${FVER}-${abi}.apk"
done

# Windows — zip each arch (exe + dlls + resources; suffix: -windows-{arch}.zip)
for dir in VitaABS-mingw-*; do
    [ -d "$dir" ] || continue
    arch=$(echo "$dir" | sed -E 's/VitaABS-mingw-([^-]+)-.*/\1/')
    (cd "$dir" && zip -r "../release/VitaABS.${FVER}-windows-${arch}.zip" .)
done

# Flatpak bundle (no updater suffix — Flathub is the update channel; the
# bundle ships for sideloading / testing).
#
# -type f matters here: downloading every artifact creates one DIRECTORY per
# artifact, named after it, and the flatpak-builder action publishes its own
# artifact called "VitaABS-x86_64.flatpak". Without -type f, find matched that
# directory and cp bailed with "-r not specified; omitting directory". Every
# find below is likewise restricted to regular files so an artifact directory
# can never be mistaken for a build output.
flatpak_bundle=$(find . -type f -name "*.flatpak" | head -1)
[ -n "$flatpak_bundle" ] && cp "$flatpak_bundle" "release/VitaABS.${FVER}.flatpak"

# AppImages: VitaABS-{arch}-{run}.AppImage (suffix contract: -{arch}.AppImage)
for f in $(find . -type f -name "VitaABS-*.AppImage" -not -name "appimagetool*"); do
    base=$(basename "$f")
    arch=$(echo "$base" | sed -E 's/VitaABS-(.+)-[0-9]+\.AppImage/\1/')
    cp "$f" "release/VitaABS.${FVER}-${arch}.AppImage"
done

# Switch NRO (suffix: -nx-{driver}.nro)
for dir in VitaABS-nx-*; do
    [ -d "$dir" ] || continue
    driver=$(echo "$dir" | sed -E 's/VitaABS-nx-([^-]+)-.*/\1/')
    nro=$(find "$dir" -type f -name "*.nro" | head -1)
    [ -n "$nro" ] && cp "$nro" "release/VitaABS.${FVER}-nx-${driver}.nro"
done

# PS4 pkg (suffix: -ps4.pkg). The build now also produces the bundled updater
# helper pkg (VABS00003) and stages updater.pkg — the release asset must be
# the MAIN title's pkg (VABS00002) only.
main_pkg=$(find . -type f -path "*ps4*" -name "*VABS00002*.pkg" | head -1)
if [ -z "$main_pkg" ]; then
    main_pkg=$(find . -type f -path "*ps4*" -name "*.pkg" \
        -not -name "*VABS00003*" -not -name "updater.pkg" | head -1)
fi
[ -n "$main_pkg" ] && cp "$main_pkg" "release/VitaABS.${FVER}-ps4.pkg"

# macOS DMG (suffix: -macOS-{Intel,Silicon}.dmg)
for dir in VitaABS-macos-*; do
    [ -d "$dir" ] || continue
    tag=$(echo "$dir" | sed -E 's/VitaABS-macos-([^-]+)-.*/\1/')
    dmg=$(find "$dir" -type f -name "*.dmg" | head -1)
    [ -n "$dmg" ] && cp "$dmg" "release/VitaABS.${FVER}-macOS-${tag}.dmg"
done

# Deb (suffix: -Linux_{arch}.deb)
for f in $(find . -type f -name "*.deb"); do
    arch=$(dpkg-deb -f "$f" Architecture 2>/dev/null || echo "amd64")
    cp "$f" "release/VitaABS.${FVER}-Linux_${arch}.deb"
done

# AUR (suffix: -Linux.pkg.tar.zst)
for f in $(find . -type f -name "*.pkg.tar.zst"); do
    cp "$f" "release/VitaABS.${FVER}-Linux.pkg.tar.zst"
done

echo "Release files:"
ls -lh release/
