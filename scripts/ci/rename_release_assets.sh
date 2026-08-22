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
for f in $(find . -name "*.vpk" -not -name "*updater*"); do
    cp "$f" "release/VitaABS.${FVER}.vpk"
done

# Android APKs (Gradle names them VitaABS-{abi}.apk; suffix: -{abi}.apk)
for apk in $(find . -name "*.apk"); do
    abi=$(basename "$apk" .apk | sed 's/VitaABS-//')
    [ -n "$abi" ] && cp "$apk" "release/VitaABS.${FVER}-${abi}.apk"
done

# Windows — zip each arch (exe + dlls + resources; suffix: -windows-{arch}.zip)
for dir in VitaABS-mingw-*; do
    [ -d "$dir" ] || continue
    arch=$(echo "$dir" | sed -E 's/VitaABS-mingw-([^-]+)-.*/\1/')
    (cd "$dir" && zip -r "../release/VitaABS.${FVER}-windows-${arch}.zip" .)
done

# Flatpak tarballs (no updater suffix — Flathub is the update channel)
for tgz in $(find . -name "VitaABS-Linux-*.tar.gz"); do
    base=$(basename "$tgz")
    info=$(echo "$base" | sed -E 's/VitaABS-Linux-(.+)-[0-9]+\.tar\.gz/\1/')
    cp "$tgz" "release/VitaABS.${FVER}-Flatpak-${info}.tar.gz"
done

# AppImages (self-updating Linux format; suffix: -{arch}.AppImage)
for f in $(find . -name "VitaABS-*.AppImage"); do
    base=$(basename "$f" .AppImage)
    arch=${base##*-}
    cp "$f" "release/VitaABS.${FVER}-${arch}.AppImage"
done

# Switch NRO (suffix: -nx-{driver}.nro)
for dir in VitaABS-nx-*; do
    [ -d "$dir" ] || continue
    driver=$(echo "$dir" | sed -E 's/VitaABS-nx-([^-]+)-.*/\1/')
    nro=$(find "$dir" -name "*.nro" | head -1)
    [ -n "$nro" ] && cp "$nro" "release/VitaABS.${FVER}-nx-${driver}.nro"
done

# PS4 pkg (suffix: -ps4.pkg). The build now also produces the bundled updater
# helper pkg (VSWY00003) and stages updater.pkg — the release asset must be
# the MAIN title's pkg (VSWY00002) only.
main_pkg=$(find . -path "*ps4*" -name "*VSWY00002*.pkg" | head -1)
if [ -z "$main_pkg" ]; then
    main_pkg=$(find . -path "*ps4*" -name "*.pkg" \
        -not -name "*VSWY00003*" -not -name "updater.pkg" | head -1)
fi
[ -n "$main_pkg" ] && cp "$main_pkg" "release/VitaABS.${FVER}-ps4.pkg"

# macOS DMG (suffix: -macOS-{Intel,Silicon}.dmg)
for dir in VitaABS-macos-*; do
    [ -d "$dir" ] || continue
    tag=$(echo "$dir" | sed -E 's/VitaABS-macos-([^-]+)-.*/\1/')
    dmg=$(find "$dir" -name "*.dmg" | head -1)
    [ -n "$dmg" ] && cp "$dmg" "release/VitaABS.${FVER}-macOS-${tag}.dmg"
done

# Deb (suffix: -Linux_{arch}.deb)
for f in $(find . -name "*.deb"); do
    arch=$(dpkg-deb -f "$f" Architecture 2>/dev/null || echo "amd64")
    cp "$f" "release/VitaABS.${FVER}-Linux_${arch}.deb"
done

# AUR (suffix: -Linux.pkg.tar.zst)
for f in $(find . -name "*.pkg.tar.zst"); do
    cp "$f" "release/VitaABS.${FVER}-Linux.pkg.tar.zst"
done

echo "Release files:"
ls -lh release/
