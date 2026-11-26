# DiRT 3 Complete Edition on Apple Silicon

Compatibility experiments for the original Feral Mac release. The test input is a
local installation of the game. This repository contains the probe, source
patches for the compatibility runtime, and reproducible findings.

**Status.** On an Apple M4 with macOS 15.7.7, the original Feral release ran
an offline rally on Finland's Tupasentie stage from the start to the results
screen and returned to the menu. The game saved a local profile with
autosaving enabled and loaded it in a new process. On the same Mac, the
Steam edition loaded the Tupasentie stage in Singleplayer → Time Trial
and responded to keyboard driving controls. Controllers and other macOS
versions remain unverified.

## Open the launcher

Download the DMG from [GitHub Releases](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/releases/latest).
Mount the disk image and drag **DiRT 3 Launcher.app** into
**Applications**. Open the launcher and click **Play**. It looks for your
original Mac installation. Use **Choose Game…** to locate a copy in another
folder. **Play** starts the game directly. **Skip startup videos** is enabled
by default. Clear the checkbox to play the original opening videos.
**Show Logs** reveals the latest run log in Finder.

Change resolution, multisampling (MSAA), shadows and other graphics
settings in the game's **Options → Graphics Options** menu. The game
uses its saved graphics settings, or chooses its own defaults on a
fresh installation.

The GitHub build has an ad-hoc signature and macOS may block it on first
open. After trying to open it, look for **Open Anyway** under **System
Settings → Privacy & Security** for this app. Some managed Macs may
restrict that option. [Apple's instructions](https://support.apple.com/en-us/102445)

The launcher requires an Apple silicon Mac, macOS 15 or later, Rosetta,
and an installed 32-bit Mac copy from Feral. The runtime has compatibility
profiles for Feral 1.0 (85186.17625.4091) and Steam 1.0.3
(92701.18470.3). The disk image contains the launcher and compatibility
runtime. The game executable, libraries and data come from your installation.

### Steam installation

Open Steam and sign in before clicking **Play**. The launcher looks in the
default Steam library. For a different library, use **Choose Game…** and
select **DiRT 3 Complete Edition.app** from the game's installation folder.
Keep the `Dirt3Data` folder beside the app. `Dirt3Data/dirt3_game.exe` is the
Windows executable. The Mac runtime uses the installed Steam client and
passes through its Steamworks calls.

## Inspect the installed copy

With Python 3 and the Xcode command-line tools:

```sh
python3 probe.py inspect
```

Use `--app '/path/to/DiRT 3 Complete Edition.app'` for another install. The
probe reads the bundle metadata and prints the executable's SHA-256, slice
architectures and linked libraries. SHA-256 is diagnostic. At startup the
runtime matches instruction bytes at the locations it needs to patch.
A compatible profile can match an executable with a different SHA-256.
If no profile matches, launch stops before the game starts. For a different
Mac binary, report the build number and architecture in a compatibility issue.

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
ditto "$runtime/Mac32.app" local/Mac32.app
"./Play DiRT 3.command"
```

`check` resolves startup imports without calling guest initializers. A completed
check may report unresolved imports. `run` invokes the original game through
mac32. Run the launcher in a logged-in macOS session. You can double-click
**Play DiRT 3.command** in Finder after building. It writes each session to
a separate log in `local/`. To launch from another install, pass `--app`
followed by the path to the original game bundle.

Press Enter at the title screen and load or create a local profile. In **Singleplayer → Single Race**, choose a car and team, then
select **Race** after the track loads. On the original keyboard mapping,
Space accelerates, Shift or keypad 2 brakes or reverses, A and D steer, L
applies the handbrake, and Esc pauses.

### Run the built-in performance benchmark

After building the experimental runtime, run this command from a logged-in
macOS session.

```sh
"./Play DiRT 3.command" --benchmark
```

The game skips the startup videos, runs Aspen route 0 with eight cars using
the current game settings, writes the result to
`~/Library/Application Support/Feral Interactive/DiRT 3 Complete Edition/benchmarks/`,
and exits. Use the FPS and graphics settings reported in the result XML for
comparisons. Add `--timeout 300` to stop a stalled run after five minutes.
Use **Options → Graphics Options** in the game to change the graphics
settings before a run. See
[the benchmark results](findings.md#frame-pacing-in-the-aspen-benchmark)
for measurements and the tested settings on an M4.

## Package a disk image

After the runtime build above, run `launcher/package.sh "$runtime"
"$build_dir/llvm-project"` on macOS. The script creates the app, disk image,
corresponding source archive and SHA-256 checksums under `local/`. macOS
needs access to its Disk Images service when creating the image.

The script applies ad-hoc signatures. The Intel helper uses the unsigned
executable memory entitlement to load the guest Mach-O and run its call
thunks. The Intel helper can load the installed Steam client across
signing teams and validates Valve's signature first. The Apple silicon
launcher is signed with Hardened Runtime.
Distribute the image, source archive and checksums together.

The release source archive contains the patched `mac32` tree and the
LLVM checkout paths used by its guest build. Extract it into a directory
with no spaces and run the host and guest `make` commands above with
`runtime` set to the extracted `mac32` directory and `LLVM` set to the
extracted `llvm-project` directory. The launcher sources are under
`project/launcher`.

## License

The launcher and compatibility changes are available under GPL-3.0-or-later.
mac32 and its bundled dependencies carry their respective license notices.
The original game comes from your own installation.
