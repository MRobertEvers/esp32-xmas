# Vendored from oldschool-clientc

Plain files, not a submodule. Do not edit them here: change the library in
[oldschool-clientc](https://github.com/MRobertEvers/oldschool-clientc), then
re-run `scripts/vendor-clientc.sh <working copy> [commit]` and commit the result.

| | |
|---|---|
| oldschool-clientc | `ca628472a0aa8093306402e88bf6571e36f081e2` |
| OSRS-Content (pinned by that commit) | `a0fccbd8fe363895ecacd6e904ebe415563b3c3b` |

What is here, at the same paths as in oldschool-clientc so `-DCLIENTC_DIR`
can still point the build at a full working copy instead:

- `3rd/toridraw`, `3rd/rscache`, `3rd/toridraw_rscache`: the firmware components.
- `3rd/bzip`, `3rd/miniz`, `3rd/xteas`: only for the host bake/extract tools.
- `OSRS-Content/osrs-content/configs/all.seq`: animation frame ids and hold times for `ANIM_SEQ`.

The OSRS caches are not vendored; see `CACHE_ROOT` in the top-level README.
