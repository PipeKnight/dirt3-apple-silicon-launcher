# Bring a 32-bit Intel Mac game to Apple silicon

This guide follows the path from an installed legacy game to a working command-line launch and a small macOS launcher. It uses DiRT 3 Complete Edition as the example. The launcher starts the original game from the player's installation. mac32 loads the i386 Mach-O, runs it in an x86-64 process under Rosetta, and bridges calls to the host frameworks. [mac32 architecture](https://github.com/Shagulka/mac32#how-it-works)

The first milestone is one complete offline play session and a save that loads after quitting and reopening the game. This approach depends on the runtime being able to load the game and implement the APIs it actually calls. A required framework with no working equivalent can take more work than the launcher itself.

## 1. Identify the executable and pick a runtime

Start with an original installed copy. Keep its directory layout and use a disposable profile for save tests. Replace the example path with your own. Keep using the same Terminal session so the following commands can reuse the variables. If the Mac asks for developer tools when running `lipo` or `otool`, install them and repeat the checks.

```sh
app="/absolute/path/to/Game.app"
plist="$app/Contents/Info.plist"
entry="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")"
executable="$app/Contents/MacOS/$entry"

sw_vers
/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist"
/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist"
/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$plist"
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
| PowerPC Mach-O | A separate PowerPC emulator or a port is needed. |
| Windows PE, often `.exe` | Locate the Mac bundle if the game ships one. A Windows-only game needs a Windows compatibility runtime. |

For a universal binary, try `arm64` first, then `x86_64`, then `i386` among the slices present. If the entry point is a script, read it to find the actual Mach-O and the arguments it expects. For an install with a standalone Mach-O, run the same binary inspection commands on that file directly. If a bundled library fails to load, inspect its slices and use `otool -l` on its caller to check the runtime search paths (`LC_RPATH`).

The Steam installation of DiRT 3 contains both Feral's Mac executable and a Windows executable named `dirt3_game.exe`. Its Mac bundle needs the adjacent `Dirt3Data` directory. [DiRT 3 Steam layout](findings.md#steam-103-mac-build) · [Apple on 32-bit apps](https://support.apple.com/en-us/103076)

### Identify the Mac port's startup code

Games from one studio can ship very different Mac ports. Inventory the bundle entry point, search paths, adjacent game data and any second executable or bundled runtime. This determines how far a direct launch of the main Mach-O will take you.

| What the installation contains | Start here |
| --- | --- |
| Feral bundle with an adjacent data directory | Keep the bundle beside the data. GRID 2 has its Steam library under `Contents/Resources`, whereas DiRT 3 uses `Contents/Frameworks`. A shared engine fix may apply across games, but locate and verify any game-specific patch again. |
| A Mac wrapper around another engine | Start with the wrapper's Mach-O and keep its relative data paths. DiRT Showdown ships an eON executable and sibling `dirt.vpfs` and `win/` data. Inspect the wrapper's own initialization before deciding if you need a separate path for what it loads. |
| A Unity player with embedded Mono | Treat the player, bundled Mono library, managed assemblies and native plug-ins as different parts of the same launch. Check each native plug-in's slice. Diagnose Mono startup and managed-to-native calls separately. Colin McRae Rally reached the bundled Mono runtime before its game code could be tested. |
| A bundle with an additional executable | Verify which executable the bundle opens and trace how it hands off. The Borderlands: The Pre-Sequel launcher calls `execv` to replace itself with a second i386 executable in the same bundle. Re-entering mac32 for that exact executable let its initializers run, while keeping the bundle, arguments and original Steam startup. |

## 2. Build and run mac32 before writing an interface

Use a logged-in macOS desktop session. Install the Xcode Command Line Tools with `xcode-select --install` if `xcrun --sdk macosx --show-sdk-path` fails. Run `/usr/bin/arch -x86_64 /usr/bin/true` to check that Rosetta can start an Intel process. If macOS reports a missing executable architecture, install Rosetta with `softwareupdate --install-rosetta` on a supported macOS version and repeat the check. The runtime's self-test below verifies that this particular build can also run i386 code. [Apple's Rosetta instructions](https://support.apple.com/en-us/102527)

The revision below is the upstream baseline used for DiRT 3. On a different game, check upstream for existing fixes and pin whichever revision you test. Build the upstream sources first so you have a clean comparison for your changes. Use a temporary path without spaces for the guest compiler and keep the output path to reuse the build.

For this revision, use Xcode with the macOS 26.2 SDK for the host build. Check the selected SDK with `xcrun --sdk macosx --show-sdk-version`. The generated CoreGraphics bindings refer to declarations missing from older SDKs. If several Xcode versions are installed, set `DEVELOPER_DIR` for this Terminal session to the desired Xcode's `Contents/Developer` directory.

```sh
build_dir="$(mktemp -d /tmp/legacy-mac-game.XXXXXX)"
printf 'Working directory: %s\n' "$build_dir"
runtime="$build_dir/mac32"
git clone https://github.com/Shagulka/mac32.git "$runtime"
git -C "$runtime" checkout --detach dcfed5b9ad1ca1356bddd39ac6a26e564d536c28

