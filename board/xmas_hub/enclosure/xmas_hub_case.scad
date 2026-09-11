// xmas_hub_case.scad
// Plain two-part box for the xmas_hub power board (80 x 66 mm, DC-005 jack on the left edge,
// four KF301 / MKDS 5.08 screw terminals on the right edge, M3 holes in the corners).
//
// Base + lid.  Four M3 x 16 screws go in from underneath: through the floor, the standoffs and
// the board's own mounting holes, and bite into bosses hanging from the lid.  So the same four
// screws clamp the board and hold the lid on; there is nothing on top.
//
// Units mm.  Frame: board bottom-left corner = (0,0), +x right, +y toward the top edge of the
// KiCad view, z = 0 at the top of the base floor.  From KiCad:  x = kx - 50,  y = 116 - ky.
// Printer: Bambu Lab A1 mini.  Render: ./export.sh   (or openscad -D 'part="base"' ...)

part = "assembly";   // base | lid | assembly | base_print | lid_print | check_base_boards | check_lid_boards | check_lid_base
$fn = 48;
eps = 0.01;

// ---------------------------------------------------------------- board
pcb_w = 80;  pcb_l = 66;  pcb_t = 1.6;  pcb_r = 2;
pcb_holes = [[3.5, 3.5], [74.5, 3.5], [3.5, 62.5], [74.5, 62.5]];   // M3, 3.2 mm
jack_y    = 52.0;   jack_w = 9.0;  jack_h = 11.0;  jack_axis = 6.5;  // DC-005, flush with x = 0
term_ys   = [53.0, 40.0, 27.0, 14.0];                              // J2..J5 body centres (F.Fab), right edge
term_w    = 10.16;  term_h = 10.0;  term_x0 = 68.35;  term_x1 = 78.25;
term_wire_z = [1.0, 8.5];                                          // wire entry band above the PCB
led_xy    = [[26.5, 18.0], [63, 56.8], [63, 43.8], [63, 30.8], [63, 17.8]];   // D2 (12 V), D11..D41 (arms)
cap_xy    = [29.5, 26.0];  cap_d = 6.3;  cap_h = 11.0;              // C1, the tallest part
ind_ys    = [53, 40, 27, 14];                                      // L1..L4, 8.3 sq x 4 tall at x 47.85..56.15

// ---------------------------------------------------------------- box
floor_t   = 3.2;
post_h    = 4.0;              // standoff: clears the through-hole pins underneath
post_d    = 7.0;
wall      = 2.4;
clr       = 0.5;              // board edge to wall
head_d    = 6.5;  head_h = 2.2;   // M3 pan / socket head counterbore in the floor
screw_d   = 3.4;
inner_h   = 13.0;             // PCB top to lid underside: C1 (11) + 2
lid_t     = 2.0;
lip_t     = 1.2;  lip_h = 2.5;  lip_clr = 0.25;
boss_d    = 7.0;  boss_hole = 2.5;   // M3 self-tapping.  4.0 for M3 heat-set inserts.
vent      = true;

z_pcb     = post_h;
z_pcb_top = post_h + pcb_t;          // 5.6
z_wall_top= z_pcb_top + inner_h;     // 18.6
z_top     = z_wall_top + lid_t;      // 20.6 (+ 3.2 floor = 23.8 overall)

cx0 = -clr;        cx1 = pcb_w + clr;
cy0 = -clr;        cy1 = pcb_l + clr;
bx0 = cx0 - wall;  bx1 = cx1 + wall;   // outside: 85.8 x 71.8
by0 = cy0 - wall;  by1 = cy1 + wall;
box_r = 3;

jack_win = [jack_y - 6.75, jack_y + 6.75, z_pcb_top - 0.5];   // y0, y1, z0; open to the wall top
term_win_w = 9.8;                                             // leaves 3.2 mm bars between arms

// ================================================================ helpers
module box(x0, x1, y0, y1, z0, z1) translate([x0, y0, z0]) cube([x1 - x0, y1 - y0, z1 - z0]);
module rbox(x0, x1, y0, y1, z0, z1, r)
    hull() for (x = [x0 + r, x1 - r], y = [y0 + r, y1 - r]) translate([x, y, z0]) cylinder(r = r, h = z1 - z0);
module xslot(x0, x1, y0, y1, z0, z1, r)      // rounded window through the x direction
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0)
        offset(r) offset(-r) polygon([[y0, z0], [y1, z0], [y1, z1], [y0, z1]]);
module yslot(y0, y1, x0, x1, z0, z1, r)      // rounded window through the y direction
    translate([0, y1, 0]) rotate([90, 0, 0]) linear_extrude(y1 - y0)
        offset(r) offset(-r) polygon([[x0, z0], [x1, z0], [x1, z1], [x0, z1]]);

