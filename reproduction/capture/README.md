# Native capture checklist

These instructions and packet scripts describe the preserved October 10 engine. They start no renderer automatically. No new capture or benchmark was run while preparing this release.

## Exact supported extents

| Tier | TRAIN | Selection | Actual framebuffer/output | Actual first-pass color | Actual quarter IDs/barycentrics |
|---|---:|---:|---|---|---|
| A480 | 96 | 12 | 1920x1080 | 480x270 | 960x540 |
| B240 | 160 | 6 | 960x540 | 240x135 | 480x270 |
| C128 | 128 | 6 | 512x288 | 128x72 | 256x144 |

**All 408 input acquisitions use `r_surfaceUpscale 9`.** This is the dedicated native multiresolution bilinear capture path, including the FHD case. It produces genuine LR quarter guides and corrected associated LINEAR LR color. It creates no HR visibility guides/rays. Modes 10 (C16) and 11 (same-family bypass) are fixed 480x270 -> 1920x1080 in this release, and are not the mixed-tier acquisition path. There is no need to fall back to the old native mode0 guide export.

The packaged `surface_capture4x_extent()` admits these three output extents only when mode9 is selected. `surface_finish_capture()` routes mode9 to `surface_multires_finish_capture()` before the modes10/11 CNN capture branch. The output-to-LR scale is exactly four on each axis. `SOURCE_VERIFICATION.json` binds the supporting source and representative historical normal captures for all three tiers. Historical recapture used the same mode9 flags at 1920, 960, 512 framebuffer widths; it did not render a single FHD capture and resize it.

## Source-only camera packet preparation

From the repository root:

```powershell
python -B reproduction/capture/make_packets.py --phase pre_a --out work/capture-pre-a
```

The packet builder only reads the source and frozen camera metadata, then writes **52 configs** in batches of at most eight views: 14 A480, 21 B240, 17 C128. It verifies that all 408 camera IDs and every float32 camera word match the packaged native camera table. Each camera is assigned exactly once to its declared tier/split. A config contains `wait`, camera-ID selection, one `surfaceCapture` per view, then `quit`; execution requires an explicit engine launch with that config.

`PLAN.json` includes every exact startup setting, resolution, expected frame/guide extent, and capture stem. The expected raw payload for all 408 captures is **6,677,771,520 bytes**, excluding scene/home assets, logs, metadata and teacher arrays. A second actual-new-A recapture costs another acquisition unless earlier scratch is deliberately retired after validation. Execution requires reserved storage and a finite exclusive capture slot. The suggested 60-second historical batch cap is not enforced by these source-only packets; each batch launch requires a bounded process timeout.

## Batch execution

1. The main build recipe produces the preserved native engine. Capture uses a separate writable home outside the OpenArena installation. `runtime/run.ps1 -PrepareOnly` prepares the scene/model assets using the specified OpenArena root; the locally compiled `neural_test.bsp` belongs in that home's `baseoa/maps`. Preparation alone launches nothing.
2. A selected packet batch in `PLAN.json` supplies `config_NN.cfg`, which belongs in the fresh home's `baseoa` directory under the same filename. Input acquisition uses this batch config rather than the preview's `c16_release.cfg`.
3. The built engine launches with the selected batch's `startup_settings`; only `<OpenArenaRoot>` and `<FreshHome>` are replaced, followed by `+devmap neural_test +exec config_NN.cfg`. Required settings are the **actual target framebuffer** width/height from the table, `r_neuralFastScale 1`, `r_surfaceMode 2`, `r_surfaceVisibility 1`, `r_surfaceUpscale 9`, `r_neuralFastBuffers 1`, `r_fbo 1`, gamma 1/overbright 0. The packet reproduces the original `r_neuralView 7` capture setting; final raw arrays come from the internal surface output.
4. Normal batch exit leaves the fresh home and log intact. A capture stem writes its completion JSON below `baseoa/captures`. Pending/rejected JSON and stale same-stem output from another run are invalid completion evidence.
5. The read-only checker validates the batch. It reads metadata, file sizes and the two actual 96-byte frames; optional `--verify-sha` streams the existing arrays. The checker requires the expected whole A directional/small32 SHA values for this acquisition:

```powershell
python -B reproduction/capture/check_capture.py --plan work/capture-pre-a/PLAN.json --batch 0 --home YOUR_FRESH_HOME --directional-sha256 546a3d123111056e155f4f0491245775375f4047711441997072cb98bd944f01 --small32-sha256 02707afc205514bdd4a70675bc5e74e3e08f483783545822ff2b71406f062ece
```

6. Acquisition covers all 52 batches. The packet's counts are 384 TRAIN + 24 selection; it includes no fresh views. A complete native input set needs all 408 normal completion records and the exact per-tier arrays, not only the first representative view.

For a manual one-view console capture after selecting the correct actual framebuffer extent:

```text
set r_surfaceUpscale 9
set r_surfaceBatchViewID 10000001
wait 8
surfaceCapture captures/pre_a_10000001_A480_m9
wait 12
```

Representative cameras are 10020001 at 960x540 for B240 and 10030001 at 512x288 for C128. Interactive extent changes require mode9 first, then `r_mode -1`, `r_customWidth` and `r_customHeight`, followed by `vid_restart`, frozen camera-ID selection and a settling wait. Starting each batch at its native extent is the documented original acquisition procedure.

## Files and teacher pairing

| Capture suffix | Type / shape |
|---|---|
| `.lr.rgba32f` | associated LINEAR RGBA float32, HxWx4 |
| `.lr.ids.u32` | native quarter IDs uint32, 2Hx2W; 0xffffffff is miss |
| `.lr.bary.f32` | native quarter barycentrics float32, 2Hx2Wx2 |
| `.lr.center.u32` | native LR receiver Boolean uint32, HxW |
| `.frame.lr.bin` | actual LR 96-byte frame |
| `.frame.hr.bin` | actual target 96-byte frame |
| `.extent.bin` | four uint32 values: targetW,targetH,LRW,LRH |
| `.rgba32f` | target-size bilinear DISPLAY associated RGBA baseline |
| `.json` | `NL4X_MULTIRES_RUNTIME_CAPTURE_1`, completion and all array SHA bindings |

The physical teacher uses the **same capture's `.frame.hr.bin`** at that target's native resolution. The first 64 bytes of the LR and HR frames must be identical to each other and the frozen actual camera words. Only their dimensions differ. Rounded angles are not teacher-camera authority, and target rendering requires the HR frame's dimensions.

```powershell
blender --background --factory-startup --python reproduction/scene/render_teacher.py -- --view-id 10000001 --frame YOUR_FRESH_HOME/baseoa/captures/pre_a_10000001_A480_m9.frame.hr.bin --out work/teacher/10000001 --device CPU
```

B240/C128 teacher generation uses the corresponding captured HR frame; the cohort gives 960x540 or512x288 teacher dimensions. Teacher samples remain 512, seed 11, adaptive sampling off, denoising off. The teacher writes native associated LINEAR and DISPLAY arrays plus the HR center receiver mask. These arrays and capture paths go into the explicit registry for `reproduction/training/prepare_manifest.py`. RAW IDs are consumed as an exact signed int32 bit reinterpretation; no geometry is resized.

Completion checking is not independent physical first-hit parity or clean-reference noise certification. Full A fitting uses the actual LR quarter guides and exact4xassociated-LINEAR teacher reduction. C16 uses only actual corrected LR color and native DISPLAY labels.

## Recapture after an A refit

The `pre_a` packet is the upstream acquisition against an admitted initial field. The main reproduction recipe fits directional A, then small32. Fresh A assets must pass the strict identity generation/admission procedure in [the identity procedure](../../docs/IDENTITY.md) before a new native build can load them. Hash checks remain required; a rejection checker is not a successful capture.

Admission and rebuilding of the new A pair precede preparation of a distinct packet:

```powershell
python -B reproduction/capture/make_packets.py --phase new_a --out work/capture-new-a
```

All 52 batches use the **newly admitted A pair** copied under the same `surface/matched_A.nlsf` and `surface/matched_A.nlsr` filenames. Capture checks require the new SHA values. This is the required actual-new-A LR recapture before training C16; the camera cohort and native teacher labels remain the same. C16 may ignore the recaptured quarter-guide arrays as model inputs, but must not substitute the old A's LR color.

The release includes source and exact historical settings, not a newly tested unattended full rebuild. Identity/source provenance requirements remain strict. Portable refit receipts alone are not a replacement for the authenticated upstream/QA lineage required by the original identity generator.
