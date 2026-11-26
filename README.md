# DiRT 3 Launcher for Apple Silicon

Play **DiRT 3 Complete Edition** on Apple silicon with your existing 32-bit Mac copy. This independent, open-source launcher uses mac32 to run the game files from your installation. [mac32 compatibility runtime](https://github.com/Shagulka/mac32)

Codemasters developed DiRT 3. Feral Interactive developed and published the Mac port. [Original Mac game credits](https://www.feralinteractive.com/en/readmes/dirt3/latest/steam/)

**[Download the latest release for macOS](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/releases/latest)**

## What you need

- An Apple silicon Mac running macOS 15 or later with Rosetta installed.
- An installed 32-bit Mac copy of **DiRT 3 Complete Edition**.

| Mac edition | Version | Build |
| --- | --- | --- |
| Standalone | 1.0 | `85186.17625.4091` |
| Steam | 1.0.3 | `92701.18470.3` |

On an Apple M4 running macOS 15.7.7, the standalone edition completed a single-player rally and reloaded a saved profile. The Steam edition ran a Finland Time Trial with keyboard controls and completed the Aspen benchmark. [Compatibility notes](findings.md) · [Steam test results](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/releases/tag/v0.2.0)

## Install and play

1. Download `DiRT-3-Launcher-macOS-arm64.dmg`, open it and drag **DiRT 3 Launcher.app** into **Applications**.
2. For the Steam edition, open Steam, sign in and keep it running while you play.
3. Open **DiRT 3 Launcher** and click **Play**. The launcher looks in Applications and the default Steam library. If your copy is elsewhere, click **Choose Game…** and select **DiRT 3 Complete Edition.app**.

Keep the Steam installation's `Dirt3Data` folder beside the Mac app. The `dirt3_game.exe` file inside that folder is the Windows executable.

The GitHub download is ad-hoc signed. If macOS says it cannot verify the developer when you first open the launcher, use **System Settings → Privacy & Security → Open Anyway** for this app. [Apple's instructions](https://support.apple.com/en-us/102445)

## Graphics and startup videos

Change render resolution, VSync, multisampling (MSAA) and other graphics settings in **Options → Graphics Options** in the game. On Retina displays, the launcher includes compatible scaled modes and keeps the original 640 × 480 setting selectable. The game uses the desktop refresh rate. To change it, select a rate in **System Settings → Displays** before launching. [Feral's graphics guide](https://www.feralinteractive.com/en/readmes/dirt3/latest/steam/)

During play, press **F9** to toggle the performance overlay. It shows FPS, frame interval and 1% low over the last five seconds. CPU use is for the game process, with 100% equivalent to one core. CPU time per frame sums the work across its threads. GPU utilization covers the whole GPU and appears when macOS exposes it. The thermal indicator shows the macOS thermal state.

**Skip startup videos** is on by default. Clear the checkbox in the launcher to play the opening videos.

## DualSense over USB

Connect a DualSense directly to your Mac before clicking **Play**. Press **Options** at the title screen. The launcher registers a controller profile for your installed game and routes the game's two vibration channels to the DualSense handle actuators.

## Troubleshooting

If the launcher reports that no compatibility profile matched, check the build of your Mac copy. Profile selection checks the instructions the runtime needs to patch. Executables with different SHA-256 hashes can use the same profile when those instructions match.

After a launch attempt, **Show Logs** reveals the session log in Finder. When [reporting an issue](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/issues), include your Mac model, macOS version, game edition and build number. Share relevant log lines after removing account identifiers and personal paths.

To inspect a local game bundle from this repository, use Python 3 and the Xcode command-line tools.

```sh
python3 probe.py inspect --app "/path/to/DiRT 3 Complete Edition.app"
```

## Run the built-in benchmark

With the launcher installed in **Applications**, run this from the repository in a logged-in macOS session. The command uses Python 3 and the Xcode command-line tools. Keep Steam open when benchmarking the Steam edition.

```sh
runtime="/Applications/DiRT 3 Launcher.app/Contents/Helpers/Mac32.app/Contents/MacOS/mac32"
python3 probe.py run --engine "$runtime" --benchmark --timeout 300
```

The command uses the standalone copy in `/Applications` if present, then checks the default Steam library. Pass `--app "/path/to/DiRT 3 Complete Edition.app"` to choose a specific copy. The benchmark runs Aspen with eight cars and writes an XML report under `~/Library/Application Support/Feral Interactive/DiRT 3 Complete Edition/benchmarks/`. Use the graphics settings recorded in that file when comparing FPS. [Benchmark results](findings.md#frame-pacing-in-the-aspen-benchmark)

## Build from source

Run these commands from the repository root on an Apple silicon Mac with Xcode's command-line tools installed. The release was built with the macOS 26.2 SDK and a macOS 15 deployment target. The temporary build path keeps spaces out of the paths passed to the guest compiler.

```sh
build_dir="$(mktemp -d /tmp/dirt3-runtime.XXXXXX)"
runtime="$build_dir/mac32"
llvm="$build_dir/llvm-project"

git clone https://github.com/Shagulka/mac32.git "$runtime"
git -C "$runtime" checkout --detach dcfed5b9ad1ca1356bddd39ac6a26e564d536c28
git -C "$runtime" apply "$PWD/patches/mac32-feral-entrypoint.patch" \
  "$PWD/patches/mac32-startup-memory.patch" \
  "$PWD/patches/mac32-display-and-dualsense.patch" \
  "$PWD/patches/mac32-steam-cloud.patch"
LLVM="$llvm" "$runtime/tools/get-llvm.sh"

sdk="$(xcrun --sdk macosx --show-sdk-path)"
MACOSX_DEPLOYMENT_TARGET=15.0 make -C "$runtime" -j6 \
  CC="/usr/bin/clang -isysroot $sdk" mac32
LLVM="$llvm" make -C "$runtime" -j6 app
launcher/package.sh "$runtime" "$llvm"
```

The packaging script creates the app, DMG, corresponding source archive and `SHA256SUMS` under `local/`. The source archive includes the patched mac32 tree, the LLVM sources used by the guest libraries and the launcher source under `project/launcher/`.

<details>
<summary>Run the runtime tests</summary>

Run the tests from a logged-in macOS session so the OpenGL fixture can create a graphics context.

```sh
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
```

</details>

## License

The launcher and compatibility changes are available under GPL-3.0-or-later. The release includes license notices for mac32 and its bundled dependencies. [Project license](LICENSE)
