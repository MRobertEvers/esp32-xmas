"""
Build catalog.json from the .xmb bundles in a directory.

    python server/make_catalog.py build/bundles

The catalogue is DERIVED FROM THE BUNDLES, not written alongside them. Every
field here is read out of the bundle's own header, so a catalogue cannot claim
a size, a hash or a view cost that the file does not actually have -- which is
the failure that would send a device a model it has already been told will fit.

The device relays this document to the phone verbatim and never parses it (see
catalog_get in main/http_api.c), so the shape below is a contract with the
page in main/www/app.html, not with the firmware.
"""

import hashlib
import json
import os
import struct
import sys

XMB_MAGIC = 0x31424D58
XMB_VERSION = 1

# Must match enum XmbSection in main/xmb_format.h. The check below catches a
# mismatch immediately: with the wrong count the header size is wrong, and
# total_size will not agree with the file length.
XMB_SECTION_COUNT = 28

HEADER = (
    "<"
    "4I"      # magic, version, total_size, flags
    "i"       # model_id
    "32s32s"  # name, seq
    "8i"      # vertex, face, textured, face_bones, vertex_bones, base_len, frames, textures
    "6i"      # bounds cylinder
    "I"       # view_bytes
    "3i"      # limit max_faces, max_vertices, depth_levels
    "I"       # limit_textures
    + "%dI" % (XMB_SECTION_COUNT * 2)
    + "32s"   # sha256
)


def read_bundle(path):
    with open(path, "rb") as f:
        blob = f.read()

    size = struct.calcsize(HEADER)
    if len(blob) < size:
        raise SystemExit("%s: too short to be a bundle" % path)

    fields = struct.unpack(HEADER, blob[:size])
    (magic, version, total_size, _flags, model_id, name, seq) = fields[:7]
    (vertices, faces, textured, face_bones, vertex_bones, base_len, frames, textures) = fields[7:15]
    view_bytes = fields[21]
    sha = fields[-1]

    if magic != XMB_MAGIC:
        raise SystemExit("%s: not a bundle (magic %08x)" % (path, magic))
    if version != XMB_VERSION:
        raise SystemExit("%s: version %d, this script writes %d" % (path, version, XMB_VERSION))
    if total_size != len(blob):
        raise SystemExit(
            "%s: header says %d bytes, the file is %d -- the header layout here and in "
            "xmb_format.h have drifted apart" % (path, total_size, len(blob))
        )

    # Hashed over the payload, exactly as the device does after writing it to
    # flash, so a catalogue entry and a device-side verification agree.
    payload_sha = hashlib.sha256(blob[size:]).digest()
    if payload_sha != sha:
        raise SystemExit("%s: the bundle does not match its own hash" % path)

    return {
        "name": name.split(b"\0")[0].decode("utf-8", "replace") or os.path.basename(path),
        "seq": seq.split(b"\0")[0].decode("utf-8", "replace"),
        "model": model_id,
        "size": total_size,
        "sha256": sha.hex(),
        "vertices": vertices,
        "faces": faces,
        "frames": frames,
        "textures": textures,
        # What the device's view arena must hold. The page greys out anything
        # larger than the device reports, so a model that cannot be drawn is
        # never offered rather than being offered and then refused.
        "view_bytes": view_bytes,
    }


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "dist/bundles"
    models = []

    for entry in sorted(os.listdir(root)):
        if not entry.endswith(".xmb"):
            continue
        info = read_bundle(os.path.join(root, entry))
        info["file"] = "bundles/" + entry
        models.append(info)
        print(
            "%-28s %7d bytes  view %6d  %4d faces  %2d frames"
            % (entry, info["size"], info["view_bytes"], info["faces"], info["frames"])
        )

    out = os.path.join(os.path.dirname(root) or ".", "catalog.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"models": models}, f, indent=2)

    print("\nwrote %s with %d model(s)" % (out, len(models)))


if __name__ == "__main__":
    main()
