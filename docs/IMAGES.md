# Matched native C16 images

These three images preserve the October 10 ExperimentalC16 checkpoint at the same held-out close-up camera, view **12000017**. They are original **1920 x 1080** exports, copied without resizing, sharpening or regeneration.

| Image | What it contains | PNG bytes | SHA-256 |
|---|---|---:|---|
| [ExperimentalC16](images/12000017-c16.png) | Actual native engine capture: corrected 480 x 270 appearance, bilinear upsampling, then the C16 residual | 1,717,313 | `03bc3728defd08d6b78711bb548219557205b7b3cb752b437885d9e4e88893a5` |
| [Bilinear](images/12000017-bilinear.png) | Actual native engine capture using the same baked appearance and first correction | 1,238,350 | `a5548724fb1b8f38d1ba5ce1d699fcf952e3291fd8f0bab8b04340a5be050286` |
| [Physical reference](images/12000017-reference.png) | Native physical render, uniform 512 samples per pixel, seed 11 | 2,463,469 | `c2a4220527f4c736e6c930948a6d20bed28c3a3af2c37d22d8291cc886a4f118` |

The three inputs have identical frozen 96-byte camera/extent frames. Bilinear and C16 also share byte-identical captured first-pass LR color. The PNGs show associated display RGB on black; their stored RGB is rounded to eight bits once. There is no second alpha multiplication or extra display transfer. The release check compared every PNG RGB byte with that conversion of the original float32 output and verified exact equality. The pixels show the generated material scene, without a game map, HUD or fonts.

Comparison at 100% scale shows sharper torus, sphere and pedestal boundaries in C16 than in bilinear. The close-up baked lighting remains visible, together with phase-grid edge patterns and broad, banded reflection structures. The physical reference is visibly noisy and remains sharper in several specular structures.

Across the original 24 held-out cameras, the descriptive mean RGB MSE was **0.0002571** for C16 versus **0.0005236** for bilinear; boundary MSE was **0.0015742** versus **0.0037379**. False-bright pixels increased from **301.1** to **627.5** per view. These tradeoffs support an experimental demonstration, not a clean-reference or overall-quality winner claim. The sampled noise repeats failed four of nine original checks, and screen-space motion differences do not certify absence of shimmer.

Original CPU/native checks covered deterministic pixel samples in six selection views, not every native pixel or all fresh cameras. C16's largest sampled RGBA difference was **1.788139343 x 10^-7**. Existing coplanar triangle/chart ambiguities remain separate from appearance parity.

Historical whole-GPU-frame medians were **9.042 ms** for bilinear and **13.790 ms** for C16 on the recorded GTX 1650 Max-Q. These are descriptive single-frame snapshots across the 24 fresh cameras, not a controlled FPS benchmark or matched-quality speedup. The base performs 518,400 LR quarter visibility queries per frame; C16 adds no HR guides or rays.

This set belongs to the preserved October 10 pipeline. Later JBU, coverage and overnight neural upscaling prototypes are separate experiments and are not represented by these screenshots.
