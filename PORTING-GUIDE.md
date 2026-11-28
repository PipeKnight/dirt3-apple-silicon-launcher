# Bring a 32-bit Intel Mac game to Apple silicon

This guide follows the path from an installed legacy game to a working command-line launch and a small macOS launcher. It uses DiRT 3 Complete Edition as the example. The launcher starts the original game from the player's installation. mac32 loads the i386 Mach-O, runs it in an x86-64 process under Rosetta, and bridges calls to the host frameworks. [mac32 architecture](https://github.com/Shagulka/mac32#how-it-works)

The first milestone is one complete offline play session and a save that loads after quitting and reopening the game. A menu, installer and optional enhancements can follow once that works.

## 1. Identify the executable and pick a runtime

Start with an original installed copy. Read the bundle's entry point and record its version, architecture and dependencies. Keep an untouched reference copy and use a disposable profile for save tests.

```sh
app="/absolute/path/to/Game.app"
plist="$app/Contents/Info.plist"
executable="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")"

file "$executable"
lipo -archs "$executable"
otool -L "$executable"
shasum -a 256 "$executable"
```

| Main executable | First route |
| --- | --- |
| `arm64` Mach-O | Launch normally on Apple silicon. |
| `x86_64` Mach-O | Try Rosetta and check the architecture of any bundled plug-ins or child processes that fail. |
| `i386` Mach-O | Try mac32. Check its support for the game's frameworks and legacy store integration. |
| Windows PE, often `.exe` | Locate the Mac bundle if the game ships one. A Windows-only game needs a Windows compatibility runtime. |

Use the executable named by the bundle. The Steam installation of DiRT 3 contains both Feral's Mac executable and a Windows executable named `dirt3_game.exe`. Keep the game bundle alongside its sibling data directories, and resolve bundled frameworks relative to the original executable. [DiRT 3 Steam layout](findings.md#steam-103-mac-build) · [Apple on 32-bit apps](https://support.apple.com/en-us/103076) · [Apple on Rosetta](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment)

## 2. Build and run mac32 before writing an interface

Use a logged-in macOS desktop session, Xcode Command Line Tools and Rosetta. The revision below is the upstream baseline used for DiRT 3. Pin the revision used for your target in your own build instructions.

```sh
build_dir="$(mktemp -d /tmp/legacy-mac-game.XXXXXX)"
runtime="$build_dir/mac32"
git clone https://github.com/Shagulka/mac32.git "$runtime"
git -C "$runtime" checkout --detach dcfed5b9ad1ca1356bddd39ac6a26e564d536c28

(
  set -e
  cd "$runtime"
  export LLVM="$build_dir/llvm-project"
  ./tools/get-llvm.sh
  make -j"$(sysctl -n hw.logicalcpu)" app
  ./bin/mac32 selftest
)
```

With `app` set to the original bundle from step 1, run a dependency check and save a baseline launch log. Keep Steam signed in for Steam builds and follow the runtime's Steam instructions for that game's launch arguments and working directory.

```sh
"$runtime/bin/mac32" check --all "$app"
"$runtime/bin/mac32" run -v --log "$build_dir/startup.log" "$app"
```