(
  set -e
  cd "$runtime"
  export PATH="/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
  export LLVM="$build_dir/llvm-project"
  ./tools/get-llvm.sh
  sdk="$(xcrun --sdk macosx --show-sdk-path)"
  jobs="$(sysctl -n hw.logicalcpu)"
  MACOSX_DEPLOYMENT_TARGET=15.0 make -j"$jobs" \
    CC="/usr/bin/clang -isysroot \"$sdk\"" mac32
  make -j"$jobs" app
  ./bin/mac32 selftest
)
```

The first `make` builds the x86-64 host with the selected SDK. The second builds the i386 libraries with the guest makefile and its bundled libSystem stub. If retrying after a host SDK change, start with a clean build so old object files are rebuilt.

With `app` set to the original bundle from step 1, run a dependency check and save a baseline launch log. For a standalone executable, set `app` to its path. Use the runtime's Steam instructions if the game needs its storefront. Keep Steam signed in to the account that owns the game, and check the game's arguments, working directory and actual library path. The pinned CLI's Steam folder-name shortcut targets supported Source and GoldSrc layouts in the default library. Use the full bundle path for a game like DiRT 3.

```sh
"$runtime/bin/mac32" check --all "$app" > "$build_dir/imports.txt" 2>&1
cat "$build_dir/imports.txt"
"$runtime/bin/mac32" run -v --log "$build_dir/startup.log" "$app"
tail -n 60 "$build_dir/startup.log"
```

The launch command stays attached until the game quits. You can follow the log from another Terminal. Record the Mac model, OS version, SDK version, runtime revision, game build and last milestone. Weak imports, unused code and plug-ins loaded later can make the import inventory differ from what you see at launch. Use the runtime log to identify the first failure that blocks the next milestone. [Pinned mac32 usage and troubleshooting](https://github.com/Shagulka/mac32/blob/dcfed5b9ad1ca1356bddd39ac6a26e564d536c28/README.md#use-it)

For a Steam bundle that needs its App ID in the environment, take the ID from the installed game's Steam manifest or its official store entry. Pass `SteamAppId` and `SteamGameId` only to that launch process while Steam is signed in. Keep startup options and extra library directories per title. Test the original runtime and a patched build separately to see which shared fixes carry over.

## 3. Fix the first observed failure and retest

| First symptom | Where to look |
| --- | --- |
| The self-test fails | Check Rosetta, host and guest architectures, build output and the selected SDK. Fix the runtime setup before changing any game code. |
| The loader fails to map the executable | Inspect segment protections, file sizes and preferred virtual addresses using `otool -l`. DiRT Showdown has an inaccessible zero-fill region from address zero called `EON_RESERVE`. Recognizing it as a reservation let mac32 map the executable while leaving the region inaccessible. |
| A library or resource is missing | Check the path named in the log, the working directory, sibling data, Mach-O search paths and the missing library's architecture. An error during a dependency check can interrupt the inventory before it is complete. |
| `unimplemented function _Name` | Locate the calling image and inspect the guest's use of that API. Implement the part of its contract the caller relies on and cover it with a fixture. |
| Native exception or crash inside a bridge | Check pointer width, struct layout, out parameters, return values, callbacks and object lifetimes at the i386 to x86-64 boundary. |
| Cocoa reports an unknown class while loading a NIB | Check whether that class is registered in the host process as well as listed in the guest's dependencies. Borderlands loaded its NIB further after reusing the native WebKit linkage added for DiRT 3. |
| Window opens, then stalls | Take a process sample, for example `sample <pid> 5 -file hang.txt` with the runtime's numeric PID. Look for the blocked thread, its expected wake-up and any AppKit work running off the main thread. |
| CPU stays busy before any game window opens | Look for the repeated guest caller, then walk back to the code waiting for progress. GRID 2 repeatedly waited in Feral's startup file updater because mac32 returned an immediate timeout from its event-queue stub. Routing `ReceiveNextEvent` to native Carbon let it advance to menu initialization. |
| Repeated tests appear stuck after an earlier crash | Inspect the helper's actual window and the game's own logs as well as runtime stderr. A shared helper bundle identifier can carry macOS window recovery across games. Use a disposable helper identity for clean-start diagnostics and a stable per-title identity when testing persistent settings. |
| Store dialog or existing save fails to load | Check the original store client's state, the guest's interface version, and where preferences and saves are read and written. Reproduce again in a fresh process. |

If the program needs a CPU feature absent from your runtime, reproduce that requirement in a small standalone test. DiRT Showdown's eON wrapper allocates a non-flat x86 data segment and loads it into `FS`. Rosetta on the test Mac rejected both automatic LDT allocation and an explicit descriptor of that kind. Supporting that path requires segment emulation or another runtime that implements it.

For a suspected allocator or graphics problem, trace just those symbol fragments. Keep the verbose log with image load ranges and slides.

```sh
"$runtime/bin/mac32" run -v --trace malloc_zone_register,CGLChoosePixelFormat \
  --log "$build_dir/focused.log" "$app"
