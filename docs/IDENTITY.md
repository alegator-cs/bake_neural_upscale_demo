# Admit a separately fitted A/C16 experiment

The included assets already match the compiled October 10 allowlists. A fresh
fit changes those whole-file digests and must be admitted separately. The
preserved loader deliberately rejects a new file even when its size matches.
This document supplies a reviewed manual procedure and exact original codec
validators; it is not a one-command, unattended reproduction claim.

Keep the original release and its eight-file launcher manifest unchanged.
Work on a separate branch and fresh output directories. Do not disable the
loader, ignore a checksum, remove finite-value checks, use wildcard identities
or paste arbitrary hashes into a compiled row.

## Original generator dependency graph

The original October 10 owner generator was `full384_runtime_v06/prepare_runtime.py`
(SHA `9e58963e07760bf945c108a510f5d2c3046f3fad071b9dc33843bd4c6b445ca9`).
It reused `prepare_runtime_b.py` (SHA
`3de524015306d87db9b6b3c6b423f3fbb85948f039a201cc281bc1fefbe48ef0`)
and `prepare_runtime_ab.py` (SHA
`05734250b4a76aae7b0fafb3d65e39eb326b3ffc0e51e7a3ddc3635229bea6bf`).
Those original entry points are receipt-aware experiment tools, not portable
commands that accept only three newly trained files.

The task7 generator also authenticated its prepared-v3 source bindings,
original source backups and original pilot assets. The October 10 wrapper
authenticated a production source freeze, the fitted source freeze and its
direct metadata modules `full_contract.py`, `admission.py`,
`exploratory_contract.py`, `run_fit.py` and `a2/contracts.py`. Those validators
then recursively authenticate authority, cohort/publication, teacher/guide,
fit/chunk, recapture and selection receipts. Restoring just the generator would
leave this receipt graph incomplete. Private paths, job receipts and datasets
are deliberately not distributed here.

The original wrapper required four normally completed A/B stages, each 10,000
updates with all 24 selection views, exactly 384 TRAIN with native tier counts 96/160/128,
and matching scene, geometry, cohort, teacher, input, source and protocol SHA
bindings. C16 additionally required the actual newly fitted A recaptures for
all 384 TRAIN plus 24 selection, an unchanged C16 ABI, no selection gradients,
no fresh pixels, and 10,000 updates and endpoint evidence for all 24 selection views. It
handled the actual failed finite-reference QA through an authenticated
noise-limited, nonpromotable branch. The earlier task7 clean-reference evidence
schema must not be made to pass by falsely setting reference-validation true.

`runtime/identity/codec_reference.py` packages the closed portable subset:
nine original functions with unchanged ASTs. Its only dependencies are Python
stdlib hashlib, re, struct, pathlib and types. It retains byte/header/payload
checks; it does not replace the owner/teacher/selection proof graph.
[Validator provenance](../runtime/identity/SOURCE_PROVENANCE.json) records their exact origins.

## Stage A: inspect, review and admit only the new A pair

1. Retain real directional and small32 fit records, actual teacher/guide/frame
   SHA bindings, all 384 TRAIN and 24 disjoint selection IDs, native tier counts,
   source/protocol hashes, final selection results and explicit reference-QA
   limitations. A partial checkpoint is not a completed endpoint. The portable
   adapters' FIT.json alone does not provide the original owner admission or
   independently establish evaluation of all 24 selection views.
2. Run the codec-only inspection after the new A pair exists:

   ```powershell
   python -B runtime/identity/inspect_assets.py --base work/A-directional/directional.nlsf --correction work/A-small32/small32.nlsr --tag refit1 --out work/A-identity-inspection
   ```

   It checks NLSF0001/NLSR0001 headers, FNV/payload SHA, reserved fields,
   normalization, finite FP32 payloads, exact scene/geometry and base pairing.
   The A atlas must remain 16,384 pooled texels and all 88 original four-word
   chart records, including starts, widths, heights and wrap flags. It reads
   the pinned released A assets as the layout reference. The resulting
   INSPECTION.json is byte validation only, never runtime admission.
3. After authentic evidence review, append the exact proposed A row to
   `runtime/quake3e/code/renderervk/surface_experiment_identity.h` in the
   isolated branch. Its fields are `{name,16384,baseBytes,correctionBytes,
   baseWholeSHA,correctionWholeSHA,0}`. Preserve old rows, the original chart
   table/digest, scene/geometry/material/BVH identities, caps, validators and
   resource invalidation. `genuineFHD=0` is required for this coarse A layout.
