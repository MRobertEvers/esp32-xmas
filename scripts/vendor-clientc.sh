#!/bin/sh
# Re-vendor the parts of oldschool-clientc this firmware builds from into
# 3rd/oldschool-clientc/, as plain files pinned to one commit.
#
#   scripts/vendor-clientc.sh <oldschool-clientc working copy> [commit]
#
# commit defaults to the working copy's HEAD. Files come from `git archive` of
# that commit, so uncommitted edits and ignored files (caches, toolchains,
# build output) never leak in. all.seq comes from the OSRS-Content commit that
# the clientc commit pins: from the working copy's OSRS-Content checkout when it
# has that commit, otherwise fetched from GitHub (one file, blobless).
#
# Writes 3rd/oldschool-clientc/VENDORED.md with both commits. Review with
# `git status` / `git diff --stat` and commit the result like any other change.
set -eu

SRC=${1:?usage: $0 <oldschool-clientc working copy> [commit]}
REV=${2:-HEAD}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${VENDOR_DEST:-$ROOT/3rd/oldschool-clientc}
PATHS="3rd/toridraw 3rd/rscache 3rd/toridraw_rscache 3rd/bzip 3rd/miniz 3rd/xteas"
SEQ=osrs-content/configs/all.seq
OSRS_URL=https://github.com/MRobertEvers/OSRS-Content.git

COMMIT=$(git -C "$SRC" rev-parse --verify "$REV^{commit}")
OSRS=$(git -C "$SRC" ls-tree "$COMMIT" OSRS-Content | awk '$2 == "commit" { print $3 }')
[ -n "$OSRS" ] || { echo "no OSRS-Content gitlink at $COMMIT" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# shellcheck disable=SC2086
git -C "$SRC" archive "$COMMIT" $PATHS | tar -x -C "$TMP"

mkdir -p "$TMP/OSRS-Content/$(dirname "$SEQ")"
if git -C "$SRC/OSRS-Content" cat-file -e "$OSRS:$SEQ" 2>/dev/null; then
    git -C "$SRC/OSRS-Content" show "$OSRS:$SEQ" > "$TMP/OSRS-Content/$SEQ"
else
    mkdir "$TMP/osrs.git"
    git -C "$TMP/osrs.git" init -q
    git -C "$TMP/osrs.git" fetch -q --depth 1 --filter=blob:none "$OSRS_URL" "$OSRS"
    git -C "$TMP/osrs.git" show "$OSRS:$SEQ" > "$TMP/OSRS-Content/$SEQ"
    rm -rf "$TMP/osrs.git"
fi

cat > "$TMP/VENDORED.md" <<EOF
# Vendored from oldschool-clientc

Plain files, not a submodule. Do not edit them here: change the library in
[oldschool-clientc](https://github.com/MRobertEvers/oldschool-clientc), then
re-run \`scripts/vendor-clientc.sh <working copy> [commit]\` and commit the result.

| | |
|---|---|
| oldschool-clientc | \`$COMMIT\` |
| OSRS-Content (pinned by that commit) | \`$OSRS\` |

What is here, at the same paths as in oldschool-clientc so \`-DCLIENTC_DIR\`
can still point the build at a full working copy instead:

- \`3rd/toridraw\`, \`3rd/rscache\`, \`3rd/toridraw_rscache\`: the firmware components.
- \`3rd/bzip\`, \`3rd/miniz\`, \`3rd/xteas\`: only for the host bake/extract tools.
- \`OSRS-Content/$SEQ\`: animation frame ids and hold times for \`ANIM_SEQ\`.

The OSRS caches are not vendored; see \`CACHE_ROOT\` in the top-level README.
EOF

rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
mv "$TMP" "$DEST"
trap - EXIT
chmod 755 "$DEST"
echo "vendored oldschool-clientc $COMMIT (OSRS-Content $OSRS) into $DEST"
