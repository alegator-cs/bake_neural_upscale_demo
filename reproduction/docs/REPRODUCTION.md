# Reproducing the October 10 ExperimentalC16 pipeline

The release preserves the scene, source and three selected inference assets. It also provides small portable adapters for native teacher export and refitting. Inference with the preserved assets is the reproducible release path. The offline adapters have not been run through a new full bake/training campaign, and freshly fitted assets have not been integrated into a rebuilt native engine during packaging. Byte-identical retraining is not established.

This is one procedural scene and a bounded camera region. Scene-specific weights store appearance. The learned final correction does not recover information that was absent from its LR inputs except insofar as that information is stored in its fitted weights. The close-view renders show the fitted scene appearance; the final head still has contour, highlight, halo and motion limitations. The original finite 512-sample Cycles teacher did not pass the clean-reference noise gate. The C16 choice is an experimental visual result, not a certified quality winner or an arbitrary-camera generalization claim.

## What is preserved

| Component | Exact asset bytes | Learned scalars |
|---|---:|---:|
| A directional field: 16,384 texels, 12 coefficients/texel, 88 charts | 788,096 | 196,608 |
| A small32 correction: four latent channels/texel and 34 -> 32 -> 32 -> 3 MLP | 272,908 | 67,811 |
| C16 final pixel correction, `NLR40001` | 16,256 | 4,000 |
| Total | **1,077,260** | **268,419** |

The three files include 768 header bytes and 2,816 chart-record bytes. FP32 learned payloads account for 1,073,676 bytes. Scene geometry, BVH and material tables are additional assets; the learned-byte total is not total runtime memory or complete package size.

The original template scene identity is SHA-256 `c844dfc2401c5fb3b8e30d9f4f375dbeca4fbb8d5aeb21ff66e6876bc2237f93`. The released `scene/indirect_alcove.blend` has SHA-256 `8e98ac44c4dab9e52821b3bfff9a510b418f460fb99a9c3223e7829a1296c45c`: only its saved file-browser UI directory was redacted. All other bytes, including every geometry/material/light/world/camera datablock, are identical. `scene/FILE_UI_REDACTION.json` records the exact changed offsets. It contains a generated alcove, ribs, plinths, spheres/ellipsoid, a standing torus, baffles, constant Principled materials and three area lights. There is no HDRI or texture image in the procedural builder. The matched evaluated mesh has **10,936 triangles, 88 charts, 18 mesh objects and 8 materials**. Later geometry/normal repair is outside this release.

The 16,384-texel chart layout was allocated earlier using 48 historical TRAIN geometry views. The October 10 fits retain those exact 88 chart records; they were not reallocated using the 384-camera cohort. `scene/chart_metadata_16384.npy` is the fixed allocation. `scene/mesh_v1.npz` is the exact CPU reconstruction mesh.

## Dependencies and versions

The original offline teacher used **Blender 3.6.0**, build hash **c7fc78b81ecb**, Cycles, and NumPy available inside Blender. The original numerical C16 trainer explicitly admitted **PyTorch 2.5.0+cu124** on a GTX 1650 Max-Q (compute capability 7.5). It used FP32, no AMP, disabled TF32, deterministic cuDNN settings and one CPU worker. The portable adapters also have CPU paths for inspection; a different PyTorch/backend version does not promise byte-identical results.

The native engine needs a C/C++ toolchain, Vulkan headers/loader and a GLSL-to-SPIR-V compiler as described by the release build instructions. The room-map compilation requires a separately obtained q3map2/NetRadiant tool and an OpenArena 0.8.8 installation. No compiler binaries, OpenArena PK3s, textures, BSP or training datasets are included here. Redistribution of generated game packages is subject to the release licensing notes.

`requirements-training.txt` lists numerical dependencies; it is a declaration, not an install command. Nothing is installed by these scripts.

## Inference first

The native release build/run recipe uses the preserved `matched_A.nlsf`, `matched_A.nlsr` and `matched_C16.nlr4`. Later guided, coverage, JBU and FSRCNN models are outside this pipeline. The C16 header is bound to the exact whole A small32 file SHA-256 `02707afc205514bdd4a70675bc5e74e3e08f483783545822ff2b71406f062ece`.

The CPU smoke tool is small and does no training, rendering or CUDA initialization:

```powershell
python -B reproduction/smoke.py --assets assets --out scratch/cpu-smoke.json
```

