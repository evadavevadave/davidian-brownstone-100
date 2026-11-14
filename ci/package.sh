#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Packages every plugin format that was built into one zip per platform.
#   ci/package.sh <version> <platform>     e.g. ci/package.sh v1.1.0 macos-universal
# Expects build/brownstone100.lv2 (Makefile / build.bat) and build-cmake/bin/*.vst3 and,
# on macOS, build-cmake/bin/*.component (CMake).
set -euo pipefail

version="$1"
platform="$2"
name="davidian-brownstone-100-${version}-${platform}"
out="build/dist/${name}"

rm -rf "$out" "build/dist/${name}.zip"
mkdir -p "$out/LV2" "$out/VST3"

cp -R build/brownstone100.lv2 "$out/LV2/"
cp -R build-cmake/bin/brownstone100.vst3 "$out/VST3/"
if [ -d build-cmake/bin/brownstone100.component ]; then
  mkdir -p "$out/AU"
  cp -R build-cmake/bin/brownstone100.component "$out/AU/"
fi
cp README.md LICENSE LICENSE-LV2 LICENSE-DPF docs/INSTALL.txt "$out/"

cd build/dist
if command -v zip >/dev/null 2>&1; then
  zip -qry "${name}.zip" "${name}"
else
  7z a -tzip -bd "${name}.zip" "${name}" >/dev/null
fi
echo "Packaged build/dist/${name}.zip"
