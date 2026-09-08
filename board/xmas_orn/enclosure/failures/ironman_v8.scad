// Iron Man v8: reference-based torso, narrow plated neck and rounded gold shoulder caps.
// A complete small reactor sits high on the chest; the display sits below it.
// Previous versions and the common core interface are preserved.
// part: ironman_v8 | body/gold/black/white[_print] | check_core/colours/backing
include <xmas_orn_case.scad>
part = "ironman_v8";
preview_display = true;
im_cx = (glass_x0 + glass_x1)/2;
im_cy = (glass_y0 + glass_y1)/2 - 4;
im_bottom = -35;
im_top = 53;
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
// The neck is a narrow continuation of the red armour, not a framed throat opening.
im_profile = [[7,53],[9,48],[10,44],[20,43],[28,40],[35,36],[40,30],
              [42,23],[41,15],[36,10],[32,-5],[28,-18],[23,-35]];
module im_outline() polygon(concat(im_profile,
    [for(i=[len(im_profile)-1:-1:0]) [-im_profile[i][0],im_profile[i][1]]]));
module im_reactor() offset(r=0.6) offset(delta=-0.6) polygon([
    [-9,36],[-4,36],[-2,34.5],[2,34.5],[4,36],[9,36],
    [8,31],[4,24],[-4,24],[-8,31]]);
module im_gold() im_pair() {
    // Rounded shoulder shells echo the reference's gold deltoid armour.
    offset(r=1.1) offset(delta=-1.1) polygon([
        [25,39],[31,36],[36,31],[39,24],[38,18],[32,16],
        [28,19],[29,26],[26,32]]);
    // Small side plates taper toward the waist rather than forming a full gold border.
    polygon([[32,10],[34,12],[31,-6],[27,-16],[22,-24],[22,-17],[26,-7],[28,4]]);
}
module im_black() {
    offset(delta=0.9) im_reactor();
    offset(delta=0.9) im_window();
    // Two shallow neck bands, with red armour visible between and around them.
    im_line([[-5.5,49],[5.5,49]],0.85);
    im_line([[-6.5,46],[6.5,46]],0.85);
    im_line([[-11,43],[-7,41],[0,39],[7,41],[11,43]],0.9);
    im_pair() {
        // Pectoral seam sweeps inward below the collar and outside the screen.
        im_line([[14,41],[20,38],[24,33],[26,25],[24,20]],1.0);
        im_line([[39,14],[34,13],[30,16]],0.9);
        im_line([[28,3],[25,-6],[21,-12]],0.9);
    }
    // Restrained abdominal articulation below the display opening.
    im_line([[-17,-18],[-10,-21],[10,-21],[17,-18]],0.9);
    im_line([[-15,-26],[-9,-28],[9,-28],[15,-26]],0.9);
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

if (part == "ironman_v8") {
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
