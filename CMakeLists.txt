cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32s3)

#
# WHERE THE LIBRARIES COME FROM. ToriDraw, RSCache and the adaptor between them
# all live in oldschool-clientc/3rd, vendored here as a SUBMODULE pinned to a
# commit -- 3rd/oldschool-clientc, see .gitmodules.
#
# It used to be `../oldschool-clientc`: whatever happened to be checked out in
# a sibling directory on the machine doing the build. That is not a version.
# Two people with different working copies got different firmware from the same
# source tree and nothing recorded the difference, and the raster kernels this
# client depends on are developed in that repo, so "different" was routine.
# A submodule makes the dependency a commit hash in this repo's history.
#
# CLIENTC_DIR still overrides it, which is what you want while developing
# against a working copy of the library:
#
#   idf.py -DCLIENTC_DIR=/path/to/oldschool-clientc build
#
if(NOT DEFINED CLIENTC_DIR)
    set(CLIENTC_DIR "${CMAKE_CURRENT_LIST_DIR}/3rd/oldschool-clientc"
        CACHE PATH "Path to the oldschool-clientc repository")
endif()

if(NOT EXISTS "${CLIENTC_DIR}/3rd/toridraw/toridraw_unity.c")
    message(FATAL_ERROR
        "oldschool-clientc not found at ${CLIENTC_DIR}.\n"
        "If this is a fresh clone, the submodule is not checked out yet:\n"
        "    git submodule update --init --recursive\n"
        "Or point the build at a working copy with -DCLIENTC_DIR=<path>.")
endif()

#
# WHICH MODEL. Two ways in, and MODEL_FILE wins when it is set.
#
# A raw archive from OSRS-Content is named, already decompressed, and needs no
# cache -- which is the short road when what you know about a model is what it
# is called. MODEL_ID goes through a dat1 cache and is there for the case where
# all you have is a number.
#
set(MODEL_FILE "" CACHE FILEPATH "raw .model archive to flash; overrides MODEL_ID when set")
set(MODEL_ID "7" CACHE STRING "dat1 model archive id, used when MODEL_FILE is empty")
#
# THE CACHES ARE DATA, AND DATA IS NOT VENDORED.
#
# CLIENTC_DIR is a pinned submodule, and oldschool-clientc gitignores `cache.*/`
# -- so the submodule carries the libraries and the configs but no cache. The
# caches are gigabytes of game data that do not belong in either repo's history,
# so they stay a local prerequisite and get their own root.
#
# Pointing these at CLIENTC_DIR would look tidier and would silently break every
# fresh clone, because the directory exists and is empty.
#
set(CACHE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../oldschool-clientc"
    CACHE PATH "Where the OSRS caches live; NOT the vendored submodule")

set(CACHE_DIR "${CACHE_ROOT}/cache.rs289lc" CACHE PATH "dat1 OSRS cache directory")

#
# An ANIMATED model: a dat2 cache, one model id and a sequence's frame ids.
# When ANIM_MODEL is set the blob carries the rig and the frames as well, and
# the client poses the model every frame instead of drawing the rest pose.
#
# The defaults are treevillage_spirittree / entready from osrs239: the Tree
# Gnome Village spirit tree and its idle sway.
#
# NOT pog_spirit_tree_anim / pog_spirit_tree_transform, which was the first
# choice and is wrong for this. That loc is `models=49769,49771` -- a PAIR --
# and the transform sequence hides whichever tree is not current by collapsing
# every one of its vertices onto a single point. Rendering one model of the
# pair therefore draws nothing for half the sequence: measured on the part, 48
# of its 99 poses came out with bounds radius 0 and min_y == max_y. That reads
# on the panel as flicker, and it is the animation behaving correctly.
#
# A `readyanim` has no such partner. It loops, every frame has geometry, and
# that is what makes it a benchmark rather than a light switch.
#
set(ANIM_CACHE_DIR "${CACHE_ROOT}/cache.osrs239" CACHE PATH "dat2 cache for the animated model")
set(ANIM_REV "osrs239" CACHE STRING "revision profile for ANIM_CACHE_DIR")
set(ANIM_MODEL "2851" CACHE STRING "dat2 model id to animate; empty disables the animated path")
# entendseq, NOT entready.
#
# Both drive model 2851 and both are idles. The difference is the orbs in the
# branches, and it is deliberate: all.loc ships `spirittree_big_2ops` with
# anim=entready and `spirittree_big_2ops_orbs` with anim=entendseq.
#
# The rig has two type-5 (transparency) groups. entready holds them at a
# constant +16 and +32; +32*8 = 256, which clamps to 255 and hides the orbs on
# every frame -- correct, that loc has no orbs. entendseq ramps the second
# group 20->6 and back and dips the first to -10, which is the fade.
#
# So a sequence being an "idle" for a model does not mean it shows all of it.
set(ANIM_SEQ "entendseq" CACHE STRING "sequence name in OSRS-Content all.seq")

