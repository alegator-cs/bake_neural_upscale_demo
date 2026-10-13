# Reproduce the preserved C16 preview

The package includes the exact selected appearance assets, scene geometry and shader source. Start with inference using the preserved assets; offline reconstruction is a separate workflow.

## Windows build and native preview

Prerequisites: Visual Studio 2022 C++ tools/Windows SDK, Vulkan SDK 1.3.290.0 (GLSL/SPIR-V tools), Python, a separately supplied OpenArena 0.8.8 installation and q3map2 compiler. The reference build used Windows SDK 10.0.22000.0; build.ps1 accepts an explicit SDK version. No tools or game data are installed by this package.

From the repository root:

```powershell
python tools/verify_assets.py
powershell -NoProfile -File runtime/build.ps1 -VulkanBin "$env:VULKAN_SDK\Bin"
powershell -NoProfile -File reproduction/map/compile_map.ps1 -OpenArenaRoot "D:\Games\OpenArena" -Q3Map2 "D:\Tools\q3map2.exe" -OutputDirectory "work\map"
New-Item -ItemType Directory -Path "runtime\session\baseoa\maps" -Force
Copy-Item -LiteralPath "work\map\neural_test.bsp" -Destination "runtime\session\baseoa\maps\neural_test.bsp"
powershell -NoProfile -File runtime/run.ps1 -OpenArenaPath "D:\Games\OpenArena" -PrepareOnly
powershell -NoProfile -File runtime/run.ps1 -OpenArenaPath "D:\Games\OpenArena"
```

The build.ps1 arguments `-MSBuildPath`, `-PythonPath` and `-WindowsSDKVersion` override tool discovery and defaults. `-ShadersOnly` compiles and validates GLSL without an engine build. Neither build option launches a renderer.

The launcher defaults to runtime/out/quake3e.x64.exe, runtime/session, FHD and frozen camera 12000017. It copies only the eight pinned procedural runtime files into an isolated home, verifies their hashes, and uses C16 mode 10/full-scene view 6. It reads game content from the supplied installation. `-EnginePath` and `-HomePath` provide explicit overrides. The canonical map compiler needs a fresh output directory; it preserves existing maps. The map source references an external OpenArena texture name, whose bytes are not redistributed.

PrepareOnly writes/validates the setup and starts no engine. The final command opens the interactive renderer. `-FrozenViewId -1` selects free camera mode; the launcher also binds F7 to leave the frozen view. Mouse/WASD control movement; Space/Ctrl move vertically and F10 toggles noclip. Original interactive evidence covered bilinear mouse/WASD, not a new test of this portable C16 launcher.

The original Windows engine has startup command/argument limits; short ASCII installation/home paths avoid those limits. No automatic background job is scheduled.

## Offline reconstruction

[The detailed recipe](../reproduction/docs/REPRODUCTION.md) includes the procedural scene/template, geometry exporter, 408 camera records, native physical teacher settings, actual LR capture commands, data registry builder, A directional/small32 fits and C16 fit/export.

Original tools were Blender 3.6.0/build c7fc78b81ecb and PyTorch 2.5.0+cu124. The selected three fits each ended at 10,000 updates. The recipe/protocol files specify seeds, loss, masks, padding, exact linear 4x reduction and display handling.

The dataset, optimizer checkpoints and private job receipts are omitted. The provided adapters passed CPU header/source/CLI checks and were not run through another bake/train campaign. A new fit must actually recapture its own corrected LR input before C16 training.

The [native capture checklist](../reproduction/capture/README.md) provides a source-only packet builder for all 408 exact cameras, a completion checker and the remaining deliberate batch-launch steps. The [identity procedure](IDENTITY.md) provides the original portable binary validators and a review-only row inspector. Fresh A admission, rebuild, actual new-A recapture, C16 fitting/selection review, C16 admission and a second rebuild remain separate steps. The original private owner-receipt graph is not reconstructed by these tools, and portable FIT.json alone does not admit assets. The included selected assets are admitted already. Fresh end-to-end retraining and native admission are unvalidated.

## What was checked in this package

The portable source-only Windows build passed all 17 shader compile/validation steps and engine linking on one CPU worker. CPU asset/ABI checks and launcher preparation passed. Original appearance bytes and shader numerical code are retained. No new GPU render/parity/benchmark, map compilation, bake or training was performed. The three supplied images remain original October 10 native pixel evidence.

A saved Blender file-browser directory was redacted from the release template only; geometry/material/light/world/camera bytes are unchanged. Both original scene identity and release-file SHA are documented. [Licensing notes](LICENSING.md) describe the scoped MIT grant for original work, retained inherited notices and excluded external game content.
