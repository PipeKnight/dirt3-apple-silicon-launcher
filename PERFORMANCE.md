# DiRT 3 performance plan

Aim for 60 FPS or better on the base Apple M4 with the existing Mac edition, and reduce the long frame intervals that are still noticeable above 60 FPS average.

## Measurements on Apple M4

| Test | Result | What it tells us |
| --- | --- | --- |
| Aspen, 640 × 480, Medium, 4× MSAA, VSync off | Mapping the OpenGL buffer before reading its metadata increased average FPS from 43.57 to about 46.6 | Waiting on the graphics queue matters even at low resolution. The slowest frames varied across runs. |
| Aspen, 1280 × 720, same graphics except dynamic shadows | Shadows Ultra Low increased average FPS from 59.92 to 64.92. The 95th-percentile frame interval fell from 23.13 to 21.35 ms. | Rendering settings affect both throughput and frame pacing. |
| Aspen, 1280 × 720, two-core versus four-core worker map | The four-core map averaged 56.79 FPS versus 59.92 FPS, with a 24.20 ms versus 23.13 ms 95th-percentile interval. | The single trial gives no evidence that the four-core configuration improves this workload. The worker map describes engine scheduling and does not measure which physical cores macOS used. |
| Steam Aspen, 1512 × 982 windowed, Medium, MSAA and VSync off | A 6,921-frame run averaged 61.73 FPS. The game reported a 36.63 FPS minimum. | Average FPS alone misses noticeable slow frames. The game's minimum and an independently calculated 1% low are different statistics. |

