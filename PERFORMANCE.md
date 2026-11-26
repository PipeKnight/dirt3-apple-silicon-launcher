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

## 1. Profile the existing renderer

- Use the shipped runtime as control. Repeat Aspen route 0 with eight cars at 1280 × 720 and at the current 1512 × 982 windowed profile. Check the game's results XML for effective settings, especially VSync, MSAA, shadows, mirrors and the worker map.
- Alternate control and candidate runs after a warm-up. Compare average FPS, 1% low, p95 and p99 frame intervals, and counts above 16.7 ms and 33.3 ms. Keep raw timings with the runtime revision, macOS build and graphics settings. Confirm worthwhile changes in a driving session, where camera and effects can differ from Aspen.
- Correlate long intervals with CPU scheduling and render calls. Measure time waiting inside buffer mapping, explicit buffer flushes, synchronous readbacks and presentation. Use a small in-memory trace and compare it with tracing disabled to check measurement overhead.
- Collect GPU timer samples asynchronously alongside interval timestamps. The overlay's CPU milliseconds sum CPU time across the process's threads. GPU milliseconds cover a sampled frame in the presenting OpenGL context. Use thread states and render waits to explain stalls, and use GPU timer samples to identify scenes where GPU work approaches the frame budget. [OpenGL timer-query specification](https://github.com/KhronosGroup/OpenGL-Registry/blob/main/extensions/ARB/ARB_timer_query.txt)

## 2. Test targeted changes in mac32

- **Buffer uploads.** Identify which buffer targets cause waits and how often the guest shadow buffer is copied. Test a streaming path for verified write-only workloads if the traces show repeated stalls. Preserve read mappings, offsets, explicit flushes, unmap return values and buffer lifetimes. Earlier staging-upload and metadata-cache experiments did not establish a repeatable gain. [Earlier experiments](findings.md#graphics-settings-and-follow-up-runtime-trials)
- **Synchronous readbacks.** Trace the engine call sites behind `glReadPixels`. Determine whether the result is needed immediately or on a later frame. A delayed readback is a candidate only where the engine tolerates that timing. Check rendering effects and menu behavior against the existing path.
- **Thread scheduling.** Distinguish time runnable from time blocked on a lock, another thread or the GPU. If the main or render thread spends meaningful time runnable, test narrowly scoped QoS changes. Revisit the worker map only if the profile shows engine tasks waiting for available workers. [Apple's guidance for performance and efficiency cores](https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon/)
- **Acceptance.** Prefer a repeatable gain in frame intervals at the same effective graphics settings. Test both supported Mac builds, including gameplay and menus at the supported resolutions and MSAA settings. Keep build-specific in-memory patches gated on matching instructions.

## 3. Prototype an alternate graphics backend

DiRT 3 uses both OpenGL 2.1 and 4.1 contexts, plus Apple buffer-flush behavior. A backend must cover context creation, switching and presentation, as well as GL commands. The initial milestone is to run the existing [OpenGL profile test](tests/gl_profiles_i386.c) and [buffer-mapping test](tests/gl_buffer_mapping_i386.c) with the candidate on the host side of mac32.

- **MobileGL** offers CGL/NSOpenGL entry points and a desktop OpenGL-to-Vulkan path. Use MoltenVK for a spike compatible with the launcher's macOS 15 baseline. Verify that the backend can be built for the x86-64 host and can represent the game's legacy and core contexts. [MobileGL's macOS build notes](https://github.com/MobileGL-Dev/MobileGL#build-for-macos) · [MoltenVK](https://github.com/KhronosGroup/MoltenVK)
- **WineMetalGL** offers desktop OpenGL-to-Metal and publishes comparisons against Apple's OpenGL on small workloads. Investigate reusing its x86-64 host code and estimate the CGL integration required. [WineMetalGL build guide](https://github.com/metalsharp/WineMetalGL/blob/main/docs/building.md) · [Published timings](https://github.com/metalsharp/WineMetalGL/blob/main/docs/performance.md)
- Once the fixtures pass, compare menus, cockpit, shadows, mirrors, MSAA and frame pacing with the existing renderer. Run the same Aspen workload before drawing any conclusion about performance. The *Super Mario Sunshine* port has published an OpenGL versus Vulkan/MoltenVK comparison on M4 that illustrates the method. [The port's Vulkan benchmarks](https://github.com/chasem-dev/sms-pc-port/blob/main/docs/VULKAN.md#macos-moltenvk)

## 4. Evaluate native ARM64 recompilation

Consider a small Mach-O recompilation feasibility test if profiling shows that execution of the original x86 game code accounts for a substantial share of the frame budget. Use a well-bounded CPU function to compare correctness and cost first. A full port would also need to cover C++ and Objective-C calls, framework behavior, threading and Steam integration.

The Recomp-Framework port of *Jurassic Park: The Game* is a precedent for lifting 32-bit Intel Mac code to ARM64. The *Portal 2* source-recomp project reported 81 FPS on OpenGL and 122 FPS on Metal for one M3 Pro scene using its recompiled Linux game. That comparison changed the rendering path, so it is evidence about that renderer in that game and cannot isolate the gain from recompilation. [Recomp-Framework](https://github.com/NightSightProductions/Recomp-Framework) · [Portal 2 measurements](https://github.com/kanapitsas/source-recomp#portal-2)
