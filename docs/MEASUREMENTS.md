# Original measurements and limitations

These results belong to the frozen October 10 C16 experiment. Later coverage, JBU and neural SR work is outside this release.

## Quality

The common cohorts have 384 distinct TRAIN cameras (96 native FHD, 160 native 960, 128 native 512), 24 selection cameras (12/6/6) and 24 fresh FHD cameras. They are disjoint, and selection was frozen before fresh evaluation. The A directional, A small32 and C16 fits each ended at 10,000 updates. The selected C16 regularizer is 1e-5.

Equal-view descriptive means across 24 fresh cameras:

| Arm | RGB MSE | Boundary MSE | Highlight MSE | Bright-contour F1 | False-bright pixels |
|---|---:|---:|---:|---:|---:|
| A bilinear | 0.0005236 | 0.0037379 | 0.0045068 | 0.5155 | 301.1 |
| A+C16 | 0.0002571 | 0.0015742 | 0.0035587 | 0.6071 | 627.5 |

C16 lowers RGB MSE by 50.90%, boundary MSE by 57.89% and highlight MSE by 21.04% against bilinear, but more than doubles false-bright pixels. Boundary gradient overshoot is 8.18% versus 6.55%, and the bright near-contour false-positive rate is 31.18% versus 23.89%. The original frozen C16 selection failed 164 visual guards. No overall quality winner was declared.

The physical references are finite native 1920x1080 Cycles renderings at uniform 512 spp/seed 11. Two independent seed 23 repeats failed 4/9 original scalar checks; they do not certify all 24 references as clean. Across 14 unwarped motion transitions, residual-change MSE is 8.419e-5 for C16 versus 8.375e-5 for bilinear, while stable-interior error is 7.955e-6 versus 1.772e-5. Those screen-space deltas include camera motion, visibility change and Monte Carlo noise; they are not motion-compensated shimmer measurements.

Native/CPU parity checks used deterministic samples and whole-array integrity in six selection views. The worst sampled C16 RGBA error was 1.788139343e-7. This is not exhaustive per-pixel parity or independent certification of first-hit ownership. The original geometry retains coplanar triangle/chart-ID ambiguity.

The actual screenshots show convincing close-up baked appearance and sharper contours than bilinear, alongside phase-grid edge patterns, reflection banding and some false-bright fringes. Generalization to another scene, lighting, materials or untested camera trajectories is not established.

## Original hardware and capture configuration

Authenticated against 24 capture owners, 96 native headers and 24 capture-config SHA records:

- GPU: NVIDIA GeForce GTX 1650 with Max-Q Design, 4096 MiB installed; the original console reported 3935 MB usable.
- NVIDIA vendor 0x10DE/device 0x1F91.
- Driver 581.80.0.0, raw Vulkan driverVersion 2438201344.
- Vulkan 1.4.312, raw apiVersion 4211000.
- All 24 C16 and 24 bilinear headers: IMMEDIATE presentation, 64 valid timestamp bits, 1 ns period. The original console reported a three-image swapchain.
- All 96 captures: 1920x1080 window/framebuffer/output. A/C16 LR resolution: 480x270.
- Windowed borderless, r_fbo 1, r_overBrightBits 0, r_gamma 1, r_neuralFastBuffers 1, r_neuralFastScale 1, sound disabled, surfaceMode 2/visibility 1, com_maxfps 0, r_swapInterval 0, benchmark 0, pose 0.

Capture startup used r_neuralView 7; the final full-scene user preview uses r_neuralView 6. Surface PNG content does not depend on that plate-view choice, but whole-frame timing includes view 7 composition/presentation. These measurements do not describe the view 6 preview.

The CPU model and installed system RAM are not authenticated in these capture records. UI activity was not independently measured. Mode 4 headers omit detailed GPU/driver/presentation/timestamp fields; do not fill those omissions from unrelated runs.

## GPU timing snapshots

There is one retained GPU timestamp sample per arm per fresh camera. All 24 owners explicitly mark timing_eligible=false. These are descriptive snapshots with camera/order/UI uncertainty, not quiet repeated benchmark distributions or demonstrated FPS/speedup.

| Arm | Whole GPU frame median [min,max], ms | Surface pipeline median, ms |
|---|---:|---:|
| A bilinear | 9.042240 [5.802976,13.595872] | 5.822528 |
| A+C16 | 13.790032 [12.046304,18.748288] | 11.296704 |

C16 display: 0.045744 ms; conv0: 0.272752 ms; conv1: 1.504832 ms; phase head: 5.483872 ms; enclosing final-upscale interval: 7.307008 ms. Substages are nested within the final/pipeline/whole-GPU intervals. Do not add them twice or sum independent medians as an exact frame breakdown.

CPU full-frame latency, display FPS/scanout and continuous whole-process VRAM peak are unavailable. The recorded 378,303,680-byte retained surface allocation excludes ordinary engine/driver resources. Legacy inactive HR/readback allocations remain; this is not a lean resident-memory claim.

## Release preparation checks

The source-only portable Windows build compiled and validated 17 shaders and built Quake3e/renderer in 54.300 seconds, below a 240-second cap, on one CPU worker at below-normal priority. This is build elapsed time, not inference performance. GPU jobs: 0; engine launches: 0.

CPU asset checks verify all 6 selected surface files against whole-file SHA and bytes, the C16 NLR40001 ABI/4,000 finite FP32 weights, and the 18,662,400-byte display/activation accounting. The launcher PrepareOnly check verified 8 pinned files, frozen view 12000017, 23 startup commands/761 ASCII argument bytes, and started no renderer. The portable launcher relocates paths; CURL download support is disabled in the Windows project to exclude unverified prebuilt libraries.

Shaders and appearance files retain their original numeric identity. There was no new GPU pixel check or interactive test of the rebuilt portable engine. Full offline baking/training from the portable adapters was not rerun. Original native captures and parity remain the pixel evidence.
