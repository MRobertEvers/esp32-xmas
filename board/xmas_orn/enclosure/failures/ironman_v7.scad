// Iron Man v7: V5 chest with a trapezoid arc-reactor frame and an extended collar.
// Earlier versions are preserved. The full rectangular display remains unobstructed.
// part: ironman_v7 | body/gold/black/white[_print] | check_core/colours/backing
include <xmas_orn_case.scad>
part = "ironman_v7";
preview_display = true;
im_cx = (glass_x0 + glass_x1)/2;
im_cy = (glass_y0 + glass_y1)/2 - 4;
im_bottom = -35;
im_top = 53;
im_inlay = 0.8;
im_names = ["body", "gold", "black", "white"];
im_colours = ["#ae2232", "#d9ae57", "#252a34", "#e9faff"];

module im_pair() { children(); mirror([1,0,0]) children(); }
module im_line(points,w=1.2) for (i=[0:len(points)-2]) hull() {
    translate(points[i]) circle(d=w,$fn=16);
    translate(points[i+1]) circle(d=w,$fn=16);
}
module im_window() translate([0,4]) offset(r = 1.5)
    square([glass_w+2*front_win_margin-3, glass_l+2*front_win_margin-3], center = true);
module im_outline() polygon([
    [-12,53],[12,53],[15,49],[15,39],[29,40],[41,29],[39,13],[34,5],
    [32,-15],[24,-35],[-24,-35],[-32,-15],[-34,5],[-39,13],
    [-41,29],[-29,40],[-15,39],[-15,49]]);
// Wide across the top, tapered at the bottom; the inner rim clears all screen corners.
module im_reactor() polygon([[-30,26],[30,26],[18.5,-20],[-18.5,-20]]);
module im_gold() {
    im_pair() polygon([[31,9],[34,13],[32,-13],[25,-28],[25,-20],[29,-10]]);
    // A restrained gold collar wraps the dark throat insert.
    polygon([[-13,49],[13,49],[13,40],[9,35],[-9,35],[-13,40]]);
}
module im_black() {
    offset(delta=1.6) im_reactor();
    polygon([[-10,49],[10,49],[10,41],[7,38],[-7,38],[-10,41]]);
    im_pair() {
        im_line([[14,31],[25,34],[35,29]],1.1);
        im_line([[36,22],[35,16]],1.1);
    }
    im_line([[-19,-28],[-13,-25],[13,-25],[19,-28]],1.1);
}
module im_white() difference() {
    im_reactor();
    offset(delta=-2.2) im_reactor();
}

module im_raw(c) {
    if (c == 1) im_gold();
    if (c == 2) im_black();
    if (c == 3) im_white();
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

if (part == "ironman_v7") {
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
