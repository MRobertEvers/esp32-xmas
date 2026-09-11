// Fresh branch from V2. No dependency on the archived v3-v10 attempts.
// Retains V2's complete Iron Man sleeve geometry; replaces only the front colour layout.
// part: ironman_v2b | body/gold/black/white[_print] | check_core/colours/backing/baseline
include <xmas_orn_outers_v2.scad>
part = "ironman_v2b";
preview_display = true;
fresh_names = ["body", "gold", "black", "white"];
fresh_colours = ["#a91f2c", "#e4b44f", "#24232b", "#e5f8ff"];

module fresh_reactor() polygon([[-24,23],[24,23],[17,-16],[-17,-16]]);
module fresh_gold() v2_bilateral() {
    // Small angular shoulder plates and narrow armour at the sides of the display.
    polygon([[13,42],[17,43],[24,37],[27,29],[22,30],[18,37]]);
    polygon([[28,19],[30,17],[29,-16],[22,-28],[20,-27],[25,-15]]);
}
module fresh_black() offset(delta=1.2) fresh_reactor();
module fresh_white() difference() { fresh_reactor(); offset(delta=-1.5) fresh_reactor(); }
module fresh_raw(c) {
    if (c == 1) fresh_gold();
    if (c == 2) fresh_black();
    if (c == 3) fresh_white();
}
module fresh_ink(c) difference() {
    intersection() { v2_outline(2); fresh_raw(c); }
    if (c < 3) for (k = [c+1:3]) fresh_raw(k);
}
module fresh_body() difference() {
    // Reunite the V2 body and its inlays, then cut the new inlay pockets.
    // This keeps its outline, loop, rails, pocket, detents and openings exactly.
    v2_solid(2);
    translate([v2_cx,v2_cy,sz1-inlay_d]) linear_extrude(inlay_d+eps)
        for (c = [1:3]) fresh_raw(c);
}
module fresh_colour(c) difference() {
    translate([v2_cx,v2_cy,sz1-inlay_d]) linear_extrude(inlay_d) fresh_ink(c);
    sleeve_cuts(ext=40, finger=true);
}
module fresh_piece(c) { if (c == 0) fresh_body(); else fresh_colour(c); }
module fresh_solid() union() for (c = [0:3]) fresh_piece(c);

if (part == "ironman_v2b") {
    for (c = [0:3]) color(fresh_colours[c]) render() fresh_piece(c);
    if (preview_display) color("#101a25") translate([v2_cx,v2_cy,sz1-inlay_d-0.1])
        linear_extrude(0.05) v2_window();
}
for (c = [0:3]) {
    if (part == fresh_names[c]) fresh_piece(c);
    if (part == str(fresh_names[c],"_print")) v2_stand(2) fresh_piece(c);
}
if (part == "check_core") intersection() { fresh_solid(); union() { core_tray(); core_bezel(); } }
if (part == "check_colours") for (a = [0:2], b = [a+1:3])
    intersection() { fresh_piece(a); fresh_piece(b); }
if (part == "check_backing") difference() {
    translate([0,0,-inlay_d]) union() for (c = [1:3]) fresh_colour(c);
    fresh_body();
}
if (part == "check_baseline") {
    difference() { fresh_solid(); v2_solid(2); }
    difference() { v2_solid(2); fresh_solid(); }
}
