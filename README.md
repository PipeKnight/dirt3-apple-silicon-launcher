# DiRT 3 Complete Edition on Apple Silicon

Compatibility experiments for the original Feral Mac release. The test input is a
local installation of the game. This repository contains the probe, source
patches for the compatibility runtime, and reproducible findings.

**Status.** On an Apple M4 with macOS 15.7.7, **Play** in the original Feral
launcher displays the DiRT 3 logo, plays the original startup videos, and
reaches the title screen with **Press Start or Enter**. The engine creates
OpenGL and OpenCL contexts and starts audio output units. Some launches
still fail in the resource check. The first offline race remains unverified.

## Inspect the installed copy

With Python 3 and the Xcode command-line tools:

```sh
python3 probe.py inspect
```

Use `--app '/path/to/DiRT 3 Complete Edition.app'` for another install. The
probe reads the bundle metadata and prints the executable's SHA-256, slice
architectures and linked libraries.

## Build the experimental runtime

The tested source is [mac32 at dcfed5b](https://github.com/Shagulka/mac32/tree/dcfed5b9ad1ca1356bddd39ac6a26e564d536c28).
The mac32 patches follow the upstream GPL-3.0 license.
The host binary runs under Rosetta. The commands below were used with Xcode's
macOS 26.2 SDK and a macOS 15 deployment target. On this Mac, an implicit
macOS 15 SDK failed to compile mac32's generated CoreGraphics bindings.
Run the OpenGL fixture and the game from a logged-in graphical session.
Build in a temporary directory with no spaces in its path because mac32's
guest Makefile passes the LLVM include path to the compiler.

```sh
build_dir="$(mktemp -d /tmp/dirt3-runtime.XXXXXX)"
runtime="$build_dir/mac32"
git clone https://github.com/Shagulka/mac32.git "$runtime"
git -C "$runtime" checkout --detach dcfed5b9ad1ca1356bddd39ac6a26e564d536c28
git -C "$runtime" apply "$PWD/patches/mac32-feral-entrypoint.patch" \
  "$PWD/patches/mac32-startup-memory.patch"
LLVM="$build_dir/llvm-project" "$runtime/tools/get-llvm.sh"

sdk="$(xcrun --sdk macosx --show-sdk-path)"
MACOSX_DEPLOYMENT_TARGET=15.0 make -C "$runtime" -j6 \
  CC="/usr/bin/clang -isysroot $sdk" mac32
LLVM="$build_dir/llvm-project" make -C "$runtime" -j6 guest

"$runtime/mac32" --selftest "$runtime/guest/selftest.dylib"
(
  set -e
  for fixture in tests/*_i386.c; do
    executable="$build_dir/$(basename "$fixture" .c)"
    /usr/bin/clang -target i386-apple-macos10.9 -fblocks -fno-builtin -fno-stack-protector \
      -nostdlib -Wl,-undefined,dynamic_lookup "$fixture" "$runtime/guest/libSystem.tbd" -o "$executable"
    "$runtime/mac32" "$executable"
  done
)
mkdir -p local
python3 probe.py check --engine "$runtime/mac32" --log local/check.log --timeout 30

LLVM="$build_dir/llvm-project" make -C "$runtime" app
codesign --force --deep --sign - "$runtime/Mac32.app"
python3 probe.py run --engine "$runtime/Mac32.app/Contents/MacOS/mac32" \
  --log local/startup.log --timeout 120
```

`check` resolves startup imports without calling guest initializers. A completed
check may report unresolved imports. `run` invokes the original game through
mac32. Run the GUI probe in a logged-in macOS session. The log file must be a
new path outside the game bundle.

See [findings.md](findings.md) for the execution trace, comparable projects and
the conditions for calling the game playable.
