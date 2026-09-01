"""
Bake a list of models into .xmb bundles and write the catalogue.

    python server/bake_bundles.py 2851:entendseq 2851:entready

Each argument is `<dat2 model id>:<sequence name>`, optionally with a display
name: `2851:entendseq:Spirit tree`. A sequence of `-` bakes the model static,
with no animation: `1500:-:Something Still`.

This is the server side of the same bake the firmware's own model goes through
-- tools/bake_model/bake_model.c with --format bin instead of --format c. The
decode, the lighting and the texture resampling are the identical code path, so
a downloaded model cannot look different from a compiled-in one.

Paths default to this checkout's layout; override with the environment:

    CACHE_DIR   dat2 cache             ../oldschool-clientc/cache.osrs239
    CLIENTC_DIR the vendored library   3rd/oldschool-clientc
    OUT_DIR     where bundles land     dist/bundles

Bundles land in dist/, NOT build/. `idf.py fullclean` empties build/, and a
model library that disappears because someone cleaned the firmware build is a
bad afternoon -- baking one takes a cache checkout most machines do not have.
"""

import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CLIENTC_DIR = os.environ.get("CLIENTC_DIR", os.path.join(REPO, "3rd", "oldschool-clientc"))
CACHE_DIR = os.environ.get(
    "CACHE_DIR", os.path.join(os.path.dirname(REPO), "oldschool-clientc", "cache.osrs239")
)
OUT_DIR = os.environ.get("OUT_DIR", os.path.join(REPO, "dist", "bundles"))
BAKER = os.path.join(REPO, "tools", "bake_model", "build", "bake_model.exe")

# The look. Same defaults as the firmware bake -- see the lighting table in
# README.md for what moves the contrast and what only clips it.
LIGHT = ["--ambient", "128", "--attenuation", "192", "--light", "-50,-10,-50", "--gamma", "1.0"]


def read_sequence(name):
    """
    The frame ids and their hold times, from all.seq.

    BOTH fields matter. The id says which pose; the second says how many 50 Hz
    client cycles to hold it, and it exists ONLY here -- the frame archive does
    not carry it. Dropping it leaves every frame at delay 0, which a player
    renders as the whole sequence at fifty poses a second.
    """
    path = os.path.join(CLIENTC_DIR, "OSRS-Content", "osrs239-content", "configs", "all.seq")
    if not os.path.exists(path):
        raise SystemExit(
            "all.seq not found at %s -- did you clone with --recursive?" % path
        )

    frames, delays = [], []
    in_record = False

    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            t = line.rstrip()
            if t == "[%s]" % name:
                in_record = True
                continue
            if in_record and t.startswith("["):
                break
            if in_record and t.startswith("frame="):
                parts = t[6:].split(",")
                frames.append(parts[0])
                delays.append(parts[1] if len(parts) > 1 else "0")

    if not frames:
        raise SystemExit("sequence [%s] has no frames in all.seq" % name)

    return frames, delays


def bake(model_id, seq, display_name):
    static = seq == "-"
    frames, delays = ([], []) if static else read_sequence(seq)
    out = os.path.join(OUT_DIR, "%s-%s.xmb" % (model_id, "static" if static else seq))

    cmd = [
        BAKER, CACHE_DIR,
        "--rev", "osrs239",
        "--model", str(model_id),
    ]
    if not static:
        cmd += ["--frames", ",".join(frames), "--delays", ",".join(delays)]
    cmd += [
        "--format", "bin",
        "--name", display_name,
        "--seq", "" if static else seq,
    ] + LIGHT + [out]

    print("baking model %s / %s (%d frames)" % (model_id, seq, len(frames)))
    result = subprocess.run(cmd, capture_output=True, text=True)

    # The baker reports what it did on stderr -- the lighting histogram, the
    # frame count, the view cost. It is worth seeing when a bundle comes out
    # wrong, and worth staying quiet when it does not.
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit("bake failed for %s/%s" % (model_id, seq))

    for line in result.stderr.splitlines():
        if line.startswith(("wrote", "view will need", "texture", "W ")):
            print("  " + line)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    if not os.path.exists(BAKER):
        raise SystemExit(
            "%s not found. Build it once with an ordinary firmware build "
            "(idf.py build), which compiles the baker as a side effect." % BAKER
        )

    os.makedirs(OUT_DIR, exist_ok=True)

    for arg in sys.argv[1:]:
        parts = arg.split(":")
        if len(parts) < 2:
            raise SystemExit("expected <model>:<seq>[:<name>], got %r" % arg)
        model_id, seq = parts[0], parts[1]
        name = parts[2] if len(parts) > 2 else "Model %s" % model_id
        bake(model_id, seq, name)

    print()
    subprocess.run([sys.executable, os.path.join(REPO, "server", "make_catalog.py"), OUT_DIR])


if __name__ == "__main__":
    main()