#
# LIGHTING, applied at bake time.
#
# lightness = ambient + (L.N) / (attenuation * face_count), and that scales the
# face's own base lightness before the raster sees it, clamped to [2,126].
#
#   LIGHT_AMBIENT      the floor. Slides the whole model brighter or darker.
#   LIGHT_ATTENUATION  DIVIDES the directional term, so LOWER means a wider
#                      swing either side of the floor -- this is the contrast
#                      knob. The reference ships 768.
#   LIGHT_DIR          the light vector, x,y,z.
#
# The bake prints the resulting lightness range and how much of it landed on
# the clamps, so a value that is only clipping says so instead of looking like
# more contrast.
#
# Measured on THIS model, reading the spread the bake reports. The number that
# matters is the INNER spread, p10..p90 -- the min/max are two faces, the
# quartiles are the thousand in between, and it is the latter the eye reads as
# contrast:
#
#   ambient  atten  gamma   inner  mean  clipped
#      64     768    1.0      --     24     0%    <- the reference: dark, flat
#      96     384    1.0      69     37     0%
#      96     192    1.0      88     38     2%
#      96     192    0.8      87     46     2%    <- conservative alternative
#     128     192    1.0     105     49     7%    <- here
#      96     128    1.0     108     39    17%    <- spread bought by clipping
#
# ATTENUATION is the contrast knob; it is the only one that moves the inner
# spread. GAMMA re-curves the distribution and is a BRIGHTNESS knob -- it was
# measured moving the mean 37 -> 73 while SHRINKING the inner spread 69 -> 55,
# so it must not be reached for when the complaint is contrast.
#
# 7% on the clamps is the price here: those face corners are pinned at 2 or 126
# and have lost their shading. 96/192/0.8 above is the same contrast at 2% if
# that shows.
set(LIGHT_AMBIENT "128" CACHE STRING "lighting floor; reference is 64")
set(LIGHT_ATTENUATION "192" CACHE STRING "directional divisor; LOWER is more contrast, reference 768")
set(LIGHT_GAMMA "1.0" CACHE STRING "lightness re-curve; <1 brightens, does NOT add contrast")
set(LIGHT_DIR "-50,-10,-50" CACHE STRING "light direction x,y,z")

#
# The framebuffer word. RGB565_BE is RGB565 with the high byte at the low
# address, which is what an SPI ST7789 clocks out -- so the palette is baked
# in the panel's own order and the render path never converts. See
# 3rd/toridraw/graphics/pixel_format.h. The tables below are generated for
# THIS value and refuse to compile against another.
#
set(TORIDRAW_PIXEL_FORMAT_NAME "TORIDRAW_PF_RGB565_BE"
    CACHE STRING "toripixel_t layout; must match the generated tables")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_xmas)

set(TORIDRAW_TABLES_C "${CMAKE_BINARY_DIR}/gen/toridraw_tables_precomputed.c")
set(GEN_CONST_TABLES_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen-const-tables.ps1")
set(BAKE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bake-model.ps1")
set(XMAS_BAKED_C "${CMAKE_BINARY_DIR}/gen/xmas_baked.c")


add_custom_command(
    OUTPUT "${TORIDRAW_TABLES_C}"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${GEN_CONST_TABLES_SCRIPT}"
        -ClientcDir "${CLIENTC_DIR}"
        -PixelFormat "${TORIDRAW_PIXEL_FORMAT_NAME}"
        -OutFile "${TORIDRAW_TABLES_C}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating the toridraw lookup tables as const flash arrays"
    VERBATIM
)

add_custom_target(toridraw_tables_gen ALL DEPENDS "${TORIDRAW_TABLES_C}")
add_dependencies(__idf_toridraw toridraw_tables_gen)

#
# The model and animation as const C, compiled into the app rather than decoded
# at boot; tools/bake_model/bake_model.c says why. This depends on the same
# ANIM_* cache variables the blob does, so changing the model re-bakes -- it was
# run by hand once, and a stale xmas_baked.c is a build that renders the
# previous model with no sign that anything is wrong.
#
add_custom_command(
    OUTPUT "${XMAS_BAKED_C}"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${BAKE_SCRIPT}"
        -ClientcDir "${CLIENTC_DIR}"
        -AnimCacheDir "${ANIM_CACHE_DIR}"
        -AnimRev "${ANIM_REV}"
        -AnimModel "${ANIM_MODEL}"
        -AnimSeq "${ANIM_SEQ}"
        -Ambient "${LIGHT_AMBIENT}"
        -Attenuation "${LIGHT_ATTENUATION}"
        -LightDir "${LIGHT_DIR}"
        -Gamma "${LIGHT_GAMMA}"
        -OutFile "${XMAS_BAKED_C}"
    DEPENDS "${BAKE_SCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_model/bake_model.c"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Baking the model and animation into const flash arrays"
    VERBATIM
)

add_custom_target(xmas_baked_gen ALL DEPENDS "${XMAS_BAKED_C}")
add_dependencies(__idf_main xmas_baked_gen)

#
# THE `model` PARTITION AND ITS BLOB ARE GONE.
#
# scripts/extract-model.ps1 staged a model into build/model.bin and flashed it
# to a `model` SPIFFS partition, in the XMA1 container from
# tools/extract_model/pack_anim.c. Nothing read it: main.c stopped decoding at
# boot when the model became const C, and the partition sat there being flashed
# on every build.
#
# What replaced it is main/xmb_format.h -- a bundle the DEVICE downloads into
# one of two `mdl_` slots and maps in place, rather than something the build
# stages for it. See server/bake_bundles.py to produce one and main/model_store.c
# to see how it arrives. The script and pack_anim.c are left in the tree; they
# are simply no longer part of a build.
#
