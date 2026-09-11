// Version 2: complete character features above an unobstructed display.
// Independent of xmas_orn_outers.scad; the original designs/exports are preserved.
// Shared pocket, rails, detents, loop and connector clearances come from the core.
// Export: ./export_outers_v2.sh (one aligned STL per filament, <= 4 per theme).
// Select "showcase_v2", a theme, "<theme>_<colour>[_print]", or "check_<theme>".
include <xmas_orn_case.scad>
part = "showcase_v2";
inlay_d = 0.8;              // flush inlays, leaving 1 mm of front skin beneath them
preview_display = true;    // dark screen proxy in assembled views ONLY, never STL parts

v2_cx = (glass_x0 + glass_x1) / 2;
v2_cy = (glass_y0 + glass_y1) / 2 - 4;
v2_themes = ["cap", "spidey_nwh", "ironman", "widow", "spidey_classic"];
v2_parts = [["body", "white", "blue"], ["body", "black", "gold", "white"],
            ["body", "gold", "black", "white"], ["body", "silver", "red"],
            ["body", "blue", "black", "white"]];
v2_palette = [["#bf2435", "#f5f2df", "#174980"],
              ["#c72b38", "#161a23", "#d9a54b", "#f7f4e8"],
              ["#a91f2c", "#e4b44f", "#24232b", "#e5f8ff"],
              ["#20232b", "#aab1bc", "#e52c3e"],
              ["#dc3442", "#195aa3", "#181b26", "#fff7e7"]];
v2_bottom = [-32, -35, -35, -35, -35];
v2_top = [54, 49, 48, 49, 49];
assert(inlay_d > 0 && inlay_d <= s_wall - 0.8, "Keep at least 0.8 mm behind the inlays");

// --------------------------------------------------------------- drawing primitives
module v2_ring(ri, ro) difference() {
    circle(r = ro, $fn = 160); circle(r = ri, $fn = 160);
}
module v2_star(ro, ri) polygon([for (i = [0:9])
    let(a = 90 + 36*i, r = i%2 == 0 ? ro : ri) [r*cos(a), r*sin(a)]]);
module v2_line(points, w = 1.1) for (i = [0:len(points)-2]) hull() {
    translate(points[i]) circle(d = w, $fn = 16);
    translate(points[i+1]) circle(d = w, $fn = 16);
}
module v2_bilateral() { children(); mirror([1, 0, 0]) children(); }
module v2_window(expand = 0) translate([0, 4]) offset(r = expand)
    offset(r = 1.5) square([glass_w + 2*front_win_margin - 3,
                           glass_l + 2*front_win_margin - 3], center = true);
// Leave the centre filled here: sleeve_cuts() makes the one authoritative opening.
// Subtracting a second, nominally identical hole leaves microscopic STL slivers.
module v2_window_rim(w = 1.4) v2_window(w);

// Scalloped web cells, with curves bowing toward the web hub rather than circles.
function v2_polar(r, a) = [r*cos(a), r*sin(a)];
function v2_bezier(a, b, c, t) = (1-t)*(1-t)*a + 2*(1-t)*t*b + t*t*c;
module v2_web(hub = [0, 29], radii = [15, 28, 42, 57, 73], w = 0.95) translate(hub) {
    for (a = [0:30:330]) v2_line([[0, 0], v2_polar(100, a)], w);
    for (r = radii, a = [0:30:330])
        v2_line([for (j = [0:8]) v2_bezier(v2_polar(r, a),
            v2_polar(r*0.83, a+15), v2_polar(r, a+30), j/8)], w);
}
module v2_spider(y = -24) translate([0, y]) {
    scale([1, 1.5]) circle(r = 2.1, $fn = 24);
    translate([0, 3]) circle(r = 1.5, $fn = 24);
    v2_bilateral() {
        v2_line([[1, 2], [5, 5], [7, 9]], 1.2);
        v2_line([[1, 1], [7, 3], [10, 7]], 1.2);
        v2_line([[1, 0], [7, -2], [10, -6]], 1.2);
        v2_line([[1, -1], [4, -4], [5, -9]], 1.2);
    }
}

