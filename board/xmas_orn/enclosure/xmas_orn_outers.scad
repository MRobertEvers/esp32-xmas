// xmas_orn_outers.scad
// Themed outers for the xmas_orn core: five Marvel sleeves matching the Funko Pop! figures.
// Each is the default sleeve's pocket, rails, window and cable holes (from xmas_orn_case.scad)
// inside a different outline, with 0.8 mm colour inlays on the front face.
//
// Multi-colour: every theme exports one STL per colour, all in the same coordinates.  In Bambu
// Studio, drag the theme's STLs in together and answer "Yes" to "load as a single object with
// multiple parts", then assign a filament to each part.  All themes need <= 4 colours (AMS lite).
//
//   theme            parts (colour)                             outline
//   cap              body (red), white, blue                    shield, 80 mm disc
//   spidey_nwh       body (red), black, white                   mask, 50 x 66 rounded
//   ironman          body (red), gold                           chest plate, 52 x 66 chamfered
//   widow            body (black), red                          emblem, 80 mm disc
//   spidey_classic   body (red), black, blue                    web, 80 mm disc
//
// part = "<theme>_<colour>" renders one part in design position, "<theme>_<colour>_print" stands it
// on its flat bottom for the bed, "<theme>" shows the assembled coloured sleeve, "showcase" all five.
// Render everything with ./export_outers.sh

include <xmas_orn_case.scad>
part = "showcase";
inlay_d = 0.8;

// design centre: 4 mm below the window centre so 80 mm discs stand on a wide enough flat
dcx = (glass_x0 + glass_x1) / 2;          // 13.75
dcy = (glass_y0 + glass_y1) / 2 - 4;      // 20.1
wc  = [0, 4];                             // window centre in the design frame
// design-frame extents to respect: pocket x +/-16.7, y -30.3 .. +27.75; window x +/-14.3, y -11.8 .. +19.8

// ---------------------------------------------------------------- 2D helpers (design frame)
module ring(r1, r2) difference() { circle(r = r2, $fn = 120); circle(r = r1, $fn = 120); }
module star(ro, ri) polygon([for (i = [0 : 9]) let(r = i % 2 == 0 ? ro : ri, a = 90 + i * 36) [r * cos(a), r * sin(a)]]);
module disc_flat(r, flat_y) intersection() { circle(r = r, $fn = 160); translate([-r - 1, flat_y]) square([2 * r + 2, 2 * r]); }
module rrect(w, h, r) offset(r = r) square([w - 2 * r, h - 2 * r], center = true);
module spokes(c, n, w, len) for (i = [0 : n - 1]) translate(c) rotate(i * 360 / n) translate([0, -w / 2]) square([len, w]);
module web(c, radii, w, n = 12) { spokes(c, n, w, 60); for (r = radii) translate(c) ring(r - w / 2, r + w / 2); }
module eye(x, y, a) translate([x, y]) rotate(a) scale([1, 0.5]) circle(r = 6.5, $fn = 60);

// ---------------------------------------------------------------- 3D: body and inlay from 2D
// children(0): outline.  children(1): union of every inlay (so the body gets the pockets).
module themed_body(top_y) {
    lx = dcx; ly = dcy + top_y;
    difference() {
        union() {
            translate([dcx, dcy, sz0]) linear_extrude(sz1 - sz0) children(0);
            hang_loop(lx, ly);
        }
        hang_loop_hole(lx, ly);
        sleeve_cuts(ext = 30, finger = true);
        translate([dcx, dcy, sz1 - inlay_d]) linear_extrude(inlay_d + 1) children(1);
    }
    sleeve_rails();
}
// children(0): outline.  children(1): this colour's 2D.
module themed_inlay() {
    difference() {
        translate([dcx, dcy, sz1 - inlay_d]) linear_extrude(inlay_d) intersection() { children(0); children(1); }
        sleeve_cuts(ext = 30, finger = true);
    }
}
module stand(bottom_y) translate([0, 0, -(dcy + bottom_y)]) rotate([90, 0, 0]) children();

// ================================================================ Captain America: shield
cap_bottom = -32; cap_top = 40;
module cap_outline() disc_flat(40, cap_bottom);
module cap_white() { ring(30.5, 35); translate([0, -19]) star(6.5, 2.6); }
module cap_blue()  difference() { circle(r = 26, $fn = 120); translate([0, -19]) star(6.5, 2.6); }
module cap_inlays() { cap_white(); cap_blue(); }
module cap_body()       themed_body(cap_top) { cap_outline(); cap_inlays(); }
module cap_white_part() themed_inlay() { cap_outline(); cap_white(); }
module cap_blue_part()  themed_inlay() { cap_outline(); cap_blue(); }

// ================================================================ Spider-Man, No Way Home: mask
nwh_bottom = -33; nwh_top = 33;
module nwh_outline() rrect(50, 66, 12);
module nwh_eyes() { eye(-10, 26, 25); eye(10, 26, -25); }
module nwh_black() difference() {
    union() { web(wc, [22, 30], 1.3); offset(r = 1.2) nwh_eyes(); }
    nwh_eyes();
}
module nwh_white() nwh_eyes();
module nwh_inlays() { nwh_black(); nwh_white(); }
module nwh_body()       themed_body(nwh_top) { nwh_outline(); nwh_inlays(); }
module nwh_black_part() themed_inlay() { nwh_outline(); nwh_black(); }
module nwh_white_part() themed_inlay() { nwh_outline(); nwh_white(); }

