# Review and publish locally

Publication is a manual Git operation. This repository uses the local branch `master`; source preparation performs no remote writes.

1. Review covers README.md and the three native images at 100% scale, including the introduction, attribution and acknowledgements. Architecture, byte accounting, original timing scope and limitations remain factual release constraints.
2. LICENSE.md and docs/LICENSING.md define MIT for original independently authored code and original generated artifacts within the rights held by the project author. GPL-derived engine/renderer files and all inherited components retain their own terms and notices; the complete renderer is not uniformly MIT. The provenance metadata records this scope.
3. Run `python tools/verify_assets.py` and review provenance/MANIFEST.json. Game PK3/BSPs, compiled binaries, SDKs, private receipts, credentials and datasets are excluded.
4. Inspect the staged diff and ignored generated files:

```powershell
git status --short
git log --oneline -3
git ls-files
git diff HEAD
```

5. New commits require the contributor's chosen public author name and public/noreply email. Commit metadata describes release preparation independently of source-file attribution. Snapshot hashes are refreshed after deliberate edits:

```powershell
git config user.name "Author Name"
git config user.email "Public or GitHub noreply email"
git add README.md LICENSE.md docs licenses provenance/EXPERIMENT.json
python tools/refresh_manifest.py
git add provenance/MANIFEST.json
python tools/verify_release.py
git commit -m "Review C16 release documentation and license scope"
```

6. The destination is `alegator-cs/bake_neural_upscale_demo`. The local repository already contains the reviewed files and history, and `origin` refers to that destination. Publication checks the configured remote before pushing `master`:

```powershell
git remote -v
git push -u origin master
```

These commands describe manual publication through authenticated Git access; documentation preparation does not execute the push or change authentication. The included sources derive from a specific upstream Quake3e commit, not current upstream HEAD.

Release ZIPs contain the tracked source/assets/docs at their recorded commits, without .git, build outputs or private audit files. The October 12 review bundle predates the scoped MIT grant; publication uses the current committed license files. Original packages and hashes remain historical comparison evidence. The sibling private/ directory contains local source paths and historical receipts and is excluded from publication.

The Windows build and CPU-only preparation/integrity checks passed. No new renderer, bake, training or benchmark was run during release preparation. Original pixel evidence remains the October 10 native captures. Full offline retraining has not been rerun from the portable recipe.