```

For a guest return address, find the loaded image whose range contains it, then subtract that image's reported slide. For example, `0x2040bb01 - 0x20400000 = 0xbb01`. This gives the Mach-O virtual address. Inspect the instructions immediately before it in the image's i386 slice with Ghidra, REA or `xcrun llvm-objdump --macho --arch=i386 --disassemble "$executable"` when the caller belongs to the main executable.

Compare what the instructions pass and consume with the legacy declaration and the host signature. Start in mac32's `src/` bridge tables. The upstream contributor guide shows when to generate a binding, use a generic signature, or write an explicit conversion for things like by-value structs and callbacks. Keep broadly applicable bridge fixes separate from game-specific changes. Edit a separate checkout of the same revision, and repeat both `make` invocations with the same SDK after each change. [mac32 contributor guide](https://github.com/Shagulka/mac32/blob/dcfed5b9ad1ca1356bddd39ac6a26e564d536c28/CONTRIBUTING.md)

### Worked example: turn a crash into an i386 test

DiRT 3 uses `%018p` to create a fixed-width value, then asks CoreFoundation for a substring at a fixed offset. The bridge discarded the width, so the substring was out of bounds. You can locate the import with `git -C "$runtime" grep -n -F '"_snprintf"' -- src`. The regression fixture calls `snprintf` directly and checks the bytes and length. It can run without any game files. From the root of a clone of this launcher repository, with `runtime` and `build_dir` from step 2, compile it against mac32's guest stub.

```sh
fixture="$build_dir/pointer-format-i386"
/usr/bin/clang -target i386-apple-macos10.9 -fno-builtin -fno-stack-protector \
  -nostdlib -Wl,-undefined,dynamic_lookup \
  tests/pointer_format_i386.c "$runtime/guest/libSystem.tbd" -o "$fixture"