Its CPU/model/export checks complement the historical native GPU capture parity receipts; they do not rerun or replace native parity or runtime timing. It also reports the actual numerical library versions used.

The default smoke command checks exact assets/headers, the baseline chain, the scene copy and cameras using the standard library. The `--numerics` option enables the separate tiny CPU codec/phase/padding check; the default does not import PyTorch or initialize CUDA.

## Scene and geometry

The original `.blend` semantic identity, preserved in the release with only UI-directory redaction, is the authoritative template. The builder reproduces the procedural design; Blender saves and mesh ordering can change the bytes of a newly built file.

```powershell
blender --background --factory-startup --python reproduction/scene/build_scene.py -- --out scratch/rebuilt-alcove.blend
blender --background --factory-startup --python reproduction/scene/export_mesh.py -- --scene reproduction/scene/indirect_alcove.blend --out scratch/geometry
```

The exporter retains the Blender 3.6 evaluated split-normal API and original mesh/UV/chart convention. It never renders or modifies the source `.blend`. The expected exported triangle identity is `657e3d200dcf36412bec2e4b3b3730737a5de07a8e8a49df6e2ba80bb9b1c9ce`. The released weights require the frozen chart allocation rather than a newly allocated layout.

The generated room map is source-only. It references the symbolic OpenArena texture `gothic_block/blocks17e`. It includes no texture bytes.

```powershell
./reproduction/map/generate_map.ps1 -MapPath scratch/map-source/neural_test.map
./reproduction/map/compile_map.ps1 -Q3Map2 /path/to/q3map2.exe -OpenArenaRoot /path/to/openarena-0.8.8 -OutputDirectory scratch/compiled-map
```

The compile adapter uses the original `-game quake3 -fs_basegame baseoa`, visibility, and `-light -fast` stages. Compilation requires separately supplied licensed game data. Existing outputs are never overwritten.

## Frozen cameras and physical labels

`cameras/COHORT.json` contains the **408 actual camera records** used for the selected fits. It preserves the authoritative float32 first 64 bytes of the runtime frame, not rounded camera angles. The camera generator seed was **2026100901**. The receiver plane is x=494, with the reflected virtual camera using x'=988-x. A scale of 1/128 maps game units into scene units.

| Split | 1920x1080 labels / 480x270 LR | 960x540 / 240x135 | 512x288 / 128x72 |
|---|---:|---:|---:|
| TRAIN | 96 | 160 | 128 |
| Selection | 12 | 6 | 6 |

Selection pixels never contributed gradients. The original selection was frozen before 24 fresh evaluation views: eight static and two eight-frame motion tracks. This package's training camera file contains only TRAIN and selection; it does not provide fresh pixels for model selection.

Each teacher was generated at its **native target extent**, with **512 uniform TABULATED_SOBOL samples, seed 11**, BOX filtering of effective one-pixel width, no adaptive sampling and no denoising. Transport used six total, four diffuse and four glossy bounces; transmission, volume and transparent bounces and reflective/refractive caustics were disabled. World illumination was zero. EXR/arrays were float32 associated LINEAR RGBA. The receiver Boolean was applied once at the HR pixel center. It is not a fractional per-object visibility label.

The render adapter loads the exact scene and camera bits and generates one explicitly chosen view. It writes the raw EXR and three NPY arrays, requiring storage reservation before generation. It never resizes a label.

```powershell
blender --background --factory-startup --python reproduction/scene/render_teacher.py -- --view-id 10000001 --frame scratch/capture/view10000001.frame.hr.bin --out scratch/teacher/10000001 --device CPU
```

An actual 96-byte captured frame is the preferred camera authority where available. The cohort raw camera bits are the fallback authority. `--device CUDA` or `OPTIX` selects one explicit offline Cycles GPU; it is not a runtime rendering feature. The package performs no background generation automatically.

The display transfer used throughout is: unassociate RGB by alpha, clamp LINEAR RGB below zero, map `x/(1+x)`, raise to `1/2.2`, reassociate by alpha. No additional gamma or multiplication of final display RGB by alpha is applied. A fitting reduces the native associated LINEAR teacher with 16 explicit row-major FP32 additions per LR pixel, then applies this transfer once. Reducing display RGB instead changes the task.

## A directional and small32 refits

