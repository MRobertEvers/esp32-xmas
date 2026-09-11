// Iron Man revision 4: simplified 2008 Mark III helmet.
// Narrow horizontal lenses, broad faceplate, plain red shell; no bust or facial linework.
// V1-V3 remain untouched. Mechanical interface comes from the common core.
// part: ironman_v4 | body/gold/black/white[_print] | check_core/colours/backing
include <xmas_orn_case.scad>
part = "ironman_v4";
preview_display = true;
im_cx = (glass_x0 + glass_x1)/2;
im_cy = (glass_y0 + glass_y1)/2 - 4;
im_bottom = -35;
im_top = 48;
im_inlay = 0.8;
im_names = ["body", "gold", "black", "white"];
im_colours = ["#a51c2c", "#d9ae57", "#171b23", "#e9faff"];

module im_pair() { children(); mirror([1,0,0]) children(); }
module im_window() translate([0,4]) offset(r = 1.5)
    square([glass_w+2*front_win_margin-3, glass_l+2*front_win_margin-3], center = true);
module im_outline() polygon(concat(
    [for (a = [0:5:180]) [30*cos(a),24+24*sin(a)]],
    [[-29,-23],[-21,-35],[21,-35],[29,-23]]));

// The forehead notch and cut-back cheek plates define the helmet without drawn features.
module im_faceplate() polygon([
    [-6,44],[-5,38],[5,38],[6,44],[13,42],[21,37],[25,30],
    [26,12],[25,3],[21,-6],[18,-20],[16,-26],[12,-21],
    [-12,-21],[-16,-26],[-18,-20],[-21,-6],[-25,3],[-26,12],
    [-25,30],[-21,37],[-13,42]]);
module im_eyes() im_pair() polygon([
    [-24,27.4],[-6,27.4],[-7,25.2],[-23.4,24.8]]);
module im_gold() {
    im_faceplate();
    // Separate lower jaw plate, with the small red centre notch of the early helmet.
    polygon([[-18,-27],[-16,-27.2],[-11.5,-22.2],[11.5,-22.2],[16,-27.2],[18,-27],
             [20,-30],[16,-33],[11,-28],[4,-27],[3,-24.8],[-3,-24.8],
             [-4,-27],[-11,-28],[-16,-33],[-20,-30]]);
}
module im_black() {
    offset(delta = 0.65) im_eyes();
    // One plate separation, not a drawn mouth or multiple facial creases.
    polygon([[-16,-26],[-12,-21],[12,-21],[16,-26],
             [16,-27.2],[11.5,-22.2],[-11.5,-22.2],[-16,-27.2]]);
}
module im_white() im_eyes();

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

if (part == "ironman_v4") {
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
