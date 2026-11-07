#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
# Builds dist/msfs-linux-companion-<version>-x86_64.flatpak (a single-file bundle: flatpak install --user FILE).
# Needs flatpak-builder (or the org.flatpak.Builder Flatpak) and the org.freedesktop.Sdk//25.08 runtime:
#   flatpak install --user flathub org.flatpak.Builder org.freedesktop.Sdk//25.08 org.freedesktop.Platform//25.08
# JOBS (default 2) limits the build's parallel jobs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP=io.github.Kyr4l.MsfsLinuxCompanion
MANIFEST="$ROOT/packaging/flatpak/$APP.yml"
# outside the repo: the build holds symlinks to / (var/run -> /run) that editors and indexers would crawl
BUILD="${XDG_CACHE_HOME:-$HOME/.cache}/msfs-linux-companion/build-flatpak"
DIST="$ROOT/dist"
VERSION="$(sed -n 's/^project(msfs-linux-companion VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")$(sed -n 's/^set(COMPANION_VERSION_FIX "\([a-z]*\)").*/\1/p' "$ROOT/CMakeLists.txt")"
JOBS="${JOBS:-2}"

if command -v flatpak-builder >/dev/null; then BUILDER=(flatpak-builder)
else BUILDER=(flatpak run --filesystem="$ROOT" org.flatpak.Builder); fi

mkdir -p "$BUILD" "$DIST"
"${BUILDER[@]}" --user --force-clean --jobs="$JOBS" --state-dir="$BUILD/state" --repo="$BUILD/repo" "$BUILD/app" "$MANIFEST"
OUT="$DIST/msfs-linux-companion-$VERSION-x86_64.flatpak"
flatpak build-bundle "$BUILD/repo" "$OUT" "$APP"
echo "built $OUT"
