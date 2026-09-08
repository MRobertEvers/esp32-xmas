// Iron Man v6: the user's red-ring helmet badge, kept intact above the display.
// Artwork is separate from the mechanical interface. Earlier revisions are untouched.
// part: ironman_v6 | body/black/gold/cyan[_print] | check_core/colours/backing
include <xmas_orn_case.scad>
use <ironman_reference_art.scad>
part = "ironman_v6";
preview_display = true;
im_cx = (glass_x0 + glass_x1)/2;
im_cy = (glass_y0 + glass_y1)/2 - 4;
im_bottom = -32;
im_top = 82;
im_inlay = 0.8;
im_names = ["body", "black", "gold", "cyan"];
im_colours = ["#b42029", "#252627", "#f1cd51", "#43d4ec"];
badge_y = 52;
badge_r = 30;
art_scale = 60/384;

module im_window() translate([0,4]) offset(r = 1.5)
    square([glass_w+2*front_win_margin-3, glass_l+2*front_win_margin-3], center = true);
module im_outline() union() {
    translate([0,badge_y]) circle(r=badge_r,$fn=192);
    // Flat entry edge with small corner chamfers; 42 mm width clears the core pocket.
    polygon([[-18,-32],[18,-32],[21,-29],[21,31.5],[-21,31.5],[-21,-29]]);
}
module im_art_position() translate([0,badge_y-150*art_scale]) scale(art_scale) children();
module im_gold() im_art_position() helmet_art_gold();
module im_cyan() im_art_position() helmet_art_eyes();
module im_black() {
    // Black medallion field around a red helmet shell.
    difference() {
        translate([0,badge_y]) circle(r=25,$fn=192);
        im_art_position() helmet_art_red();
    }
    // Crisp ink-like boundaries between the flat colour plates (about 0.55 mm).
    im_art_position() {
        offset(delta=3.5) helmet_art_gold();
        offset(delta=3.5) helmet_art_eyes();
    }
    translate([0,badge_y]) difference() {
        circle(r=30,$fn=192); circle(r=29.4,$fn=192);
    }
    // Screen surround; sleeve_cuts makes the sole authoritative opening.
    offset(delta=1.4) im_window();
}

module im_raw(c) {
    if (c == 1) im_black();
    if (c == 2) im_gold();
    if (c == 3) im_cyan();
}
module im_ink(c) difference() {
    intersection() { im_outline(); im_raw(c); }
    if (c < 3) for (k = [c+1:3]) im_raw(k);
}
module im_body() {
    difference() {
        union() {
            translate([im_cx,im_cy,sz0]) linear_extrude(sz1-sz0) im_outline();
            hang_loop(im_cx,im_cy+im_top);
        }
        hang_loop_hole(im_cx,im_cy+im_top);
        sleeve_cuts(ext = 40, finger = true);
        translate([im_cx,im_cy,sz1-im_inlay]) linear_extrude(im_inlay+eps)
            for (c = [1:3]) im_raw(c);
    }
    sleeve_rails();
}
module im_colour(c) difference() {
    translate([im_cx,im_cy,sz1-im_inlay]) linear_extrude(im_inlay) im_ink(c);
    sleeve_cuts(ext = 40, finger = true);
}
module im_piece(c) { if (c == 0) im_body(); else im_colour(c); }
module im_solid() union() for (c = [0:3]) im_piece(c);
module im_stand() translate([0,0,-(im_cy+im_bottom)]) rotate([90,0,0]) children();

if (part == "ironman_v6") {
    for (c = [0:3]) color(im_colours[c]) render() im_piece(c);
    if (preview_display) color("#101a25") translate([im_cx,im_cy,sz1-im_inlay-0.1])
        linear_extrude(0.05) im_window();
}
for (c = [0:3]) {
    if (part == im_names[c]) im_piece(c);
    if (part == str(im_names[c],"_print")) im_stand() im_piece(c);
}
if (part == "check_core") intersection() { im_solid(); union() { core_tray(); core_bezel(); } }
if (part == "check_colours") for (a = [0:2], b = [a+1:3])
    intersection() { im_piece(a); im_piece(b); }
if (part == "check_backing") difference() {
    translate([0,0,-im_inlay]) union() for (c = [1:3]) im_colour(c);
    im_body();
}
