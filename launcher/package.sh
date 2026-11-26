#!/bin/bash
set -euo pipefail

if [[ $# != 2 ]]; then
    printf 'Usage: %s /path/to/built/mac32 /path/to/llvm-project\n' "$0" >&2
    exit 2
fi
project="$(cd "$(dirname "$0")/.." && pwd -P)"
runtime="$(cd "$1" && pwd -P)"
llvm="$(cd "$2" && pwd -P)"
revision=dcfed5b9ad1ca1356bddd39ac6a26e564d536c28

mkdir -p "$project/local"
output="$(mktemp -d "$project/local/release.XXXXXX")"

# Bundle the exact source and build scripts for the distributed binaries.
source="$output/source/DiRT-3-Launcher-source"
mkdir -p "$source/mac32" "$source/project"
git -C "$runtime" archive "$revision" | tar -xf - -C "$source/mac32"
# The staging directory is inside this repository. Stop Git's search there
# so it applies upstream-relative paths to the extracted tree.
GIT_CEILING_DIRECTORIES="$source" git -C "$source/mac32" apply \
    "$project/patches/mac32-feral-entrypoint.patch" \
    "$project/patches/mac32-startup-memory.patch" \
    "$project/patches/mac32-display-and-dualsense.patch" \
    "$project/patches/mac32-steam-cloud.patch"
GIT_CEILING_DIRECTORIES="$source" git -C "$source/mac32" apply \
    "$project/patches/mac32-modern-gamepads.patch"
GIT_CEILING_DIRECTORIES="$source" git -C "$source/mac32" apply \
    "$project/patches/mac32-gpu-frame-time.patch"
while IFS= read -r -d '' file; do
    cmp -s "$file" "$runtime/${file#"$source/mac32/"}" || {
        printf 'Runtime source differs from the release patches: %s\n' "${file#"$source/mac32/"}" >&2
        exit 1
    }
done < <(find "$source/mac32" -type f -print0)
cp -R "$project/launcher" "$project/patches" "$project/tests" "$source/project/"
cp "$project/README.md" "$project/findings.md" "$project/LICENSE" "$project/probe.py" \
    "$project/Play DiRT 3.command" "$source/project/"
if [[ "$(git -C "$llvm" rev-parse HEAD)" != "$(git -C "$llvm" rev-parse llvmorg-17.0.6^{commit})" ||
      -n "$(git -C "$llvm" status --porcelain)" ]]; then
    printf 'Use a clean llvmorg-17.0.6 checkout from the mac32 build instructions.\n' >&2
    exit 1
fi
mkdir -p "$source/llvm-project/libcxx"
cp -R "$llvm/cmake" "$llvm/runtimes" "$llvm/libcxxabi" "$llvm/libunwind" "$llvm/LICENSE.TXT" "$source/llvm-project/"
cp -R "$llvm/libcxx/include" "$llvm/libcxx/src" "$source/llvm-project/libcxx/"
tar -czf "$output/DiRT-3-Launcher-source.tar.gz" -C "$output/source" DiRT-3-Launcher-source

stage="$output/disk"
app="$stage/DiRT 3 Launcher.app"
helper="$app/Contents/Helpers/Mac32.app"
mkdir -p "$helper/Contents/MacOS/guest" "$app/Contents/Resources" "$app/Contents/MacOS"

cp "$project/launcher/Info.plist" "$app/Contents/Info.plist"
cp "$runtime/Mac32.app/Contents/Info.plist" "$helper/Contents/Info.plist"
# Keep the app identity used by the compatibility process and remove document
# associations from the upstream general-purpose launcher.
/usr/libexec/PlistBuddy -c 'Delete :CFBundleDocumentTypes' \
    -c 'Add :GCSupportedGameControllers array' \
    -c 'Add :GCSupportedGameControllers:0 dict' \
    -c 'Add :GCSupportedGameControllers:0:ProfileName string ExtendedGamepad' "$helper/Contents/Info.plist"
cp "$runtime/mac32" "$helper/Contents/MacOS/mac32"
for library in libmac32rt libmac32cxx libmac32cxx2 libmac32ios; do
    cp "$runtime/guest/$library.dylib" "$helper/Contents/MacOS/guest/"
done
cp "$runtime/COPYING" "$runtime/NOTICE" "$runtime/third_party/LICENSE-Steamworks.NET.txt" "$app/Contents/Resources/"
cp "$llvm/LICENSE.TXT" "$app/Contents/Resources/LICENSE-LLVM.txt"
xcrun swiftc -swift-version 6 -target arm64-apple-macos15.0 -O \
    -module-cache-path "$output/swift-cache" \
    -o "$app/Contents/MacOS/DiRT3Launcher" "$project/launcher/Launcher.swift"

for library in "$helper/Contents/MacOS/guest/"*.dylib; do
    codesign --force --sign - "$library"
done
codesign --force --sign - --options runtime --entitlements "$project/launcher/runtime.entitlements" "$helper"
codesign --force --sign - --options runtime "$app"
codesign --verify --deep --strict "$app"

ln -s /Applications "$stage/Applications"
image="$output/DiRT-3-Launcher-macOS-arm64.dmg"
hdiutil create -srcfolder "$stage" -volname 'DiRT 3 Launcher' -format UDZO "$image"
hdiutil verify -quiet "$image"

(
    cd "$output"
    shasum -a 256 'DiRT-3-Launcher-macOS-arm64.dmg' 'DiRT-3-Launcher-source.tar.gz' > SHA256SUMS
)
printf '\nApplication: %s\nDisk image: %s\nSource archive: %s\n' "$app" "$image" "$output/DiRT-3-Launcher-source.tar.gz"