The self-test isolates runtime setup from the game. The import list is an inventory. Weak imports and unused code can leave unresolved entries in an otherwise successful run. Use the first observed failure and its caller to decide what to work on. Record the Mac model, OS version, game build and last milestone for each trace. [mac32 usage and troubleshooting](https://github.com/Shagulka/mac32#use-it)

## 3. Fix the first observed failure and retest

1. **Reproduce it with a narrow trace.** Use mac32's `--trace` option with a comma-separated list of symbol fragments. For a hang, sample the process and check which thread is waiting and what should wake it. For a crash, map the guest return address to its unslid Mach-O address before inspecting it with Ghidra, REA or `llvm-objdump`.
2. **Check the i386 ABI at the caller.** Compare legacy SDK declarations, the instructions that pass and consume the arguments, and the host function's signature. Watch for 32-bit pointers and lengths, packed records, by-value structs, callbacks and return values. Check object lifetimes and AppKit's main-thread requirements.
3. **Add the smallest bridge and test it.** Use mac32's existing thunk helpers. For an ABI change, build a small i386 program that calls the same API and checks an observable result. Then rerun the game and record the next milestone.
4. **Scope game-specific changes to verified builds.** If an original routine assumes an obsolete OS version format or a preferred load address, match the instructions at each edit site before changing memory. Report an unsupported build if a required signature is absent. Keep the original bundle intact, and keep the executable hash in the investigation notes to identify the sample.

For example, trace a game that dies while registering its heap or starting graphics:

```sh
"$runtime/bin/mac32" run -v --trace malloc_zone_register,CGLChoosePixelFormat \
  --log "$build_dir/focused.log" "$app"
```

DiRT 3 first exposed a by-value C++ argument in an Objective-C method. Keeping the method on the guest side let startup reach `malloc_zone_register`. Later fixes covered native stack use in WebKit, OpenGL queries for the active profile, and input and file APIs. The test for OpenGL creates both profiles in a single guest process. [Initial failure](findings.md#runtime-results) · [OpenGL fixture](tests/gl_profiles_i386.c) · [Steam language example](findings.md#repeated-steam-language-restart-prompt)

## 4. Prove the game is playable from the command line

Work through checkpoints in order. Each checkpoint only counts after observing the original game perform it.

| Checkpoint | What to verify |
| --- | --- |
| Startup | The game loads its own resources, creates a real window and reaches the title screen. Try a second launch to catch intermittent startup failures. |
| Input and audio | Keyboard or gamepad input moves through menus and controls gameplay. Listen to game audio and check that focus changes leave controls usable. |
| Play session | Finish a short offline level or race. Check the result screen and return to the menu. |
| Save and restart | Save with a disposable profile, quit, launch a fresh process, load the save and verify the previous progress. |
| Editions | Repeat with every distribution you plan to support. Check Steam account, save storage, language and library path independently. |

With DiRT 3, we completed a Finland rally and reloaded its saved profile before packaging the first Mac app. Steam support came later and had a separate save-loading fix. The issue was traced to the order of methods in the older Steamworks interface. Keep the game's store client and ownership checks on their original path. [First completed rally](findings.md#game-engine-startup) · [First command-line launcher commit](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/commit/7161784f5d2aa9f8dc8d185b4c47a3d69d4c7105) · [Steam saves](findings.md#steam-profile-loading)

## 5. Add the native launcher around the working command

- **Find and validate the game.** Start with one location and a **Choose Game…** button. Read the bundle metadata, validate the executable and let the runtime check the compatibility profile. Pass paths and arguments directly so games in Steam libraries with spaces work.
- **Launch the runtime as a child process.** The GUI can be arm64 and mac32 can be x86-64 because each runs in its own process. Use the working command's arguments and working directory. Keep the helper in a proper app bundle for the game window and macOS app identity. Capture stdout, stderr and exit status to a per-session log, and keep **Show Logs** available afterward. [Launcher source](launcher/Launcher.swift)
- **Reuse the game's existing startup path.** DiRT 3's original WebKit options window was unreliable. The native launcher selects Feral's existing direct-start path, using a build-checked override in memory. If the target already launches cleanly, a basic mac32 wrapper may be enough. [DiRT 3 direct launch](findings.md#direct-launch) · [mac32 wrap](https://github.com/Shagulka/mac32#use-it)
- **Add options after gameplay works.** For DiRT 3, window mode, startup video skipping, controller feedback and the performance overlay were separate changes. Measure launch time or frame pacing against a repeatable baseline before making optimization claims. [Startup timings](findings.md#time-to-the-title-screen) · [Performance plan](PERFORMANCE.md)

## 6. Package and test the download

Bundle the launcher, runtime, required guest libraries and license notices. The player supplies the original game installation. Publish the matching launcher and patched runtime sources under their applicable licenses, along with build instructions and checksums. [Build instructions](README.md#build-from-source) · [DiRT 3 packaging script](launcher/package.sh)

Test the release artifact by installing it into a normal folder and launching it as a player would. Check a manual game selection, a path with spaces, a full play session and save reload. Include the tested Mac, macOS version and game edition in the release notes. DiRT 3 ships an ad-hoc signed DMG through GitHub, with Apple's first-open instructions in the README. [Install instructions](README.md#install-and-play)