// --------------------------------------------------------------- silhouettes
module v2_outline(t) {
    if (t == 0) intersection() {
        translate([0, 8]) circle(r = 46, $fn = 192);
        translate([-50, v2_bottom[t]]) square([100, 100]);
    }
    if (t == 1) offset(r = 1) polygon([
        [-20,-34], [20,-34], [30,-22], [33,2], [31,29], [23,42],
        [11,48], [-11,48], [-23,42], [-31,29], [-33,2], [-30,-22]]);
    if (t == 2) polygon([
        [-21,-35], [21,-35], [31,-24], [33,22], [28,39], [17,48],
        [-17,48], [-28,39], [-33,22], [-31,-24]]);
    if (t == 3) polygon([
        [-21,-35], [21,-35], [35,-22], [35,28], [22,43], [0,49],
        [-22,43], [-35,28], [-35,-22]]);
    if (t == 4) offset(r = 1) polygon([
        [-21,-34], [21,-34], [32,-24], [35,-3], [34,24], [27,39],
        [14,48], [-14,48], [-27,39], [-34,24], [-35,-3], [-32,-24]]);
}

// --------------------------------------------------------------- Captain America
// Shift the shield centre up so the entire star survives above the screen.
module v2_cap_white() {
    translate([0, 8]) v2_ring(35, 40);
    translate([0, 29]) v2_star(8.8, 3.8);
}
module v2_cap_blue() difference() {
    translate([0, 8]) circle(r = 30, $fn = 160);
    translate([0, 29]) v2_star(8.8, 3.8);
}

// --------------------------------------------------------------- No Way Home: integrated suit
module v2_nwh_eyes() v2_bilateral() offset(r = 0.45) polygon([
    [-25,36], [-6,29], [-9,24], [-16,24.5], [-23,29]]);
module v2_nwh_black() {
    difference() {
        v2_web();
        offset(r = 2.5) v2_nwh_eyes();
        v2_window(2.5);
        translate([0,-24]) square([25,23], center = true);
    }
    offset(r = 1.8) v2_nwh_eyes();
    v2_window_rim(1.4);
    // Angular dark suit panels with a gold seam against the red mask.
    v2_bilateral() polygon([[19,17], [40,30], [45,-40], [20,-31], [19,-13]]);
}
module v2_nwh_gold() {
    v2_spider();
    v2_bilateral() v2_line([[29,22], [18,15], [18,-12], [24,-19], [23,-29]], 1.25);
}

// --------------------------------------------------------------- Iron Man: helmet
module v2_iron_face() polygon([
    [-15,43], [-7,43], [-5,34], [5,34], [7,43], [15,43], [25,35],
    [26,13], [22,4], [22,-17], [16,-29], [-16,-29], [-22,-17],
    [-22,4], [-26,13], [-25,35]]);
module v2_iron_eyes() v2_bilateral() polygon([
    [-23,30], [-5,27], [-6,24], [-21,25]]);
module v2_iron_black() {
    offset(r = 1.1) v2_iron_eyes();
    v2_window_rim(1.2);
    v2_line([[-14,-25], [-10,-22], [10,-22], [14,-25]], 1.3);
    v2_line([[-9,-27], [9,-27]], 1.0);
    v2_bilateral() {
        v2_line([[29,23], [29,6], [26,0], [26,-19], [20,-28]], 1.2);
        v2_line([[18,40], [23,35]], 1.0);
    }
}

// --------------------------------------------------------------- Black Widow: tactical badge
module v2_widow_silver() {
    difference() { offset(delta = -2) v2_outline(3); offset(delta = -3.2) v2_outline(3); }
    translate([0,32]) v2_ring(12.4,13.5);
    v2_window_rim(1.0);
    v2_bilateral() {
        v2_line([[27,16], [23,12], [23,-12], [29,-18]], 1.2);
        for (y = [-3, 2, 7]) translate([29,y]) rotate(35) square([3,1.2], center = true);
    }
}
module v2_widow_red() {
    // A single, intact hourglass, with a 5 mm waist (no screen through its middle).
    translate([0,32]) polygon([[-9,9], [9,9], [2.5,0], [9,-9], [-9,-9], [-2.5,0]]);
    v2_bilateral() polygon([[4,-23], [26,-23], [20,-29], [4,-29]]);
    translate([0,-26]) square([5,6], center = true);
    v2_bilateral() v2_line([[31,27], [27,23]], 2.2);
}

// --------------------------------------------------------------- Classic Spider-Man: red / blue suit
module v2_classic_eyes() v2_bilateral() offset(r = 0.6) polygon([
    [-26,39], [-6,31], [-10,24.5], [-18,25], [-25,30]]);
module v2_classic_blue() v2_bilateral() polygon([
    [34,18], [22,11], [20,-10], [13,-21], [14,-35], [40,-35], [40,18]]);
