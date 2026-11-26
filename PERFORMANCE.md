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

The synchronous 1 × 1 readback ran once per measured frame from Steam guest return address `0x95b974`. Its call site can unbind a pixel pack buffer after the read. Check the actual destination and the first consumer before testing a deferred result.

The OpenGL buffer fixture covers storage replacement through the ARB alias and explicit flushing at a nonzero offset. The normal runtime tests and the buffer fixture passed with the cache enabled. The buffer fixture also passed with the original query path. [Apple's buffer-flush extension](https://registry.khronos.org/OpenGL/extensions/APPLE/APPLE_flush_buffer_range.txt)

## 1. Profile the existing renderer

- Use the shipped runtime as control. Repeat Aspen route 0 with eight cars at 1280 × 720 and at the current 1512 × 982 windowed profile. Check the game's results XML for effective settings, especially VSync, MSAA, shadows, mirrors and the worker map.
- Alternate control and candidate runs after a warm-up. Compare average FPS, 1% low, p95 and p99 frame intervals, and counts above 16.7 ms and 33.3 ms. Keep raw timings with the runtime revision, macOS build and graphics settings. Confirm worthwhile changes in a driving session, where camera and effects can differ from Aspen.
- Correlate long intervals with CPU scheduling and render calls. Measure time waiting inside buffer mapping, explicit buffer flushes, synchronous readbacks and presentation. Use a small in-memory trace and compare it with tracing disabled to check measurement overhead.
- Collect GPU timer samples asynchronously alongside interval timestamps. The overlay's CPU milliseconds sum CPU time across the process's threads. GPU milliseconds cover a sampled frame in the presenting OpenGL context. Use thread states and render waits to explain stalls, and use GPU timer samples to identify scenes where GPU work approaches the frame budget. [OpenGL timer-query specification](https://github.com/KhronosGroup/OpenGL-Registry/blob/main/extensions/ARB/ARB_timer_query.txt)

## 2. Test targeted changes in mac32

- **Buffer uploads.** Measure which buffer targets still incur waits and how often the guest shadow buffer is copied. Test a streaming path for verified write-only workloads if the traces show repeated stalls. Preserve read mappings, offsets, explicit flushes, unmap return values and buffer lifetimes. The staging-upload and metadata-cache trials have yet to show a repeatable improvement. [Earlier experiments](findings.md#graphics-settings-and-follow-up-runtime-trials)
- **Synchronous readbacks.** Trace the engine call sites behind `glReadPixels`. Determine whether the result is needed immediately or on a later frame. A delayed readback is a candidate only where the engine tolerates that timing. Check rendering effects and menu behavior against the existing path.
- **Thread scheduling.** Start with CPU samples from `Update Task` around the long `WinMain` stalls. Distinguish time runnable from time blocked on a lock, another thread or the GPU. If the main or render thread spends meaningful time runnable, test narrowly scoped QoS changes. Revisit the worker map only if the profile shows engine tasks waiting for available workers. [Apple's guidance for performance and efficiency cores](https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon/)
- **Acceptance.** Prefer a repeatable gain in frame intervals at the same effective graphics settings. Test both supported Mac builds, including gameplay and menus at the supported resolutions and MSAA settings. Keep build-specific in-memory patches gated on matching instructions.

## 3. Prototype an alternate graphics backend

DiRT 3 uses both OpenGL 2.1 and 4.1 contexts, plus Apple buffer-flush behavior. A backend must cover context creation, switching and presentation, as well as GL commands. The initial milestone is to run the existing [OpenGL profile test](tests/gl_profiles_i386.c) and [buffer-mapping test](tests/gl_buffer_mapping_i386.c) with the candidate on the host side of mac32.

- **MobileGL** offers CGL/NSOpenGL entry points and a desktop OpenGL-to-Vulkan path. Use MoltenVK for a spike compatible with the launcher's macOS 15 baseline. Verify that the backend can be built for the x86-64 host and can represent the game's legacy and core contexts. [MobileGL's macOS build notes](https://github.com/MobileGL-Dev/MobileGL#build-for-macos) · [MoltenVK](https://github.com/KhronosGroup/MoltenVK)
- MobileGL's CGL path requests an EGL 3.3 context at the inspected revision. Its exported `glProgramStringARB`, `glGenProgramsARB`, `glBufferParameteriAPPLE` and `glFlushMappedBufferRangeAPPLE` are stubs. These paths need implementations or a way to route the legacy context to Apple's OpenGL before testing MobileGL in a full DiRT 3 benchmark. [MobileGL CGL source](https://github.com/MobileGL-Dev/MobileGL/blob/9df3600f878b2059012b4057bcb560daba816f06/MobileGL/MG_Impl/CGLImpl/CGLImpl.cpp#L199) · [OpenGL export definitions](https://github.com/MobileGL-Dev/MobileGL/blob/9df3600f878b2059012b4057bcb560daba816f06/MobileGL/MG_Impl/GLImpl/Exporting/Definitions.cpp#L1168)
- **WineMetalGL** offers desktop OpenGL-to-Metal and implements legacy ARB assembly programs. Its Wine integration creates contexts through WGL. The inspected source has no CGL entry point or implementation of the two Apple buffer-flush calls used by DiRT 3. A mac32 port would need a CGL adapter and equivalent explicit buffer flushing. [WineMetalGL compatibility notes](https://github.com/metalsharp/WineMetalGL/blob/main/docs/unsupported.md#compatibility-profile-opengl-10-to-21) · [Wine context code](https://github.com/metalsharp/WineMetalGL/blob/fac2219f37bbbe132f7d67a530a035ae033aa437/src/wine/unix/unix_wgl.c#L459)
- Once the fixtures pass, compare menus, cockpit, shadows, mirrors, MSAA and frame pacing with the existing renderer. Run the same Aspen workload before drawing any conclusion about performance. The *Super Mario Sunshine* port has published an OpenGL versus Vulkan/MoltenVK comparison on M4 that illustrates the method. [The port's Vulkan benchmarks](https://github.com/chasem-dev/sms-pc-port/blob/main/docs/VULKAN.md#macos-moltenvk)

## 4. Evaluate native ARM64 recompilation

Consider a small Mach-O recompilation feasibility test if profiling shows that execution of the original x86 game code accounts for a substantial share of the frame budget. Use a well-bounded CPU function to compare correctness and cost first. A full port would also need to cover C++ and Objective-C calls, framework behavior, threading and Steam integration.

The Recomp-Framework port of *Jurassic Park: The Game* is a precedent for lifting 32-bit Intel Mac code to ARM64. The *Portal 2* source-recomp project reported 81 FPS on OpenGL and 122 FPS on Metal for one M3 Pro scene using its recompiled Linux game. That comparison changed the rendering path, so it is evidence about that renderer in that game and cannot isolate the gain from recompilation. [Recomp-Framework](https://github.com/NightSightProductions/Recomp-Framework) · [Portal 2 measurements](https://github.com/kanapitsas/source-recomp#portal-2)