The Steam profile shows `glReadPixels` reaching Metal through Apple's OpenGL renderer, including a command-buffer wait. Earlier samples also found the game's `WinMain` thread waiting for the OpenGL worker queue in `glGetBufferParameteriv`. These observations identify places to measure in slow frames. They do not establish which wait dominates the current build. [Detailed benchmark notes](findings.md#frame-pacing-in-the-aspen-benchmark)

### Aspen buffer-mapping profile and trial

The Steam build ran Aspen route 0 with eight cars on the base M4 at 1512 × 982, windowed, Medium, MSAA and VSync off. A diagnostic runtime timed guest OpenGL calls and buffer-bridge operations into memory and matched them to the presenting thread's frame intervals. The table accounts for calls of at least 25 µs. Nested calls and copies are listed separately and their times overlap.

| Operation | Mean per frame | Mean on the 75 frames over 33.3 ms | Longest individual call |
| --- | ---: | ---: | ---: |
| `glGetBufferParameteriv(GL_BUFFER_SIZE)` inside `glMapBuffer` | 2.60 ms | 7.74 ms | 48.28 ms |
| `glFlushMappedBufferRangeAPPLE` including the guest shadow copy | 0.72 ms | 1.37 ms | 9.34 ms |
| `glReadPixels` (1 × 1, RGBA half float) | 0.33 ms | 0.68 ms | 7.63 ms |

Almost all of the measured time inside the game's `glMapBuffer` bridge was spent asking the driver for the size of the mapped buffer. The native map itself was generally short. The longest gameplay interval in this window was 94 ms and most of it was outside the recorded OpenGL calls. The full trace changes performance and is used here to find waits.

The [buffer metadata patch](patches/mac32-gl-buffer-metadata.patch) reuses the size and explicit-flush mode previously obtained from OpenGL for the same buffer and context. The cache is invalidated on buffer storage changes, Apple buffer-parameter changes and CGL context destruction. OpenGL still handles mapping, flushing and unmapping. The runtime uses the current native binding and retains the guest shadow copy between the 32-bit game and host OpenGL. A second diagnostic trace showed the size queries disappearing from the main profile and some waiting shifting to the binding lookup, which averaged 1.85 ms per frame.

| 1512 × 982 trial, frame timestamps only | Game average FPS | 1% low FPS | p99 frame interval |
| --- | ---: | ---: | ---: |
| Control 1 | 62.60 | 23.99 | 32.03 ms |
| Buffer metadata cache | 72.88 | 37.37 | 21.58 ms |
| Control 2 | 68.76 | 28.03 | 21.85 ms |
| Cache plus experimental binding tracking | 74.05 | 27.81 | 23.09 ms |

The game reported identical hardware settings and car lists in these runs. The FPS average is from the game's results XML. The interval metrics use the trailing number of presents reported by the game, with eight seconds trimmed at each end. Test races vary, so the difference between the controls sets useful context. Binding tracking is still an experiment because its slow-frame numbers regressed against the simpler cache.

Each follow-up pair used the same executable, frame timestamps, hardware settings and car list. Both resolutions used Medium with MSAA and VSync off.

| Steam A/B, cache off → on | Game average FPS | 1% low FPS | p99 frame interval |
| --- | ---: | ---: | ---: |
| 1280 × 720 | 66.76 → 67.64 | 25.27 → 27.36 | 25.51 → 27.24 ms |
| 1512 × 982 | 68.76 → 68.67 | 40.43 → 35.18 | 21.02 → 21.68 ms |

The 1512 × 982 pair had 2 → 11 intervals above 33.3 ms in the trimmed window. The initial FPS gain did not repeat. The runtime therefore keeps the original query path by default. Set `MAC32_DIRT3_GL_BUFFER_CACHE=1` in the runtime's environment to repeat the cache experiment with the [built-in benchmark](README.md#run-the-built-in-benchmark). The standalone Mac build also completed Aspen at 1512 × 982 with the cache enabled and averaged 69.17 FPS in a compatibility run.

An optimized 1512 × 982 run with focused tracing sampled 103 frames with an asynchronous GPU timer. GPU elapsed time was 15.27 ms median and 18.81 ms at p95. The longest frame interval in the analyzed window was 94.83 ms, including an 81.51 ms `pthread_cond_wait` on the presenting thread (`WinMain`). A follow-up captured an 80.61 ms interval at the same wait site, including 67.52 ms of waiting. The game's `Update Task` thread signalled the same condition just before the wait returned. The Steam guest return addresses are `0x8cf404` for the wait and `0x8cf6be` for the signal, with the Mach-O slide removed. The follow-up trace reached its capacity late in the run. Both events in this pair were captured.

A separate run sampled the update thread's Mach state and cumulative CPU time about every millisecond. In a 67.85 ms frame, `WinMain` waited 54.66 ms for `Update Task`. Between the first and last sample within that wait, `Update Task` accumulated about 52.5 ms of CPU time over 54.2 ms of elapsed time. During a second 11.82 ms wait at the same site, both measurements were about 10.1 ms across the sampled portion. These stalls warrant profiling work inside the update task before changes to thread priority or the worker map. The macOS Time Profiler capture covered a later part of the same run and recovered a guest game address in only 39 of 31,602 update-thread samples. For the stalls, target the benchmark interval around the wait and collect guest call sites through mac32.

The synchronous 1 × 1 readback ran once per measured frame from Steam guest return address `0x95b974`. In a follow-up trace, all 83 sampled calls had no pixel pack buffer bound. The game receives the result in client memory. A deferred implementation needs evidence that the first reader accepts a delayed result.

The OpenGL buffer fixture covers storage replacement through the ARB alias and explicit flushing at a nonzero offset. The normal runtime tests and the buffer fixture passed with the cache enabled. The buffer fixture also passed with the original query path. [Apple's buffer-flush extension](https://registry.khronos.org/OpenGL/extensions/APPLE/APPLE_flush_buffer_range.txt)

### Update Task and audio buffers

Sampling the emulated x86 thread state from mac32 recovered instruction pointers inside the original binaries. In one 86.07 ms frame, the presenting thread waited 72.89 ms on the update thread. Its CPU counter advanced 70.73 ms over the 72.29 ms sampled interval. Of 58 instruction samples during the wait, 50 were in the bundled i386 OpenAL Soft library and six were in the game's byte-to-PCM16 conversion loop before `alBufferData`.

The OpenAL samples cluster at `0x1e206–0x1e2e6` within `ConvertData` in the Steam library, with the image slide removed. That branch copies matching signed 16-bit samples and repeats its inner loop for each frame of audio. A bulk copy to the same destination can preserve the sample values and remove the per-frame loop. Other long waits had little update-thread CPU time, so this optimization addresses one source of stutter.

The [OpenAL patch](patches/mac32-openal-pcm16.patch) checks the copy branch and return instructions in each of the two known i386 libraries. It changes the loaded image in memory. Other formats use the original conversion. Audio buffers read back byte for byte in a [loopback fixture](tests/openal_pcm16.c) with both Steam and standalone OpenAL. The fixture covers mono, stereo, zero-length uploads and frame counts on either side of the original vector loop boundary. A separate loopback playback check produced matching rendered samples. The standalone game completed Aspen at 64.49 average FPS with the patch enabled in a compatibility run. [OpenAL Soft buffer sample API](https://github.com/kcat/openal-soft/blob/master/include/AL/alext.h)

| 120 stereo uploads of 4,000,012 bytes each, two runs per setting | Original total | Bulk copy total |
| --- | ---: | ---: |
| Steam OpenAL | 250.9–291.2 ms | 11.92–11.99 ms |
| Standalone OpenAL | 218.7–219.4 ms | 12.06–12.12 ms |

At 1512 × 982 in Steam Aspen, the frame-timestamp-only A/B used the same runtime and effective settings. Average FPS was 69.77 with the patch off and 69.69 with it on. The 1% low was 34.87 → 35.15 FPS, p99 was 21.48 → 21.40 ms, and there were 11 → 10 intervals above 33.3 ms. The copy is substantially faster in isolation. This pair shows little change in overall frame pacing.

Two runs with guest instruction sampling confirmed that the patch moved the work out of the original loop. The sampled section of Aspen contained 227 instruction samples in the original PCM16 loop with the patch off and zero with it on. With the patch off, one 93.56 ms frame included an 80.24 ms update wait with 55 of 63 samples in that loop. With the patch on, another audio-heavy frame included a 15.81 ms update wait, of which three bulk copies accounted for 10.55 ms. The two waits occurred in separate races. The traces reached their capacity late in the run and captured both intervals.

One additional trace recorded three 14,262,272-byte uploads in a 22.10 ms update wait. The three bulk copies took 14.46 ms in total. Moving audio preparation or uploads off the update thread would require confirming when the game starts using each buffer.

Enable the audio experiment for the [built-in benchmark](README.md#run-the-built-in-benchmark) with `MAC32_DIRT3_PCM16_BULK=1`. Leave the variable unset to use the bundled copy routine.

### Steam connection polling

A follow-up trace timed the host bridges called by `Update Task`. The update thread repeatedly called `SteamUser017::BLoggedOn` from Steam guest return address `0xcabed0`. It checks connectivity to Steam's servers. One call took 274.23 ms inside a 290.18 ms `WinMain` wait in a 306.06 ms frame. Two nearby waits overlapped calls that took 90.37 and 76.87 ms. Steam startup, authentication and ownership checks are separate operations. [Valve's ISteamUser documentation](https://partner.steamgames.com/doc/api/isteamuser#BLoggedOn)

The trace kept 8,927 connection polls lasting at least 25 µs from that site. Six exceeded 10 ms. The experiment reuses the latest real result for up to 100 ms at that single site and refreshes after Steam signals a connection change.

| Steam Aspen at 1512 × 982 | Average FPS | 1% low FPS | p99 | Intervals above 33.3 ms | Direct Steam calls |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cache off, pair 1 | 67.22 | 21.81 | 29.83 ms | 48 | 6,235 |
| Cache on, pair 1 | 66.96 | 29.45 | 26.91 ms | 20 | 912 |
| Cache on, pair 2 | 66.30 | 34.08 | 23.23 ms | 12 | 950 |
| Cache off, pair 2 | 67.34 | 29.85 | 26.01 ms | 24 | 6,319 |

All four runs used the same binary, hardware settings and car list, with the PCM16 patch enabled throughout. The trace recorded frame timestamps and the connection poll only. The Steam client stayed connected during these runs. Isolated tests covered cache expiry, changes of interface, invalidation during a query and a false result from the native query. Actual Steam disconnect and reconnect behavior in the game is still unverified.

The [connection poll patch](patches/mac32-steam-connection-poll.patch) reduced direct Steam calls by about 85% and improved the slow-frame percentiles in both pairs. The average FPS was slightly lower in both pairs. Enable it for the [built-in benchmark](README.md#run-the-built-in-benchmark) with `MAC32_DIRT3_STEAM_STATUS_CACHE=1`.

## 1. Profile the existing renderer

- Use the shipped runtime as control. Repeat Aspen route 0 with eight cars at 1280 × 720 and at the current 1512 × 982 windowed profile. Check the game's results XML for effective settings, especially VSync, MSAA, shadows, mirrors and the worker map.
- Alternate control and candidate runs after a warm-up. Compare average FPS, 1% low, p95 and p99 frame intervals, and counts above 16.7 ms and 33.3 ms. Keep raw timings with the runtime revision, macOS build and graphics settings. Confirm worthwhile changes in a driving session, where camera and effects can differ from Aspen.
- Correlate long intervals with CPU scheduling and render calls. Measure time waiting inside buffer mapping, explicit buffer flushes, synchronous readbacks and presentation. Use a small in-memory trace and compare it with tracing disabled to check measurement overhead.
- Collect GPU timer samples asynchronously alongside interval timestamps. The overlay's CPU milliseconds sum CPU time across the process's threads. GPU milliseconds cover a sampled frame in the presenting OpenGL context. Use thread states and render waits to explain stalls, and use GPU timer samples to identify scenes where GPU work approaches the frame budget. [OpenGL timer-query specification](https://github.com/KhronosGroup/OpenGL-Registry/blob/main/extensions/ARB/ARB_timer_query.txt)

## 2. Test targeted changes in mac32

- **Buffer uploads.** Measure which buffer targets still incur waits and how often the guest shadow buffer is copied. Test a streaming path for verified write-only workloads if the traces show repeated stalls. Preserve read mappings, offsets, explicit flushes, unmap return values and buffer lifetimes. The staging-upload and metadata-cache trials have yet to show a repeatable improvement. [Earlier experiments](findings.md#graphics-settings-and-follow-up-runtime-trials)
- **Synchronous readbacks.** Trace the engine call sites behind `glReadPixels`. Determine whether the result is needed immediately or on a later frame. A delayed readback is a candidate only where the engine tolerates that timing. Check rendering effects and menu behavior against the existing path.
- **Thread scheduling.** Trace the blocked portions of `Update Task` around the long `WinMain` waits. Attribute waits to the specific lock, I/O call, producer or GPU operation. If the main or render thread spends meaningful time runnable, test narrowly scoped QoS changes. Revisit the worker map only if the profile shows engine tasks waiting for available workers. [Apple's guidance for performance and efficiency cores](https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon/)
- **Acceptance.** Prefer a repeatable gain in frame intervals at the same effective graphics settings. Test both supported Mac builds, including gameplay and menus at the supported resolutions and MSAA settings. Keep build-specific in-memory patches gated on matching instructions.

## 3. Prototype an alternate graphics backend

DiRT 3 uses both OpenGL 2.1 and 4.1 contexts, plus Apple buffer-flush behavior. A backend must cover context creation, switching and presentation, as well as GL commands. The initial milestone is to run the existing [OpenGL profile test](tests/gl_profiles_i386.c) and [buffer-mapping test](tests/gl_buffer_mapping_i386.c) with the candidate on the host side of mac32.

- **MobileGL** offers CGL/NSOpenGL entry points and a desktop OpenGL-to-Vulkan path. Use MoltenVK for a spike compatible with the launcher's macOS 15 baseline. Verify that the backend can be built for the x86-64 host and can represent the game's legacy and core contexts. [MobileGL's macOS build notes](https://github.com/MobileGL-Dev/MobileGL#build-for-macos) · [MoltenVK](https://github.com/KhronosGroup/MoltenVK)
- MobileGL's CGL path requests an EGL 3.3 context at the inspected revision. Its exported `glProgramStringARB`, `glGenProgramsARB`, `glBufferParameteriAPPLE` and `glFlushMappedBufferRangeAPPLE` are stubs. These paths need implementations or a way to route the legacy context to Apple's OpenGL before testing MobileGL in a full DiRT 3 benchmark. [MobileGL CGL source](https://github.com/MobileGL-Dev/MobileGL/blob/9df3600f878b2059012b4057bcb560daba816f06/MobileGL/MG_Impl/CGLImpl/CGLImpl.cpp#L199) · [OpenGL export definitions](https://github.com/MobileGL-Dev/MobileGL/blob/9df3600f878b2059012b4057bcb560daba816f06/MobileGL/MG_Impl/GLImpl/Exporting/Definitions.cpp#L1168)
- The two Mac game binaries import the same 261 CGL and OpenGL entry points. A comparison against MobileGL's export declarations at that revision found 62 imports declared as stubs, including legacy matrix, lighting and vertex-array functions. Some imports may be unused during Aspen. [OpenGL export definitions](https://github.com/MobileGL-Dev/MobileGL/blob/9df3600f878b2059012b4057bcb560daba816f06/MobileGL/MG_Impl/GLImpl/Exporting/Definitions.cpp)
- **WineMetalGL** offers desktop OpenGL-to-Metal and implements legacy ARB assembly programs. Its Wine integration creates contexts through WGL. The inspected source has no CGL entry point or implementation of the two Apple buffer-flush calls used by DiRT 3. A mac32 port would need a CGL adapter and equivalent explicit buffer flushing. [WineMetalGL compatibility notes](https://github.com/metalsharp/WineMetalGL/blob/main/docs/unsupported.md#compatibility-profile-opengl-10-to-21) · [Wine context code](https://github.com/metalsharp/WineMetalGL/blob/fac2219f37bbbe132f7d67a530a035ae033aa437/src/wine/unix/unix_wgl.c#L459)
- Once the fixtures pass, compare menus, cockpit, shadows, mirrors, MSAA and frame pacing with the existing renderer. Run the same Aspen workload before drawing any conclusion about performance. The *Super Mario Sunshine* port has published an OpenGL versus Vulkan/MoltenVK comparison on M4 that illustrates the method. [The port's Vulkan benchmarks](https://github.com/chasem-dev/sms-pc-port/blob/main/docs/VULKAN.md#macos-moltenvk)

### WineMetalGL offscreen prototype

WineMetalGL `fac2219f37bbbe132f7d67a530a035ae033aa437` built as x86-64 with a macOS 15 deployment target and ran under Rosetta on M4. All nine ARB program GPU tests passed, along with the staging-buffer draw and framebuffer readback tests. Through the native driver ABI, requests for 2.1 compatibility and 4.1 core created 4.6 contexts of the correct profile. Both cleared a pbuffer, read back a pixel and mapped a buffer.

The native driver resolved the Apple buffer and fence entry point names. Setting `GL_BUFFER_FLUSHING_UNMAP_APPLE`, flushing a mapped range through the Apple call and generating an Apple fence returned `GL_INVALID_OPERATION`. Changing one word through `glMapBufferRange` with explicit flushing preserved the neighboring words.

The [offscreen adapter](experiments/winemetalgl-offscreen.patch) reproduced the existing `gl_profiles_i386` and `gl_buffer_mapping_i386` fixtures through mac32. It maps a small subset of CGL to the driver ABI, reports the CGL profile version requested by the test, allocates buffer storage in the guest address space and translates explicit buffer flushing to the standard range API. The adapter rejects unsupported guest calls. The later window prototype covers Cocoa presentation, shared buffers and Apple fences.

Use a separate patched mac32 checkout from the [build instructions](README.md#build-from-source) to reproduce the offscreen tests. In this example, `wmgl` points to the WineMetalGL checkout at the revision above, and `runtime` points to the separate mac32 checkout in a path without spaces.

```sh
cmake -S "$wmgl" -B "$wmgl/build" -DCMAKE_BUILD_TYPE=Release -DWMGL_LTO=OFF
cmake --build "$wmgl/build" --target winemetalgl_core
git -C "$runtime" apply "$PWD/experiments/winemetalgl-offscreen.patch"
sdk="$(xcrun --sdk macosx --show-sdk-path)"
MACOSX_DEPLOYMENT_TARGET=15.0 make -C "$runtime" -j6 \
  CC="/usr/bin/clang -isysroot $sdk -I$wmgl/include" mac32
for fixture in gl_profiles_i386 gl_buffer_mapping_i386; do
  /usr/bin/clang -target i386-apple-macos10.9 -fno-builtin -fno-stack-protector \
    -nostdlib -Wl,-undefined,dynamic_lookup "tests/$fixture.c" \
    "$runtime/guest/libSystem.tbd" -o "$runtime/$fixture"
  MAC32_WMGL_FIXTURE="$wmgl/build/libwinemetalgl.dylib" "$runtime/mac32" "$runtime/$fixture"
done
```

### WineMetalGL window prototype

The window adapter uses the game's CGL contexts for pixel format, share group and capability queries. It gives each context a Metal counterpart and presents the game on a `CAMetalLayer` in the existing Cocoa window. Backing-size changes and swap interval follow the game's CGL calls. Only guest calls select the Metal context. A captured thread sample showed why this matters: AppKit's OpenGL layer could wait for the game's CGL lock while the game waited for the adapter's context registry.

The adapter uses GL sync objects for Apple fences and implements explicitly flushed mapped buffers with range mapping. On M4 an isolated test verified fence completion, isolation across two contexts and visibility of a flushed write from a shared context. Texture and vertex-array object waits conservatively cover all prior work in the current context. Purgeable objects keep their storage. GPU milliseconds in the overlay are unavailable in this mode.

WineMetalGL's ARB translator needed the NVIDIA assembly options emitted by this game. The backend patch implements the observed instruction syntax, including texture LOD, derivatives, fragment facing, condition codes and structured repetition. The host suite passed all eight ARB tests. The Steam build rendered the Aspen race in the game window and completed its eight-car benchmark through Metal. The changes remain experimental and opt-in.

Aspen ran on an Apple M4 with macOS 15.7.7 and the Steam Mac build `92701.18470.3`. Both renderers used Aspen route 0 with eight cars, 1512 × 982 windowed, VSync off and MSAA off. The game XML records identical track and hardware settings. The PCM16, Steam status and OpenGL buffer cache experiments were disabled. The timed runs recorded present timestamps only.

| Renderer | Game average FPS | 1% low | p95 interval | p99 interval | Intervals above 33.33 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Apple OpenGL | 70.87 | 25.63 FPS | 20.60 ms | 27.14 ms | 27 of 6,332 (0.4%) |
| WineMetalGL | 25.44 | 7.00 FPS | 70.75 ms | 104.93 ms | 1,423 of 1,987 (71.6%) |
| WineMetalGL with deferred CGL submission | 47.73 | 25.68 FPS | 27.82 ms | 31.51 ms | 29 of 4,431 (0.7%) |

The game reports the average FPS for the benchmark. The trace statistics use the dominant presenting context and trim eight seconds from each end of the trailing benchmark sample count. They measure intervals between calls that submit frames, including CPU and synchronization delays. On WineMetalGL with VSync off, its mailbox may drop submitted frames before the display presents them.

A separate Metal run with sparse back-buffer captures averaged 28.35 FPS. The captured frames show the track, cars, shadows, particles and HUD. The comparison covers this Aspen configuration on the Steam edition. The released launcher uses Apple OpenGL.

The first Metal profile showed a strong source of overhead. WineMetalGL's WGL path flushes the outgoing context on each release. DiRT 3 temporarily releases its window's CGL context around draw work. Over an in-race sample, the median frame had 789 draws and 803 Metal command-buffer commits. The optional CGL unbind patch leaves commands pending on that window context during temporary release. Explicit synchronization and presentation still submit work. The profiled run with this patch showed a median of 9 commits in an in-race sample. These counters guide optimization and are omitted from the timed runs.

Deferred CGL submission raised the Metal run's average FPS by 88%. Apple OpenGL remained 48% faster than the tuned Metal run. The batching option has been tested on the Steam Aspen workload. Shared-context handoff across other scenes and the standalone edition still need validation.

Apply the [window adapter](experiments/winemetalgl-window.patch) after the launcher runtime patches, in place of the offscreen experiment. Apply the [NVIDIA shader patch](experiments/winemetalgl-nv-programs.patch) to WineMetalGL at the revision above. The [CGL unbind patch](experiments/winemetalgl-cgl-unbind.patch) is required for the optional batching mode. Build for the x86-64 Rosetta host.

```sh
git -C "$wmgl" apply "$PWD/experiments/winemetalgl-nv-programs.patch" \
  "$PWD/experiments/winemetalgl-cgl-unbind.patch"
cmake -S "$wmgl" -B "$wmgl/build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DWMGL_LTO=OFF
cmake --build "$wmgl/build" --target winemetalgl_core

git -C "$runtime" apply "$PWD/experiments/winemetalgl-window.patch"
sdk="$(xcrun --sdk macosx --show-sdk-path)"
MACOSX_DEPLOYMENT_TARGET=15.0 make -C "$runtime" -j6 \
  CC="/usr/bin/clang -isysroot $sdk -I$wmgl/include" mac32
```

Set `game` to the Steam game bundle. Run all cases from a logged-in session with the same installation and graphics settings. Use a new trace path for each run. Copy each run's game-generated benchmark XML from `~/Library/Application Support/Feral Interactive/DiRT 3 Complete Edition/benchmarks/`. Compare the complete `hardware_settings_config` and `track/settings` sections before interpreting the timings.

```sh
MAC32_GL_TRACE_PATH="$PWD/aspen-opengl.trace" MAC32_GL_TRACE_FRAMES_ONLY=1 \
  MAC32_DIRT3_WINDOW_MODE=windowed python3 probe.py run \
  --engine "$runtime/mac32" --app "$game" --benchmark --timeout 300

MAC32_GL_TRACE_PATH="$PWD/aspen-metal.trace" MAC32_GL_TRACE_FRAMES_ONLY=1 \
  MAC32_DIRT3_WINEMETALGL="$wmgl/build/libwinemetalgl.dylib" \
  MAC32_DIRT3_WINDOW_MODE=windowed python3 probe.py run \
  --engine "$runtime/mac32" --app "$game" --benchmark --timeout 300

MAC32_GL_TRACE_PATH="$PWD/aspen-metal-batched.trace" MAC32_GL_TRACE_FRAMES_ONLY=1 \
  MAC32_DIRT3_WINEMETALGL="$wmgl/build/libwinemetalgl.dylib" \
  MAC32_DIRT3_WMGL_BATCH_UNBIND=1 MAC32_DIRT3_WINDOW_MODE=windowed python3 probe.py run \
  --engine "$runtime/mac32" --app "$game" --benchmark --timeout 300

python3 experiments/aspen-frame-times.py aspen-opengl.trace aspen-opengl.xml
python3 experiments/aspen-frame-times.py aspen-metal.trace aspen-metal.xml
python3 experiments/aspen-frame-times.py aspen-metal-batched.trace aspen-metal-batched.xml
```

### Asynchronous pixel readback in Aspen

The Steam binary reads one RGBA half-float pixel per frame. The client-memory call returns to guest address `0x95b974` relative to the Mach-O slide. The same routine has a second path at `0x95ba8a`. It alternates two `GL_PIXEL_PACK_BUFFER` objects, queues the new transfer and maps the preceding buffer. The engine already controls when to use the older result. WineMetalGL can blit this format directly into the pack buffer. [OpenGL pixel-buffer specification](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_pixel_buffer_object.txt)

The routine chooses its path using `UsePBOs` and the engine's pixel-buffer capability bit. The setting belongs in the `HKCU\Software\IndirectX\Direct3D\Config` key of the game's `Preferences Data` XML. For a local experiment, back up the file and add the following entry within that key. Use the original file again for the control.

```xml
<value name="UsePBOs" type="integer">1</value>
```

With `WINEMETALGL_STATS=1`, the original batched run reported one synchronous readback in each of 4,072 sampled in-race frames. In the PBO run, each of 4,376 sampled in-race frames reported one asynchronous readback and zero synchronous reads. The sampled mean GPU wait reported by the backend moved from 1.13 ms to 0.00 ms. The selection keeps frames from index 3,200 with at least 450 draws to exclude the menus and most startup work. These samples establish which code path ran. The comparison runs below leave the per-call profiler off.

The submission experiment keeps PBOs enabled and changes `WINEMETALGL_PASS_COMMIT_US` from 1000 to 2500. WineMetalGL uses this threshold when the following draw changes the render target. It submits a command buffer while the CPU is still recording the rest of the frame. A higher threshold can reduce submissions and also delay when the GPU receives work.

These completed runs used the same Steam build, eight Aspen cars and the same graphics settings as the window prototype. Deferred CGL submission was enabled in every Metal case. The reported average comes from the game XML.

| WineMetalGL configuration | Game average FPS | 1% low | p99 interval | Intervals above 33.33 ms |
| --- | ---: | ---: | ---: | ---: |
| Earlier batching control, 1000 µs | 47.73 | 25.68 FPS | 31.51 ms | 0.7% |
| Repeat batching control, 1000 µs | 44.33 | 14.70 FPS | 50.96 ms | 5.7% |
| PBO, 1000 µs | 49.53 | 24.82 FPS | 33.76 ms | 1.1% |
| PBO, 2500 µs | 43.26 | 16.58 FPS | 44.20 ms | 4.5% |

DiRT 3 paused when its window lost focus. The 1000 µs PBO trace includes a 72.28-second pause. The 2500 µs trace includes eight multi-second pauses between active segments. The frame-interval columns omit those intervals. Use `aspen-frame-times.py --exclude-confirmed-pauses 10` for the former and `--exclude-confirmed-pauses 5` for the latter. The default analyzer includes every interval in the selected window.

The PBO path removed the synchronous read and averaged 4–12% more FPS than the two controls. Its p99 fell within the range of the controls. That spread limits what this set establishes about frame pacing. The 2500 µs result was worse than the 1000 µs PBO run on both measures and had more focus interruptions. Use the 1000 µs threshold for subsequent WineMetalGL experiments.

## 4. Evaluate native ARM64 recompilation

Consider a small Mach-O recompilation feasibility test if profiling shows that execution of the original x86 game code accounts for a substantial share of the frame budget. Use a well-bounded CPU function to compare correctness and cost first. A full port would also need to cover C++ and Objective-C calls, framework behavior, threading and Steam integration.

The Recomp-Framework port of *Jurassic Park: The Game* is a precedent for lifting 32-bit Intel Mac code to ARM64. The *Portal 2* source-recomp project reported 81 FPS on OpenGL and 122 FPS on Metal for one M3 Pro scene using its recompiled Linux game. That comparison changed the rendering path, so it is evidence about that renderer in that game and cannot isolate the gain from recompilation. [Recomp-Framework](https://github.com/NightSightProductions/Recomp-Framework) · [Portal 2 measurements](https://github.com/kanapitsas/source-recomp#portal-2)
