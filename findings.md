# Initial investigation

## Test input

| Property | Observed value |
| --- | --- |
| Bundle identifier | `com.feralinteractive.dirt3` |
| Release | 1.0, build 85186.17625.4091 |
| Executable architecture | i386 Mach-O, one slice |
| Executable SHA-256 | `0415a9ecbaccdc25752a327c7b8a0dbd62435a308d0a95652d4a286e6a579014` |
| SDK and minimum version | Mach-O SDK 10.9, deployment target 10.7. The app's Info.plist requires 10.9.5. |
| Host | Apple Silicon, macOS 15.7.7 (24G720), Rosetta present |

REA Mach-O evidence `ev_c73fff767f0c95d354f34b389eae23769f9f93391737f812ef7378b3d0cae070`
and local `lipo` and `otool` output identify the binary. The main executable
links OpenGL and OpenCL weakly. It also links Cocoa, Carbon, CoreAudio,
AudioToolbox, AVFoundation, IOKit, bundled OpenAL and the old GNU C++
runtime. These are load commands. Runtime use needs a trace. The bundled
OpenAL dylib and QuincyKit framework each contain i386 and x86-64 slices.

Apple ended 32-bit Mac app support after Mojave. Rosetta's documented
application path translates x86-64 Mac apps. The i386 Feral binary needs a
loader and an ABI bridge for system calls and frameworks. [Apple 32-bit support](https://support.apple.com/en-mide/103076),
[Rosetta architecture](https://developer.apple.com/documentation/Apple-Silicon/about-the-rosetta-translation-environment).

## Runtime results

1. Built mac32 from source. The upstream self-test passed guest/host calls,
   allocator churn, CoreGraphics type conversion, exception handling and C++
   RTTI.
2. The unmodified loader mapped the main executable and its bundled i386
   OpenAL dylib. While registering Objective-C methods, Foundation raised
   `NSInvalidArgumentException` for the type encoding of
   `FeralApplication.setEntryPoint:`. The argument is a by-value C++
   `std::function<void()>`.
3. The patch keeps this specific method in mac32's guest method table and
   skips installing an incompatible x86-64 forwarding method for it. The
   existing i386-to-i386 dispatch can still use the original method. The
   host-to-guest ABI for this signature remains unimplemented.
4. The patched dependency pass completed with 450 recorded unresolved
   imports, including entries from the runtime and loaded guest images.
   Some may be unused or weak imports. The patched run entered the game's
   static initializers and stopped when it called `malloc_zone_register`.
   The caller's return address corresponds to the Mach-O image address
   `0xbb01` for this build. Disassembly shows the caller populating a
   32-bit version 6 zone with function pointers before registration. The
   caller then gets the default zone, unregisters it, and registers it
   again. This sequence makes registration order relevant to the bridge.

## Startup through AppKit

- The guest zone registry honors default-zone reordering and dispatches the
  original i386 allocator callbacks. mac32's own objects stay in its low
  heap. The guest `sbrk` implementation lazily commits pages in a dedicated
  64 MiB address range. The i386 fixture exercised reordering, ownership,
  reallocation, reentrant allocation and break growth and shrinkage.
- Mach-O helpers expose the image's unslid section address to
  `getsectdatafromheader` and the mapped address to `getsectdata`. The
  fixture also checks the mapped segment-command pointer from `getsegbyname`.
- Startup needed bridges for CoreFoundation allocator callbacks and ranges,
  GCD semaphores, Carbon events and HIObject constructors, an empty libcurl
  multi queue, and the packed i386 process and file catalog records. The
  framework fixture exercised guest callbacks, returned ranges, semaphores,
  Carbon event timing and the HIObject lifecycle.
- The game reached its entry point after 282 recorded game initializer calls,
  then entered `NSApplicationMain` with `MainMenu`. A restricted GUI run
  timed out with macOS service errors. A normal graphical session proceeded
  into the startup callbacks and reached `pthread_main_np`.

## Framework initialization in a graphical session

- AudioToolbox now calls the original guest file callbacks and converts the
  guest audio buffer layout. The synthetic i386 WAV fixture verified that
  four signed PCM samples returned from the native decoder match the input.
- Legacy OpenSSL calls use the versioned macOS crypto library for RIPEMD-160
  and RSA verification. A synthetic i386 smoke test accepted a genuine
  signature and rejected a changed digest. The game advanced through this
  initialization using its original calls and verification inputs.
- The game created an OpenCL context backed by a CGL share group and then a
  command queue. It reached the next initialization steps with the
  CoreFoundation stream, timer, and XML bridges. Kernel execution, rendered
  frames, and audio output from the game remain unverified.
- The installed bundle has no `libsteam_api.dylib`. The game reported the
  missing file, continued through Carbon event registration, and then stopped
  at its first call to `AEInstallEventHandler` in this checkpoint.

Playability requires loading and completing an offline race, verifying
steering, audio, frame pacing and a save/reload cycle on the test Mac.

## Comparable projects

| Route | Evidence and implications |
| --- | --- |
| mac32 compatibility | Its authors report running the original Mac versions of Portal 2 and Half-Life on modern Macs. It loads i386 Mach-O into an x86-64 process and translates framework calls across the ABI boundary. The positive self-test and observed DiRT 3 startup make this the current experiment. [mac32](https://github.com/Shagulka/mac32) |
| Bridge32 | Its author reports Darwin i386 support through a different architecture with an ARM64 host and emulated guest CPU. Evaluate framework coverage and host OS requirements independently. [Bridge32](https://github.com/colemancda/Bridge32) |
| Static recompilation of Mac binaries | NightSight's framework lifts i386 Mach-O to C and builds for ARM64. Its JPTG project is a precedent for this input format. DiRT 3 would need its own jump-table, C++ and Objective-C validation, along with a runtime for the APIs it exercises. [Recomp-Framework](https://github.com/NightSightProductions/Recomp-Framework) |
| Static recompilation of console games | Sonic Unleashed uses XenonRecomp to translate Xbox 360 PowerPC code and a separate runtime for platform behavior and shaders. This is a precedent for the workflow, with a different input format and ABI. [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp), [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) |
| Windows edition through CrossOver | Valve/Codemasters distributed the Complete Edition for Windows and Mac on Steam. CodeWeavers lists limited macOS functionality for its DiRT 3 entry. A separate Windows entitlement or installer is needed. Runtime availability and playability for that edition on this Mac are unverified. [Steam release announcement](https://store.steampowered.com/oldnews/?appgroupname=DiRT+3+Complete+Edition&appids=321040&feed=steam_community_announcements), [CodeWeavers DiRT 3 compatibility](https://www.codeweavers.com/compatibility/crossover/colin-mcrae-dirt-3) |