// ================================================================ Iron Man: chest plate, arc reactor window
iron_bottom = -36; iron_top = 36;      // offset(r = 3) grows the polygon by 3
module iron_outline() offset(r = 3) polygon([[-20, -33], [20, -33], [26, -27], [26, 20], [16, 33], [-16, 33], [-26, 20], [-26, -27]]);
module iron_gold() {
    translate(wc) ring(22, 25);                                              // reactor housing
    for (sx = [-1, 1]) translate([sx * 25, -2]) square([3.2, 40], center = true);   // side bars
    polygon([[-17, -31], [17, -31], [13, -25], [-13, -25]]);                 // abdomen plate
    for (sx = [-1, 1]) polygon([[sx * 9, 33], [sx * 20, 33], [sx * 26, 26], [sx * 26, 22], [sx * 14, 30]]);  // shoulders
}
module iron_body()      themed_body(iron_top) { iron_outline(); iron_gold(); }
module iron_gold_part() themed_inlay() { iron_outline(); iron_gold(); }

// ================================================================ Black Widow: emblem
widow_bottom = -32; widow_top = 40;
module widow_outline() disc_flat(40, widow_bottom);
module widow_red() {
    ring(36, 38.5);
    polygon([[-22, 30], [22, 30], [6, 21.5], [-6, 21.5]]);
    polygon([[-22, -28], [22, -28], [6, -13.5], [-6, -13.5]]);
}
module widow_body()     themed_body(widow_top) { widow_outline(); widow_red(); }
module widow_red_part() themed_inlay() { widow_outline(); widow_red(); }

// ================================================================ classic Spider-Man (the Hallmark one): web disc
cls_bottom = -32; cls_top = 40;
module cls_outline() disc_flat(40, cls_bottom);
module cls_black() web(wc, [24, 31, 37], 1.4);
module cls_blue()  difference() { translate([-45, -60]) square([90, 40]); cls_black(); }   // suit blue below y = -20
module cls_inlays() { cls_black(); cls_blue(); }
module cls_body()       themed_body(cls_top) { cls_outline(); cls_inlays(); }
module cls_black_part() themed_inlay() { cls_outline(); cls_black(); }
module cls_blue_part()  themed_inlay() { cls_outline(); cls_blue(); }

// ================================================================ assembled, coloured views
module cap_show()   { color("firebrick") cap_body();   color("white") cap_white_part();   color("royalblue") cap_blue_part(); }
module nwh_show()   { color("firebrick") nwh_body();   color("black") nwh_black_part();   color("white") nwh_white_part(); }
module iron_show()  { color("firebrick") iron_body();  color("gold")  iron_gold_part(); }
module widow_show() { color("#202020")   widow_body(); color("red")   widow_red_part(); }
module cls_show()   { color("firebrick") cls_body();   color("black") cls_black_part();   color("royalblue") cls_blue_part(); }

// ================================================================ part selection
if (part == "cap")            cap_show();
if (part == "spidey_nwh")     nwh_show();
if (part == "ironman")        iron_show();
if (part == "widow")          widow_show();
if (part == "spidey_classic") cls_show();
if (part == "showcase") {
    translate([0, 0, 0])   cap_show();
    translate([90, 0, 0])  nwh_show();
    translate([160, 0, 0]) iron_show();
    translate([240, 0, 0]) widow_show();
    translate([330, 0, 0]) cls_show();
}

if (part == "cap_body")  cap_body();        if (part == "cap_body_print")  stand(cap_bottom) cap_body();
if (part == "cap_white") cap_white_part();  if (part == "cap_white_print") stand(cap_bottom) cap_white_part();
if (part == "cap_blue")  cap_blue_part();   if (part == "cap_blue_print")  stand(cap_bottom) cap_blue_part();

if (part == "spidey_nwh_body")  nwh_body();        if (part == "spidey_nwh_body_print")  stand(nwh_bottom) nwh_body();
if (part == "spidey_nwh_black") nwh_black_part();  if (part == "spidey_nwh_black_print") stand(nwh_bottom) nwh_black_part();
if (part == "spidey_nwh_white") nwh_white_part();  if (part == "spidey_nwh_white_print") stand(nwh_bottom) nwh_white_part();

if (part == "ironman_body") iron_body();       if (part == "ironman_body_print") stand(iron_bottom) iron_body();
if (part == "ironman_gold") iron_gold_part();  if (part == "ironman_gold_print") stand(iron_bottom) iron_gold_part();

if (part == "widow_body") widow_body();      if (part == "widow_body_print") stand(widow_bottom) widow_body();
if (part == "widow_red")  widow_red_part();  if (part == "widow_red_print")  stand(widow_bottom) widow_red_part();

if (part == "spidey_classic_body")  cls_body();        if (part == "spidey_classic_body_print")  stand(cls_bottom) cls_body();
if (part == "spidey_classic_black") cls_black_part();  if (part == "spidey_classic_black_print") stand(cls_bottom) cls_black_part();
if (part == "spidey_classic_blue")  cls_blue_part();   if (part == "spidey_classic_blue_print")  stand(cls_bottom) cls_blue_part();

// fit checks: should all be empty
if (part == "check_cap")   intersection() { cap_body();   union() { core_tray(); core_bezel(); } }
if (part == "check_nwh")   intersection() { nwh_body();   union() { core_tray(); core_bezel(); } }
if (part == "check_iron")  intersection() { iron_body();  union() { core_tray(); core_bezel(); } }
if (part == "check_widow") intersection() { widow_body(); union() { core_tray(); core_bezel(); } }
if (part == "check_cls")   intersection() { cls_body();   union() { core_tray(); core_bezel(); } }
