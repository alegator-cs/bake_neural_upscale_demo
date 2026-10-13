# Licensing and external assets

This document records the provenance established for the preserved October 10 Experimental C16 release. The original, independently authored Neural Light code and original generated scene/bake/model/image/documentation artifacts are licensed under MIT, within the rights held by the project author. The full grant and path/category map are in [LICENSE.md](../LICENSE.md). Inherited third-party terms remain unchanged.

## Engine source

The included engine source is a modified snapshot of [Quake3e](https://github.com/ec-/Quake3e), built on ioquake3/id Software's Quake III Arena source. Quake3e's Vulkan renderer also credits Quake-III-Arena-Kenny-Edition. Preserve the upstream copyright notices, `COPYING.txt`, `id-readme.txt`, and component notices. The local `qcommon/common.c` and `renderervk/tr_local.h` headers grant GPL version 2 or later; the repository carries the GPL version 2 text. Neural Light changes integrated into this engine should be distributed under compatible GPL terms. A blanket MIT/Apache license must not replace the upstream engine notices.

This is a modified research snapshot, not an unmodified upstream release. Its exact file hashes are the reproducibility identity. The original checkout's `main` and `origin/main` references identify [Quake3e commit `fe57ee907f4b858a09c6b64348f08750b2b469a2`](https://github.com/ec-/Quake3e/commit/fe57ee907f4b858a09c6b64348f08750b2b469a2) as the checkout base. The isolated experimental copy has no Git history of its own; use the preserved file-hash manifest for the local modifications and do not substitute a current upstream HEAD.

The [GPLv2 text](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) requires notices and license terms to remain with redistributed source and sets additional requirements for executable distribution. This package deliberately distributes source, rather than the locally built executable. A future binary release must include the appropriate corresponding source and build materials.

## Third-party code and build dependencies

| Component found in the preserved source | Evidence to retain | Release treatment |
|---|---|---|
| id Software/ioquake3/Quake3e engine and derived renderer | `quake3e/COPYING.txt`, per-file notices, `quake3e/id-readme.txt`, upstream README | Include source and unchanged notices; describe Neural Light modifications. |
| ZIP/zlib routines and other original Quake components | Exceptions/notices in `id-readme.txt` and source headers | Retain notices; do not relabel them as original Neural Light code. |
| Independent JPEG Group release 9e | `code/libjpeg/README`, including LEGAL ISSUES | Include the README unchanged with redistributed JPEG source. This software is based in part on the work of the Independent JPEG Group. |
| Ogg and Vorbis | `code/libogg/COPYING`, `code/libvorbis/COPYING` | Retain the copyright, conditions and disclaimer. |
| Optional curl headers, local version 8.4.0 | Header SPDX identifier `curl`; [version 8.4.0 COPYING](https://raw.githubusercontent.com/curl/curl/curl-8_4_0/COPYING) | Keep the matching complete permission notice with these retained headers. The staged Windows MSVC projects disable `USE_CURL`/`CURL_STATICLIB` and remove the static curl link dependencies in all configurations; curl is not required by that release build. No prebuilt curl library is included. Users who later enable curl must supply that optional dependency themselves. |
| SDL headers | Inline zlib-style notices in `code/libsdl/include/SDL2` | Retain inline notices if these optional backend headers are included. No SDL library binary is included. |
| Vendored Khronos Vulkan API headers | All six `code/renderercommon/vulkan/*.h` carry Apache-2.0 identifiers and Khronos copyright notices | Retain the inline notices and the existing marked `vulkan_core.h` backport; include `licenses/Apache-2.0.txt` and `licenses/Khronos-Vulkan-NOTICE.txt`. These are retained engine dependencies, not original Neural Light code. |
| Khronos OpenGL extension declarations | Complete permissive notice in `code/renderer2/glext.h` | Retain this notice unchanged. It is distinct from the Apache-licensed Vulkan headers. |
| Vulkan SDK, Microsoft compiler/SDK, Python, NumPy, PyTorch, Blender/Cycles | Separately installed tools, not vendored software | Document required versions. Do not bundle installed runtimes, SDK files or their compiled libraries. |

The preserved original Windows project linked a prebuilt static curl library. The release preparation removes that dependency without changing renderer or model mathematics. The binary remains excluded from this source-only package. This is not a finding that curl cannot legally be redistributed: curl is available under its [own permission notice](https://curl.se/docs/copyright.html). Any later packaging with curl enabled must preserve the notices for the exact supplied build and its dependencies.

The source bundle has separate GPL engine, Apache-2.0 Vulkan-header, MIT original-work and other permissive component terms; it must not be described as uniformly MIT or GPLv2-only. The [Apache Software Foundation's compatibility guidance](https://www.apache.org/licenses/GPL-compatibility.html) states that Apache-2.0 is compatible with GPLv3 but not GPLv2. The preserved core engine headers allow GPLv2 or later. A future combined executable's license treatment must resolve this combination explicitly, preserving component notices and the available later-version grant. The MIT original-work grant does not override those terms or clear an exclusively GPLv2 binary combination.

## Procedural scene, fitted assets and images

The recovered alcove creator uses Blender primitives, numeric Principled material parameters, explicit area lights and a black world. No external mesh, image texture or HDRI import was observed in the audited creator. The physical teacher's frozen scene identity and the resulting runtime geometry are recorded separately in the release provenance manifest.

The selected native reference, bilinear and C16 images are pipeline-only images of that procedural scene, without the OpenArena map, HUD, fonts or icons visible. The directional bake, small32 correction and C16 weights are fitted scene-specific artifacts. They are not third-party pretrained weights. The scoped MIT grant covers these original scene/model/image artifacts within the rights held by the project author in their underlying content; it does not follow automatically from the engine's GPL license.

The original scene creator, independently authored standalone fit/build/launch tools, original model definitions, generated geometry/bake/model files, images and documentation are covered by the scoped MIT grant in [LICENSE.md](../LICENSE.md). GPL-derived engine and renderer modifications remain under their inherited GPL terms, including modifications within a file that otherwise contains independently authored content. Existing component notices are not replaced. Original-project attribution uses the project author name **gator**; it does not assert a legal identity, GitHub account name or third-party authorship.

The recovered source/asset hashes and procedural creator establish local provenance. This was not an exhaustive line-by-line authorship audit of every mixed source file, nor an asset-level rights audit of the external host game. If a file contains copied, derived or separately licensed material, its inherited terms take precedence for that material; the MIT grant applies only to original contributions within the rights held by the project author.

## OpenArena and game content

Quake3e [explicitly ships no game content](https://github.com/ec-/Quake3e#readme). The local interactive host uses separately supplied OpenArena data and a BSP world to initialize the engine. Those files are outside this release. The procedural Neural Light transport scene is distinct from that host world.

[OpenArena describes its project as GPLv2](https://www.openarena.ws/about.htm), but that project-level statement does not authenticate every locally installed PK3, map or add-on. Redistribution of a particular game asset would require its authorship, exact version, asset-level notice and preferred source form. No such asset-level audit is claimed here.

Do not include PK3 archives, an external BSP map, game QVMs, UI textures, audio, logos, commercial Quake assets or installed game executables. Users should obtain OpenArena separately and point the launcher at their own installation. Do not silently download game data in a smoke test.

## Publication gate

- Keep the exact upstream/component notices with redistributed source.
- Preserve the scoped MIT original-work grant and its path/category map, without overriding upstream grants.
- Keep engine executables, static libraries, SDK/runtime files and external game data outside the public tree.
- Retain the procedural source and hashes for model/scene/image provenance; do not include training datasets or private machine paths.
- Document the upstream checkout base, local modification hashes and any untested dependency-supply/build step honestly.

Conceptual prior art is credited in [RELATED_WORK.md](RELATED_WORK.md). Citation does not imply code reuse or license coverage for copied material; no code/assets from those research projects are included on the basis of this attribution alone.