// ================================================================ base
module base() {
    difference() {
        union() {
            difference() {
                rbox(bx0, bx1, by0, by1, -floor_t, z_wall_top, box_r);
                box(cx0, cx1, cy0, cy1, 0, z_wall_top + 1);
            }
            for (h = pcb_holes) translate([h[0], h[1], 0]) cylinder(d = post_d, h = post_h);
        }
        for (h = pcb_holes) translate([h[0], h[1], 0]) {
            translate([0, 0, -floor_t - 1]) cylinder(d = screw_d, h = post_h + floor_t + 2);
            translate([0, 0, -floor_t - 1]) cylinder(d = head_d, h = head_h + 1);
        }
        // DC jack, left wall: notch open to the top so a fat plug overmould fits
        xslot(bx0 - 1, cx0 + 1, jack_win[0], jack_win[1], jack_win[2], z_wall_top + 5, 2);
        // terminal wire entries, right wall
        for (y = term_ys)
            xslot(cx1 - 1, bx1 + 1, y - term_win_w/2, y + term_win_w/2,
                  z_pcb_top + term_wire_z[0], z_pcb_top + term_wire_z[1], 1.5);
        // vents in the long walls
        if (vent) for (x = [14, 40, 66], yy = [[by0 - 1, cy0 + 1], [cy1 - 1, by1 + 1]])
            yslot(yy[0], yy[1], x - 6, x + 6, z_pcb_top + 6.5, z_pcb_top + 8.5, 0.9);
    }
}

// ================================================================ lid
module lid() {
    difference() {
        union() {
            rbox(bx0, bx1, by0, by1, z_wall_top, z_top, box_r);
            // alignment lip inside the walls, interrupted at the jack notch
            difference() {
                box(cx0 + lip_clr, cx1 - lip_clr, cy0 + lip_clr, cy1 - lip_clr, z_wall_top - lip_h, z_wall_top + eps);
                box(cx0 + lip_clr + lip_t, cx1 - lip_clr - lip_t, cy0 + lip_clr + lip_t, cy1 - lip_clr - lip_t, z_wall_top - lip_h - 1, z_wall_top + 1);
                box(cx0 - 1, cx0 + lip_clr + lip_t + 1, jack_win[0] - 1, jack_win[1] + 1, z_wall_top - lip_h - 1, z_wall_top + 1);
            }
            // screw bosses down to the board
            for (h = pcb_holes) translate([h[0], h[1], z_pcb_top]) cylinder(d = boss_d, h = z_wall_top - z_pcb_top + eps);
        }
        for (h = pcb_holes) translate([h[0], h[1], z_pcb_top - 1]) cylinder(d = boss_hole, h = 12);
        // LED sight holes
        for (p = led_xy) translate([p[0], p[1], z_wall_top - 1]) cylinder(d = 2.5, h = lid_t + 2);
        // vent slots over the regulators
        if (vent) for (y = [12 : 8 : 52]) rbox(30, 58, y - 0.9, y + 0.9, z_wall_top - 1, z_top + 1, 0.85);
    }
}

// ================================================================ mock parts for the checks
module mock_boards() {
    color("darkgreen", 0.6) difference() {
        rbox(0, pcb_w, 0, pcb_l, z_pcb, z_pcb_top, pcb_r);
        for (h = pcb_holes) translate([h[0], h[1], z_pcb - 1]) cylinder(d = 3.2, h = 4);
    }
    color("black", 0.6) box(0.25, 14.45, jack_y - jack_w/2, jack_y + jack_w/2, z_pcb_top, z_pcb_top + jack_h);
    color("green", 0.6) for (y = term_ys) box(term_x0, term_x1, y - term_w/2, y + term_w/2, z_pcb_top, z_pcb_top + term_h);
    color("navy", 0.6) translate([cap_xy[0], cap_xy[1], z_pcb_top]) cylinder(d = cap_d, h = cap_h);
    color("gray", 0.6) for (y = ind_ys) box(47.85, 56.15, y - 4.45, y + 4.45, z_pcb_top, z_pcb_top + 4);
    color("gray", 0.6) box(20.7, 27.3, 35.14, 45.16, z_pcb_top, z_pcb_top + 2.4);   // Q1
}

// ================================================================ part selection
if (part == "base")       base();
if (part == "lid")        lid();
if (part == "base_print") translate([0, 0, floor_t]) base();
if (part == "lid_print")  rotate([180, 0, 0]) translate([0, 0, -z_top]) lid();     // top face down, bosses up
if (part == "assembly")   { base(); mock_boards(); color("orange", 0.45) lid(); }
if (part == "open")       { base(); mock_boards(); }
if (part == "check_base_boards") intersection() { base(); mock_boards(); }
if (part == "check_lid_boards")  intersection() { lid();  mock_boards(); }
if (part == "check_lid_base")    intersection() { lid();  base(); }