module v2_classic_black() {
    difference() {
        v2_web([0,30], [16,30,45,61,78], 1.05);
        offset(r = 2.9) v2_classic_eyes();
        v2_window(2.4);
        v2_classic_blue();
        translate([0,-24]) square([24,23], center = true);
    }
    offset(r = 2.1) v2_classic_eyes();
    v2_spider();
    v2_window_rim(1.4);
    v2_bilateral() v2_line([[34,18], [22,11], [20,-10], [13,-21], [14,-35]], 1.4);
}

// --------------------------------------------------------------- colour partition / mechanical interface
module v2_raw_ink(t, c) {
    if (t == 0) { if (c == 1) v2_cap_white(); if (c == 2) v2_cap_blue(); }
    if (t == 1) { if (c == 1) v2_nwh_black(); if (c == 2) v2_nwh_gold(); if (c == 3) v2_nwh_eyes(); }
    if (t == 2) { if (c == 1) v2_iron_face(); if (c == 2) v2_iron_black(); if (c == 3) v2_iron_eyes(); }
    if (t == 3) { if (c == 1) v2_widow_silver(); if (c == 2) v2_widow_red(); }
    if (t == 4) { if (c == 1) v2_classic_blue(); if (c == 2) v2_classic_black(); if (c == 3) v2_classic_eyes(); }
}
// Later colours have priority: all STLs meet at their edges without overlapping volumes.
module v2_ink(t, c) difference() {
    intersection() { v2_outline(t); v2_raw_ink(t,c); }
    if (c < len(v2_parts[t])-1) for (k = [c+1:len(v2_parts[t])-1]) v2_raw_ink(t,k);
}
module v2_body(t) {
    difference() {
        union() {
            translate([v2_cx,v2_cy,sz0]) linear_extrude(sz1-sz0) v2_outline(t);
            hang_loop(v2_cx,v2_cy+v2_top[t]);
        }
        hang_loop_hole(v2_cx,v2_cy+v2_top[t]);
        sleeve_cuts(ext = 40, finger = true);
        translate([v2_cx,v2_cy,sz1-inlay_d]) linear_extrude(inlay_d+eps)
            for (c = [1:len(v2_parts[t])-1]) v2_raw_ink(t,c);
    }
    sleeve_rails();
}
module v2_colour(t,c) difference() {
    translate([v2_cx,v2_cy,sz1-inlay_d]) linear_extrude(inlay_d) v2_ink(t,c);
    sleeve_cuts(ext = 40, finger = true);
}
module v2_piece(t,c) { if (c == 0) v2_body(t); else v2_colour(t,c); }
module v2_stand(t) translate([0,0,-(v2_cy+v2_bottom[t])]) rotate([90,0,0]) children();
module v2_solid(t) union() for (c = [0:len(v2_parts[t])-1]) v2_piece(t,c);
module v2_show(t) {
    for (c = [0:len(v2_parts[t])-1]) color(v2_palette[t][c]) render() v2_piece(t,c);
    // A plain display proxy makes the hole legible; this is not printable geometry.
    if (preview_display) color("#101a25") translate([v2_cx,v2_cy,sz1-inlay_d-0.1])
        linear_extrude(0.05) v2_window();
}

// --------------------------------------------------------------- selection and fit checks
for (t = [0:len(v2_themes)-1]) {
    if (part == v2_themes[t]) v2_show(t);
    for (c = [0:len(v2_parts[t])-1]) {
        if (part == str(v2_themes[t],"_",v2_parts[t][c])) v2_piece(t,c);
        if (part == str(v2_themes[t],"_",v2_parts[t][c],"_print")) v2_stand(t) v2_piece(t,c);
    }
    // Core intersection must be empty. Colour checks may contain floating-point boundary
    // fragments, so validate_outers_v2.py also measures their volume (< 0.0001 mm^3).
    if (part == str("check_",v2_themes[t]) || part == str("check_core_",v2_themes[t]))
        intersection() { v2_solid(t); union() { core_tray(); core_bezel(); } }
    if (part == str("check_",v2_themes[t]) || part == str("check_colours_",v2_themes[t])) {
        for (a = [0:len(v2_parts[t])-2], b = [a+1:len(v2_parts[t])-1])
            intersection() { v2_piece(t,a); v2_piece(t,b); }
    }
    // Every inlay must have solid body material immediately behind its full footprint.
    if (part == str("check_backing_",v2_themes[t])) difference() {
        translate([0,0,-inlay_d]) union()
            for (c = [1:len(v2_parts[t])-1]) v2_colour(t,c);
        v2_body(t);
    }
}
if (part == "showcase_v2") for (t = [0:len(v2_themes)-1]) translate([t*105,0,0]) v2_show(t);