An A refit requires true native LR quarter-site triangle IDs/barycentrics and LR center receiver gates for every declared camera/tier, captured using the release geometry and camera table. HR and LR guides are not resized. The data registry needs `lr_frame`, `lr_linear`, `lr_ids`, `lr_barycentric`, `lr_gate`, `target_linear` and `target_rgba`. `training/registry.example.json` documents one row; the complete registry contains all 408 frozen camera records. Input files remain outside the repository.

[The native capture checklist](../capture/README.md) documents the source-verified all-408-view packet builder, exact per-tier startup settings, completion checker, capture-to-teacher frame pairing and actual-new-A recapture order. Mode 9 is explicitly routed to the mixed-resolution capture implementation before the fixed-FHD CNN modes 10/11; historical normal captures confirm all three native tiers.

The packaged runtime's actual capture interface is `surfaceCapture <game-relative stem>`. In an explicitly launched isolated preview, these console commands capture one FHD TRAIN view with the preserved A assets:

```text
set r_surfaceMode 2
set r_surfaceVisibility 1
set r_surfaceUpscale 9
set r_surfaceBatchViewID 10000001
wait 120
surfaceCapture captures/train10000001_A480
```

The normal completion JSON must say `complete=true`, `mode_index=9`, correct output/LR extents, associated LINEAR LR transfer, no HR guides, and the expected A asset SHA values. Actual outputs below the isolated home directory `baseoa/captures` are:

| Suffix | Meaning |
|---|---|
| `.lr.rgba32f` | native corrected associated LINEAR LR RGBA |
| `.lr.ids.u32` | native 2Wx2H quarter-grid triangle IDs; 0xffffffff is miss |
| `.lr.bary.f32` | native 2Wx2H two-component barycentrics |
| `.lr.center.u32` | WxH Boolean receiver gate |
| `.frame.lr.bin` / `.frame.hr.bin` | exact 96-byte frames |
| `.rgba32f` | native display baseline output, useful for independent parity |

The registry reads the ID RAW bytes as signed int32, an exact bit reinterpretation so the miss sentinel is -1; it does not resize or reassign IDs. The barycentric and gate bytes are used directly.

The medium tier requires `r_surfaceUpscale 9`, `r_customWidth 960`, `r_customHeight 540`, `r_mode -1`, followed by `vid_restart` and a declared B240 ID such as `10020001`. The low tier requires 512x288 and a C128 ID such as `10030001`. Both retain `r_neuralFastScale 1`; the mode 9 path itself creates 240x135 or 128x72 first-pass buffers. A settling interval precedes completion JSON/frame validation. Mode 10 C16 is FHD-only in this preserved build; lower-extent rejection output is invalid data. A batched unattended capture driver is not part of the portable release adapters.

```powershell
python -B reproduction/training/prepare_manifest.py --registry scratch/A-inputs.json --purpose a --out scratch/A-dataset.json
python -B reproduction/training/train_a.py check --manifest scratch/A-dataset.json --verify-sha
python -B reproduction/training/train_a.py fit --manifest scratch/A-dataset.json --stage directional --device cuda --updates 10000 --wall-cap-seconds 3600 --out scratch/A-directional
python -B reproduction/training/train_a.py fit --manifest scratch/A-dataset.json --stage small32 --baseline scratch/A-directional/directional.nlsf --device cuda --updates 10000 --wall-cap-seconds 3600 --out scratch/A-small32
```

Each original A stage completed **10,000 updates**, using view-uniform sampling and **1,024 LR pixels/update** across all three tiers. Initialization seed was **11**. Per-update sample RNG seeds were **2026100901+update** for directional and **2026100903+update** for small32. Adam used atlas LR 0.01 and small32 decoder LR 0.003, weight decay zero, with multiplier `0.1+0.9*(1+cos(pi*update/10000))/2`. The directional loss was display-RGB MSE after four-quarter resolve; small32 added 1e-5 each for chart-local latent edge and decoder penalties. Checkpoints were every 500 updates. The original selection panel used 4,096 pixels/view with seed **2026100902**; it was descriptive, not a clean-reference promotion gate.

The adapter reuses the accepted numerical model, same-triangle reconstruction, fixed layout, area reduction and loss. It replaces private experiment admission machinery with a portable manifest. A new full refit was not executed during packaging. Partial capped runs retain checkpoints and do not report a completed fit.