"$runtime/bin/mac32" run -v --trace snprintf --log "$build_dir/fixture-before.log" "$fixture"
printf 'Fixture exit status: %s\n' "$?"
```

The log records each call to `_snprintf`. The pinned upstream build exits with status 1 on the first assertion. Run the same binary with the patched mac32 from the [DiRT 3 build instructions](README.md#build-from-source) and expect status 0. For your own game, reduce the failure to the smallest call you can reproduce. Check an observable result, rebuild mac32 and rerun the same fixture and the same game checkpoint. [Original failure](findings.md#game-engine-startup) · [Fixture source](tests/pointer_format_i386.c) · [Runtime patch](patches/mac32-startup-memory.patch)

Other DiRT 3 failures required different fixes. An early Objective-C method passed a C++ object by value and could stay entirely on the guest side. An OpenGL query had to use the active context profile. A repeated Steam restart prompt came from reading the wrong language preferences. Each case has a different failing boundary even though all three could prevent startup. [Objective-C failure](findings.md#runtime-results) · [OpenGL fixture](tests/gl_profiles_i386.c) · [Steam language diagnosis](findings.md#repeated-steam-language-restart-prompt)

## 4. Support multiple editions and builds

Identify the storefront edition and the executable build independently. In DiRT 3, both copies use the same bundle identifier, but their data layout, startup routines and store dependencies differ.

| DiRT 3 Mac edition | Version and build | Installation and launch |
| --- | --- | --- |
| Standalone | `1.0 (85186.17625.4091)` | Game data is inside the bundle under `Contents/Resources/Data`. This copy has no bundled Steam API library. |
| Steam | `1.0.3 (92701.18470.3)` | `Dirt3Data` sits beside the app. The bundled `libsteam_api.dylib` talks to the running Steam client. Select the Mac `.app` even though the install also contains a Windows executable. |

### When someone reports a new build

1. **Identify the copy and reproduce its launch.** Ask for the store, version and build number, architecture, and a relevant excerpt from the session log. Record a SHA-256 for each executable sample you investigate so you can reproduce the comparison. Check the bundle ID, binary architecture and game-specific resources before choosing the launch path. Test with the original directory layout, store client and arguments. Existing bundle IDs and version strings can be shared across different binaries. [DiRT 3 inspection command](README.md#troubleshooting) · [Launcher validation](launcher/Launcher.swift)
2. **Check what the existing runtime actually supports.** Start with a dependency report and inspect the relevant i386 code and bundled libraries. DiRT 3 briefly required an exact executable hash. It now validates the expected instructions and nearby context at the locations it needs to patch. A changed full-file hash can still match an existing profile. Changing only the Steam executable's Mach-O UUID kept the profile match in a controlled test. Changing an instruction at a required site caused the runtime to stop before guest initializers. [Steam build investigation](findings.md#steam-103-mac-build)
3. **Add a profile when the code or layout changes.** Find the same behavior in the new disassembly, inspect its callers and any referenced data, and confirm the ABI and control flow again. Store the offsets relative to the relevant Mach-O section and check the new expected instructions inside the loaded section's bounds. A profile should match every required site before applying its startup edits. Check optional hooks independently and leave that feature off if its own checks fail. Keep the installed binaries untouched and make any edits in memory. [DiRT 3 profile table and matching code](patches/mac32-startup-memory.patch) · [Optional motion and rumble checks](patches/mac32-display-and-dualsense.patch)
4. **Test acceptance and rejection.** Launch each known build and confirm the selected profile in the log. Test an unknown or deliberately non-matching disposable copy and confirm that the launcher gives a clear error when a required patch is unavailable. DiRT 3 reports exit status `78` as an unmatched compatibility profile. An import report alone cannot verify this launch behavior. Repeat the play and save checkpoints below before adding a build to the supported list. [Launcher status handling](launcher/Launcher.swift)

The Steam build needed a second version-packing fix and its own addresses. It also needed the original Steamworks method order to list existing saves. A match at the game's patch sites still leaves store integration, localization and persistence to verify. [Steam build investigation](findings.md#steam-103-mac-build) · [Steam save diagnosis](findings.md#steam-profile-loading)

For a DiRT 3 sample, run these commands from this repository with the bundle path in `app` and the patched runtime in `engine`. The check prints the selected profile when it matches and reports imports without starting guest code.

```sh
engine="/absolute/path/to/patched/Mac32.app/Contents/MacOS/mac32"
python3 probe.py inspect --app "$app"
python3 probe.py check --app "$app" --engine "$engine" > "$build_dir/profile-check.log" 2>&1
grep -F 'DiRT 3 compatibility profile:' "$build_dir/profile-check.log"
```

## 5. Prove the game is playable from the command line

Work through checkpoints in order. Each checkpoint only counts after observing the original game perform it.

| Checkpoint | What to verify |
| --- | --- |
| Startup | The game loads its own resources, creates a real window and reaches the title screen. Try a second launch to catch intermittent startup failures. |
| Input and audio | Keyboard or gamepad input moves through menus and controls gameplay. Listen to game audio and check that focus changes leave controls usable. |
| Play session | Finish a short offline level or race. Check the result screen and return to the menu. |
| Save and restart | Save with a disposable profile, quit, launch a fresh process, load the save and verify the previous progress. |
| Editions | Repeat with every distribution you plan to support. Check Steam account, save storage, language and library path independently. Test with the defaults and after restarting with changed settings. |

With DiRT 3, we completed a Finland rally and reloaded its saved profile before packaging the first Mac app. Keep the game's store client and ownership checks on their original path. [First completed rally](findings.md#game-engine-startup) · [First command-line launcher commit](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/commit/7161784f5d2aa9f8dc8d185b4c47a3d69d4c7105)

### Regression checks from DiRT 3

| What broke | What to check on another game or build |
| --- | --- |
| False installation errors and empty launcher labels | Try repeated launches with existing preferences and a clean test profile. Check the game's actual resource path and the mapped image addresses before concluding that a file is missing. DiRT 3 sometimes corrupted the localization path even when the files were present. [Resource lookup](findings.md#intermittent-resource-lookup-at-startup) · [Legacy options window](findings.md#webkit-launcher) |
| Slow or stalled startup | Time **Play** to the first interactive screen over multiple runs. Separate initialization, videos and thread waits. Skipping DiRT 3's videos saved time, while a separate stall was observed before video playback. [Startup measurements](findings.md#time-to-the-title-screen) |
| Saves disappeared and a language prompt repeated | Load an existing save and a newly written one in fresh processes. Change the storefront language, finish any normal restart and verify that the following run uses the saved language. Test a storefront library outside the default directory if you intend to support it. [Steam saves](findings.md#steam-profile-loading) · [Steam language](findings.md#repeated-steam-language-restart-prompt) |
| Resolution list was empty and the benchmark sometimes stalled | Test windowed and full-screen modes, resolution changes, refresh-rate reporting and confirmation or rollback on a HiDPI display. Verify driver context and display mode enumeration. For performance, require a completed run and compare the settings the game actually used. [Display modes](findings.md#retina-display-modes-and-fps-overlay) · [Aspen measurements](findings.md#frame-pacing-in-the-aspen-benchmark) |
| Controllers were ignored, or input worked while rumble and trigger effects failed | Test USB and Bluetooth separately. Check title-screen input, driving, physical feedback, pause and reconnect. Check that effects clear when leaving a race or quitting. DiRT 3 uses a common macOS Game Controller input profile for modern controllers and needed distinct fixes for some devices and transports. [Gamepad support](README.md#gamepads) · [DualSense investigation](findings.md#dualsense-output) · [Stadia vibration](README.md#google-stadia-controller) |
| The pointer was unavailable and the overlay covered the quit dialog | With the gamepad active, move the pointer and switch windows. Verify the overlay around focus changes and quit confirmation. Relaunch the launcher and check that the last session log is still reachable. [Pointer fix](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/commit/b79f6186495010c55634f02bae36924c1a123aac) · [Quit-dialog fix](https://github.com/PipeKnight/dirt3-apple-silicon-launcher/commit/2107f5d35adfe6517f8f2bfd146a6a15e2af4686) · [Session logs](README.md#troubleshooting) |

## 6. Add the native launcher around the working command

For a local Finder shortcut, try `"$runtime/bin/mac32" wrap "$app"`. It creates an app in `~/Applications/mac32` that points to your existing runtime and installation. For distribution, bundle the helper and carry over the working command's full launch contract.

When supporting several games, keep discovery, launch status and logging in shared code. Keep the original bundle ID, store App ID, working directory, extra library paths, entry point and tested builds with each title. Give each title's helper a distinct bundle identifier so macOS application preferences and saved window state are isolated. Since changing the helper's Info.plist invalidates its signature, sign the assembled helper and its nested libraries after setting that identity. Share runtime bridges where the API contract is the same. Keep game-specific in-memory edits and optional features on their verified profiles. A shared launcher still needs independent play and save tests for each title.

- **Find and validate the game.** Start with one location and a **Choose Game…** button. Read the bundle metadata, validate the executable and let the runtime check the compatibility profile. Pass paths and arguments directly so games in Steam libraries with spaces work.
- **Launch the runtime as a child process.** In Swift, set `Process.executableURL` to the helper binary and populate `arguments`, `currentDirectoryURL` and any required environment variables from the tested CLI launch. The GUI can be arm64 and mac32 can be x86-64 because each runs in its own process. Keep the helper in a proper app bundle for the game window and macOS app identity. Capture stdout, stderr and exit status to a per-session log, and keep **Show Logs** available afterward. [Launcher source](launcher/Launcher.swift)
- **Reuse the game's existing startup path.** DiRT 3's original WebKit options window was unreliable. The native launcher selects Feral's existing direct-start path, using a build-checked override in memory. If the target already launches cleanly, a basic mac32 wrapper may be enough. [DiRT 3 direct launch](findings.md#direct-launch) · [mac32 wrap](https://github.com/Shagulka/mac32#use-it)
- **Add options after gameplay works.** For DiRT 3, window mode, startup video skipping, controller feedback and the performance overlay were separate changes. Measure launch time or frame pacing against a repeatable baseline before making optimization claims. [Startup timings](findings.md#time-to-the-title-screen) · [Performance plan](PERFORMANCE.md)

## 7. Package and test the download

Bundle the launcher, runtime, required guest libraries and license notices. Keep the runtime's expected directory layout, including the `guest` libraries beside the runtime executable. The player supplies the original game and store libraries. Publish the matching launcher and patched runtime sources under their applicable licenses, along with build instructions and checksums. Sign the nested components before the outer macOS app. [Build instructions](README.md#build-from-source) · [DiRT 3 packaging script](launcher/package.sh)

Run `codesign --verify --deep --strict` on the assembled app and verify the published download with `shasum -a 256 -c SHA256SUMS` when using the same checksum format as DiRT 3. Install the download from Finder on a clean account or another Mac, with the development tree out of the way. Check first open, a missing installation, manual selection of a path with spaces, a full play session, and save reload. Include the tested Mac, macOS version and game edition in the release notes. DiRT 3 ships an ad-hoc signed DMG through GitHub, with Apple's first-open instructions in the README. [Install instructions](README.md#install-and-play)
