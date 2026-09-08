// Iron Man v9: angular shoulders, no raised neck, arm-shaped sides beside the display.
// Based on V8; keeps its complete small reactor and clears the lower central panel.
// Previous versions and the common core interface are preserved.
// part: ironman_v9 | body/gold/black/white[_print] | check_core/colours/backing
include <xmas_orn_case.scad>
part = "ironman_v9";
preview_display = true;
im_cx = (glass_x0 + glass_x1)/2;
im_cy = (glass_y0 + glass_y1)/2 - 4;
im_bottom = -35;
im_top = 42;
im_inlay = 0.8;
im_names = ["body", "gold", "black", "white"];
im_colours = ["#a51e31", "#d7b16a", "#292931", "#e9faff"];

module im_pair() { children(); mirror([1,0,0]) children(); }
module im_line(points,w=1.0) for (i=[0:len(points)-2]) hull() {
    translate(points[i]) circle(d=w,$fn=16);
    translate(points[i+1]) circle(d=w,$fn=16);
}
module im_window() translate([0,4]) offset(r = 1.5)
    square([glass_w+2*front_win_margin-3, glass_l+2*front_win_margin-3], center = true);
// A flat top replaces the neck. Angular shoulders lead into upper arms and red gauntlets.
// The lower sides grow outward at <=45 degrees from the print bed, so the forearms
// do not begin as unsupported islands above the flat entry edge.
im_profile = [[12,42],[32,42],[41,33],[41,22],[36,15],[35,5],
              [38,-5],[38,-19],[34,-24],[23,-35]];
module im_outline() polygon(concat(im_profile,
    [for(i=[len(im_profile)-1:-1:0]) [-im_profile[i][0],im_profile[i][1]]]));
module im_reactor() offset(r=0.6) offset(delta=-0.6) polygon([
    [-9,36],[-4,36],[-2,34.5],[2,34.5],[4,36],[9,36],
    [8,31],[4,24],[-4,24],[-8,31]]);
module im_gold() im_pair() {
    // Faceted shoulder caps, without the rounded silhouette of V8.
    polygon([[24,37],[32,37],[37,32],[37,25],[32,22],[25,24]]);
    // Bicep armour follows the sides of the display instead of tapering into the waist.
    polygon([[26,20],[33,18],[34,8],[32,2],[26,3],[24,10]]);
}
module im_black() {
    offset(delta=0.9) im_reactor();
    offset(delta=0.9) im_window();
    im_pair() {
        im_line([[14,39],[21,34],[23,25]],1.0);
        im_line([[25,21],[31,19],[37,21]],0.9);
        // Recess-colour seam separates each arm from the central torso.
        polygon([[22,20],[25,15],[24,6],[23,-4],[24,-15],[23,-24],
                 [20,-18],[20,4]]);
        // Elbow joint and a single gauntlet seam; central lower panel stays plain.
        polygon([[26,0],[34,-1],[35,-4],[26,-3]]);
        im_line([[25,-8],[27,-22],[31,-26]],1.0);
    }
}
module im_white() im_reactor();

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

if (part == "ironman_v9") {
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
