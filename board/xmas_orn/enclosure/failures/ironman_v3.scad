// Iron Man revision 3: a complete helmet above an armoured display chest.
// Preserves V1 and V2. Uses the existing core without changing its interface.
// part: ironman_v3 | body/gold/black/white[_print] | check_core/colours/backing
include <xmas_orn_case.scad>
part = "ironman_v3";
preview_display = true;
im_cx = (glass_x0 + glass_x1)/2;
im_cy = (glass_y0 + glass_y1)/2 - 4;
im_bottom = -35;
im_top = 76;
im_inlay = 0.8;
im_names = ["body", "gold", "black", "white"];
im_colours = ["#ab2031", "#e3b454", "#222632", "#e9faff"];

module im_pair() { children(); mirror([1,0,0]) children(); }
module im_line(p, w = 1.1) for (i = [0:len(p)-2]) hull() {
    translate(p[i]) circle(d = w, $fn = 16);
    translate(p[i+1]) circle(d = w, $fn = 16);
}
module im_window() translate([0,4]) offset(r = 1.5)
    square([glass_w+2*front_win_margin-3, glass_l+2*front_win_margin-3], center = true);
module im_outline() polygon([
    [-12,76], [12,76], [22,70], [27,59], [27,45], [23,35], [20,31],
    [20,27], [29,30], [39,19], [37,-18], [25,-35], [-25,-35],
    [-37,-18], [-39,19], [-29,30], [-20,27], [-20,31], [-23,35],
    [-27,45], [-27,59], [-22,70]]);

module im_faceplate() polygon([
    [-12,71], [-7,71], [-5,64], [5,64], [7,71], [12,71], [20,65],
    [22,56], [20,51], [18,48], [17,41], [10,34], [-10,34],
    [-17,41], [-18,48], [-20,51], [-22,56], [-20,65]]);
module im_eyes() im_pair() polygon([[-19,56], [-5,53.5], [-6,51], [-17,51.8]]);
module im_gold() {
    im_faceplate();
    // Pectoral plates and articulated flanks, kept separate from the helmet.
    im_pair() {
        polygon([[3,26], [17,23], [26,25], [32,20], [25,16], [20,19], [3,23]]);
        polygon([[25,10], [32,14], [30,-12], [22,-22], [21,-16], [25,-7]]);
        polygon([[12,-26], [24,-22], [28,-23], [23,-30], [12,-31]]);
    }
}
module im_chest_frame() polygon([
    [-12,23], [12,23], [18,17], [18,-9], [12,-15], [-12,-15], [-18,-9], [-18,17]]);
module im_black() {
    // Bold helmet outline, temple hardware and the characteristic angular mouth.
    difference() { offset(delta = 1) im_faceplate(); im_faceplate(); }
    offset(delta = 0.9) im_eyes();
    im_line([[-11,40], [-8,42], [8,42], [11,40]], 1.2);
    im_line([[-7,37.5], [7,37.5]], 1);
    im_pair() {
        im_line([[15,49], [13,46], [13,43]], 0.9);
        im_line([[24,59], [24,47], [21,40]], 1.25);
        im_line([[9,49], [4,48], [3,45]], 0.9);
        im_line([[21,31], [12,28], [0,28]], 1.2);
        im_line([[29,27], [35,20], [34,12]], 1.3);
        im_line([[34,5], [33,-14], [26,-23]], 1.3);
        im_line([[8,-19], [15,-22], [14,-29]], 1.2);
    }
    offset(delta = 1.2) im_chest_frame();
    translate([0,-25]) circle(r = 7.2, $fn = 64);
}
module im_white() {
    im_eyes();
    // Four short luminous corner brackets suggest armour around the screen.
    im_pair() {
        im_line([[10,21.7], [12,21.7], [16.6,17.1], [16.6,14]], 1.1);
        im_line([[16.6,-6], [16.6,-9], [12,-13.6], [10,-13.6]], 1.1);
    }
    // An intact arc reactor below the display: red/gold armour remains readable.
    translate([0,-25]) difference() {
        circle(r = 5.5, $fn = 64);
        circle(r = 4.2, $fn = 64);
        for (a = [90:120:330]) rotate(a) translate([3,-0.6]) square([4,1.2]);
    }
    translate([0,-25]) polygon([[-3.2,2.2], [3.2,2.2], [0,-3.2]]);
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

if (part == "ironman_v3") {
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