**Full-from-scratch runtime admission still requires strict experiment review.** The preserved engine deliberately admits exact known asset SHA identities. `runtime/identity/inspect_assets.py` now provides portable byte-validation and proposed A/C16 identity rows using the unchanged original codec validators; [the strict identity recipe](../../docs/IDENTITY.md) specifies the two admission/rebuild stages. It does not automatically admit a new model or change headers. The original receipt-aware admission graph is intentionally not restored: native physical pairing, all24 selection evidence and authenticated source/fit/upstream review remain prerequisites. Portable `FIT.json` alone is not admission. Fresh A must be admitted and rebuilt before actual408 LR recapture; fresh C16 needs its own later admission/rebuild/native checks. The shipped launcher remains pinned to the selected original assets until a new reviewed manifest/launcher is supplied.

## Actual-new-A recapture, then C16

An isolated native build containing the newly fitted A assets must **actually recapture corrected LR associated LINEAR RGBA** for all 384 TRAIN and 24 selection cameras. A C16 fit against old A captures is a different pipeline. The C16 registry only needs actual LR color, native display teacher, actual frame and baseline recipe; it does not require geometry guides. The recapture records the exact new A SHA values.

```powershell
python -B reproduction/training/prepare_manifest.py --registry scratch/C16-inputs.json --purpose c16 --directional scratch/A-directional/directional.nlsf --small32 scratch/A-small32/small32.nlsr --out scratch/C16-dataset.json
python -B reproduction/training/train_c16.py check --manifest scratch/C16-dataset.json --verify-sha
python -B reproduction/training/train_c16.py fit --manifest scratch/C16-dataset.json --small32 scratch/A-small32/small32.nlsr --device cuda --updates 10000 --wall-cap-seconds 3600 --out scratch/C16
```

The selected C16 completed **10,000 updates**. Its initialization, patch RNG and mask-pool seeds were **2026100911**, **2026100912**, **2026100914**. Adam used LR 0.001, betas (0.9,0.999), epsilon 1e-8; gradient norm clipping was 1.0. Each update sampled eight 32x32 LR cores from one camera/tier with four LR context pixels on each side. The target-only sampler mixed uniform, foreground, bright and contour branches; it maintained frame-edge phase/replicate-padding behavior and bounded each target-derived pool to 8,192 entries. Target masks guide sampling and losses only; they are not inference features.

The model is `3x3 4 -> 16 ReLU`, `3x3 16 -> 16 ReLU`, `1x1 16 -> 64`, then 4x phase shuffle to RGBA. A tanh-bounded residual is added to the canonical baseline **bilinearly upsampled in LINEAR then transferred to DISPLAY**. The output is projected to associated display RGBA with `0<=RGB<=alpha<=1`. The head receives only corrected LR DISPLAY RGBA neighborhoods and output phase; it has no HR geometry, rays, camera coordinates or surface identifiers. Its receptive field is five LR pixels.

The objective combines bright-weighted RGB MSE, 0.25 alpha MSE, 0.25 white-composite MSE, 0.1 RGB-gradient and 0.05 alpha-gradient Huber terms (delta 0.02), and **1e-5** fan-in-normalized L2. Head weights/bias were zero initially, giving the projected baseline initially. The tested precommitted stronger regularizer was not selected. Fresh evaluation must follow an immutable selection choice; gallery views used for tuning cannot subsequently be treated as held out.

The runtime currently performs LR software BVH visibility queries and receiver gates before evaluating baked appearance. The final C16 stage adds no rays, but the whole pipeline is not ray-free. The original engine did not perform Cycles path tracing at runtime. Full-frame timing and historical native parity belong in the release's runtime evidence, separately from offline generation/training cost.

## Remaining reproduction limits

Native capture automation for a completely new 408-view rebuild must be paired with the release's exact camera-import/capture interface. Input payloads and the original private job/QA receipts are deliberately excluded. Portable manifest checks establish identity, dimensions and hashes; they do not replace physical first-hit checks or certify reference noise. The original geometric engineering exceptions and finite-reference limitations remain, rather than being silently converted into a new ground-truth claim.

No new bake, retraining, GPU parity or timing was run for these reproduction adapters. The portable runtime source was separately built; see [release checks](../../docs/MEASUREMENTS.md). CPU smoke/source checks are recorded separately. Recompiled GLSL/C++ and newly fitted weights may differ across toolchain, driver and numerical backend. Comparisons with the supplied native screenshots require the preserved assets.