4. Record the actual new training-manifest SHA in the branch's metadata
   bindings. If updating `SURFACE_MATCHED_TRAIN_MANIFEST_SHA` or related scope
   macros, use the authenticated manifest/noise-disposition bytes, not a hash
   inferred from camera counts. Keep noise/quality/promotion limitations
   truthful; this procedure grants no clean-reference or winner status.
5. Create a separate manifest/launcher for the new asset names and whole SHAs,
   retaining every path, size, finite and identity check. Rebuild the isolated
   engine with the ordinary build procedure. The shipped run.ps1 and original
   assets remain pinned and unchanged. Until C16 is separately admitted, use
   mode 9 for actual LR captures; old C16 must reject the newly paired A.

## Stage C16: actual recapture, inspection, then a second admission

1. Actually recapture the newly admitted A pair for all 384 TRAIN and 24
   selection cameras. Preserve native LR associated-LINEAR color, exact
   frames and runtime/asset receipts. Never substitute resized targets, old-A
   captures or degree-rounded camera reconstruction. Use the [native capture checklist](../reproduction/capture/README.md) and
   [reconstruction recipe](../reproduction/docs/REPRODUCTION.md).
2. Fit C16 with these inputs and genuine native teachers, then evaluate all 24
   selection views independently. Freeze the choice before fresh views. Keep
   model source and protocol bytes used by export; reserializing a JSON
   protocol changes its SHA even if its meaning seems the same.
3. Inspect all three assets and their actual C16 source/protocol files:

   ```powershell
   python -B runtime/identity/inspect_assets.py --base work/A-directional/directional.nlsf --correction work/A-small32/small32.nlsr --cnn work/C16/C16.nlr4 --c16-source reproduction/models/c16_model.py --c16-protocol reproduction/protocols/c16.json --tag refit1 --out work/C16-identity-inspection
   ```

   NLR40001 must have width 16, 4,000 parameters, a 256-byte header, 16,000 payload
   bytes and 16,256 whole bytes. Header offsets 64, 96, 128 and 160 hold the new
   small32 whole SHA, actual model-source SHA, actual protocol SHA and payload
   SHA. Exact decoder/feature flags, scales 1/1, reserved zeros and finite
   FP32 values are mandatory. The model source and protocol arguments must be
   the bytes actually supplied to write_asset; values are not inferred from
   the model header.
4. After reviewing the completed fit, actual-new-A recapture and selection
   evidence, append the exact C16 row to `surface_cnn_identity.h`. Fields are
   `{name,modelWholeSHA,small32WholeSHA,modelSourceSHA,protocolSHA,
   qualityStatus,16,4000,16256,1}`. The proposal deliberately labels itself
   unreviewed; retain experimental/nonpromotable status unless separate
   evidence warrants another truthful description. Preserve the compiled
   selection lookup, baseline/source/protocol checks and actual CNN-digest
   resource invalidation. No payload-only or same-size fallback is allowed.
5. Update the isolated manifest/launcher's three appearance identities, then
   rebuild, validate shaders and run authorized native numerical checks before
   calling that new experiment runtime-ready. New native screenshots and
   timing require a separately available execution slot. Code inspection and
   old parity do not certify the new weights or build.

## Native capture extent boundary

Mode 9 alone admits the reconstruction tiers: 1920x1080 -> 480x270,
960x540 -> 240x135 and 512x288 -> 128x72, with scale 1 and real quarter-site
IDs/barycentrics/receiver gates. Mode 10 C16 and mode 11 bilinear in this preserved
build require 1920x1080 output with 480x270 input. Their lower-resolution
rejection output is not data. The same C16 network can be trained on 4x native
tier pairs, but the packaged runtime endpoint remains FHD-only.

The original source admits those mode 9 extents through
surface_capture4x_extent/surface_fhd_draw and routes mode 9 capture to
vk_surface_multires_capture.inc before the mode>=10 CNN capture branch.
Original completed B240/C128 new-A capture owners used 960x540/512x288 with
scale 1 and mode 9. Current capture preparation creates metadata/config packets
only; no renderer job is implied by generating them.

No compile, renderer, training, GPU parity or performance job was run while
preparing this identity handoff. Newly fitted end-to-end runtime admission
remains a reviewed multi-stage operation; the supplied screenshot identities
continue to refer only to the preserved October 10 assets.
