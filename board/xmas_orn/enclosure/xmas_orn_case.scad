// xmas_orn_case.scad
// Enclosure for the xmas_orn rev 2 ornament board (27.5 x 41.5 mm, ESP32-S3-WROOM-1, USB-C + DC-002 jack)
// carrying a 1.3" 240x240 ST7789 module on the 7-pin socket J1.
//
// Two layers, "Happy Meal toy" style:
//   * CORE  = tray + bezel.  Holds the electronics.  Same part for every outer.
//   * OUTER = anything with a pocket that matches the core envelope + side grooves.
//            outer_sleeve() is the default: a rounded hanging tag the core slides into.
//
// Units mm.  Frame: ornament PCB bottom-left corner (antenna end, left edge) = (0,0),
// +x to the right, +y toward the header/socket edge, z = 0 at the top of the tray floor.
// From KiCad:  x = kx - 116,  y = 108.5 - ky.
//
// Printer: Bambu Lab A1 mini (180 x 180 x 180 mm).  Everything here is < 70 mm.
// Render:  openscad -D 'part="tray"' -o stl/core_tray.stl xmas_orn_case.scad   (see export.sh)

part = "assembly";   // tray | bezel | sleeve | assembly | tray_print | bezel_print | sleeve_print
$fn = 48;
eps = 0.01;

// ---------------------------------------------------------------- ornament PCB (rev 2)
pcb_w      = 27.5;
pcb_l      = 41.5;
pcb_t      = 1.6;
pcb_holes  = [[2.5, 5], [25, 5], [2.5, 39], [25, 39]];   // 2.4 mm, M2
ant_over   = 5.8;          // WROOM-1 antenna hangs this far past y = 0
usb_y      = 26.7;         // J3 centre on the left edge (x = 0), body 9 wide x 3.3 tall
jack_y     = 32.65;        // J2 centre on the right edge (x = 27.5), body 5.1 wide x 7.3 tall
jack_axis  = 4.6;          // barrel axis above the PCB top (from the DC-002 model)
led_y      = [10.5, 16];   // D1 / D2 span on the left edge
socket_h   = 8.5;          // J1 PinSocket 1x07 vertical

// ---------------------------------------------------------------- 1.3" 240x240 display module
disp_w     = 27.5;
disp_l     = 39.0;
disp_t     = 1.2;
disp_top   = pcb_l + 3.4;  // its top edge sits 3.4 above the ornament's: pin row 4.89 from the
                           // display top, J1 1.49 from the ornament top.  = 44.9
disp_bot   = disp_top - disp_l;                 // 5.9
disp_hole_d   = 2.5;
disp_hole_x   = [2.5, 25];
disp_hole_yt  = disp_top - 2.5;                 // 42.4  (outside the ornament PCB)
disp_hole_yb  = disp_top - 36.5;                // 8.4
glass_w    = 26.16;  glass_l = 29.22;  glass_t = 1.7;
glass_top_off = 6.2;                            // glass top edge below the display top edge
glass_x0   = (disp_w - glass_w) / 2;  glass_x1 = glass_x0 + glass_w;
glass_y1   = disp_top - glass_top_off; glass_y0 = glass_y1 - glass_l;   // 38.7 .. 9.48
header_h   = 2.5;                               // display header body, sits on top of J1
disp_gap   = socket_h + header_h;               // 11.0 between ornament top and display rear

// ---------------------------------------------------------------- core tray
floor_t    = 1.6;
post_h     = 3.0;          // PCB standoff, clears J1 / USB-C legs underneath
post_d     = 5.0;
post_hole  = 1.7;          // M2 self-tapping.  3.2 for M2 heat-set inserts.
wall       = 2.4;
clr        = 0.25;         // PCB / display edge clearance per side

z_pcb       = post_h;
z_pcb_top   = post_h + pcb_t;                   // 4.6
z_disp_rear = z_pcb_top + disp_gap;             // 15.6
z_disp_front= z_disp_rear + disp_t;             // 16.8
z_glass     = z_disp_front + glass_t;           // 18.5
z_wall_top  = z_disp_front + 0.8;               // 17.6  bezel plate sits here
bezel_t     = 1.6;
z_top       = z_wall_top + bezel_t;             // 19.2
z_boss_top  = z_disp_rear - 0.3;                // display rests just above the bosses

cx0 = -clr;              cx1 = pcb_w + clr;      // cavity
cy0 = -(ant_over + 1.0); cy1 = disp_top + clr;
tx0 = cx0 - wall;        tx1 = cx1 + wall;       // tray outside
ty0 = cy0 - wall;        ty1 = cy1 + wall;
tray_r = 2;

// display bosses: from the top wall, outside the ornament PCB below z_boss_step, notched over it above
boss_x_l = 4.5;   boss_x_r = 23.0;              // keep clear of J1 (x 4.81 .. 22.69, 8.5 tall)
boss_y_lo = pcb_l + clr;                        // 41.75 at PCB level
boss_y_hi = disp_hole_yt - disp_hole_d/2 - 0.5; // 40.65 above the PCB, so the peg hole has a full wall
boss_z_step = z_pcb_top + 0.8;
boss_hole_d = disp_hole_d;                      // bezel peg passes the display hole into this
boss_hole_depth = 3.2;

// side windows
usb_win   = [usb_y - 6.5, usb_y + 6.5, z_pcb_top - 1.4, z_pcb_top + 5.2];   // y0 y1 z0 z1, USB-C overmould
jack_win  = [jack_y - 5.5, jack_y + 5.5, z_pcb_top - 0.4, z_pcb_top + 10.0]; // 3.5 mm plug overmould <= 10 dia
led_win   = [led_y[0] - 1.0, led_y[1] + 1.0, z_pcb_top, z_pcb_top + 3.4];
led_window = true;

// ---------------------------------------------------------------- OUTER INTERFACE
// Everything an outer needs to know about the core:
env_x0 = tx0; env_x1 = tx1;                 // -2.65 .. 30.15   (32.8)
env_y0 = ty0; env_y1 = ty1;                 // -9.2  .. 47.55   (56.75)
env_z0 = -floor_t; env_z1 = z_top;          // -1.6  .. 19.2    (20.8)
groove_w  = 2.0;  groove_d = 1.0;           // groove in both long sides, full length, open both ends
groove_z0 = 0.6;  groove_z1 = groove_z0 + groove_w;
notch_y0  = -6.5; notch_y1 = -2.9; notch_d = 0.5;   // detent pocket in the groove near the antenna end
// plus the side windows above (usb_win on x = env_x0 face, jack_win on x = env_x1 face)
// and the front window: glass_x0..glass_x1 / glass_y0..glass_y1 on the z = env_z1 face.

// ---------------------------------------------------------------- bezel
win_x0 = glass_x0 - 0.4; win_x1 = glass_x1 + 0.4;
win_y0 = glass_y0 - 0.4; win_y1 = glass_y1 + 0.4;
peg_d  = disp_hole_d - 0.4;
// snap hooks: 2 per long side.  y positions clear of the USB / jack / LED windows and the grooves.
hook_ys   = [6, 42];
hook_w    = 4.0;   arm_t = 1.0;   recess_d = 1.2;
hook_z0   = 9.3;                  // arm tip
catch     = 0.65;  catch_top = 11.2;
slot_z0   = 9.7;   slot_z1 = 11.6;

// ---------------------------------------------------------------- default outer: sleeve
s_clr  = 0.3;
s_wall = 1.8;
ix0 = env_x0 - s_clr; ix1 = env_x1 + s_clr;
iz0 = env_z0 - s_clr; iz1 = env_z1 + s_clr;
iy1 = env_y1 + s_clr;
sx0 = ix0 - s_wall; sx1 = ix1 + s_wall;
sz0 = iz0 - s_wall; sz1 = iz1 + s_wall;
sy0 = env_y0 - 1.0;            // open end, the core face sits 1 mm inside
sy1 = iy1 + s_wall;
rail_t = groove_w - 0.4;  rail_h = groove_d - 0.2 + s_clr;   // 1.6 wide, reaches 0.2 short of the groove floor
bump_h = 0.45;                                          // detent: 0.25 interference outside the notch
loop_od = 9; loop_id = 3.5; loop_t = 4;
front_win_margin = 1.2;        // sleeve window = glass + this, so the bezel's rim is what you see

// ================================================================ helpers
module box(x0, x1, y0, y1, z0, z1) translate([x0, y0, z0]) cube([x1 - x0, y1 - y0, z1 - z0]);
module rbox(x0, x1, y0, y1, z0, z1, r)
    hull() for (x = [x0 + r, x1 - r], y = [y0 + r, y1 - r]) translate([x, y, z0]) cylinder(r = r, h = z1 - z0);
// rounded-rect hole through the x direction
module xslot(x0, x1, y0, y1, z0, z1, r)
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0)
        offset(r) offset(-r) polygon([[y0, z0], [y1, z0], [y1, z1], [y0, z1]]);
module mirror_x(about) { children(); translate([2 * about, 0, 0]) mirror([1, 0, 0]) children(); }

// ================================================================ CORE: tray
module core_tray() {
    difference() {
        union() {
            difference() {
                rbox(tx0, tx1, ty0, ty1, -floor_t, z_wall_top, tray_r);
                box(cx0, cx1, cy0, cy1, 0, z_wall_top + 1);
            }
            for (h = pcb_holes) translate([h[0], h[1], 0]) cylinder(d = post_d, h = post_h);
            // display bosses hanging off the top wall
            for (xr = [[cx0 - eps, boss_x_l], [boss_x_r, cx1 + eps]])
                {
                    box(xr[0], xr[1], boss_y_lo, cy1 + eps, 0, boss_z_step);
                    hull() {   // 45 deg chamfer out over the PCB corner, starting 0.8 above the PCB top
                        box(xr[0], xr[1], boss_y_lo, cy1 + eps, boss_z_step - eps, boss_z_step + 0.1);
                        box(xr[0], xr[1], boss_y_hi, cy1 + eps, boss_z_step + (boss_y_lo - boss_y_hi), z_boss_top);
                    }
                }
        }
        // screw pilots
        for (h = pcb_holes) translate([h[0], h[1], -floor_t - 1]) cylinder(d = post_hole, h = post_h + floor_t + 2);
        // peg holes in the bosses
        for (x = disp_hole_x) translate([x, disp_hole_yt, z_boss_top - boss_hole_depth]) cylinder(d = boss_hole_d, h = boss_hole_depth + 1);
        // USB-C, left wall
        xslot(tx0 - 1, cx0 + 1, usb_win[0], usb_win[1], usb_win[2], usb_win[3], 1.5);
        // DC jack, right wall
        xslot(cx1 - 1, tx1 + 1, jack_win[0], jack_win[1], jack_win[2], jack_win[3], 2);
        // LED glow window, left wall
        if (led_window) xslot(tx0 - 1, cx0 + 1, led_win[0], led_win[1], led_win[2], led_win[3], 0.8);
        // grooves + detent notches (outer interface)
        mirror_x((tx0 + tx1) / 2) {
            box(tx0 - 1, tx0 + groove_d, ty0 - 1, ty1 + 1, groove_z0, groove_z1);
            box(tx0 - 1, tx0 + groove_d + notch_d, notch_y0, notch_y1, groove_z0, groove_z1);
        }
        // hook recesses and catch slots
        mirror_x((tx0 + tx1) / 2) for (hy = hook_ys) {
            box(tx0 - 1, tx0 + recess_d, hy - hook_w/2 - 0.3, hy + hook_w/2 + 0.3, hook_z0 - 0.3, z_wall_top + 1);
            box(tx0 - 1, cx0 + eps,      hy - hook_w/2 - 0.3, hy + hook_w/2 + 0.3, slot_z0, slot_z1);
        }
    }
}

// ================================================================ CORE: bezel
module hook_left(hy) {
    // arm flush with the tray's outside face, catch pointing into the wall
    box(tx0, tx0 + arm_t, hy - hook_w/2, hy + hook_w/2, hook_z0, z_wall_top + 0.3);
    translate([0, hy + hook_w/2, 0]) rotate([90, 0, 0]) linear_extrude(hook_w)
        polygon([[tx0 + arm_t - 0.3, hook_z0 + 0.3], [tx0 + arm_t + catch, hook_z0 + 1.1],
                 [tx0 + arm_t + catch, catch_top], [tx0 + arm_t - 0.3, catch_top]]);
}
module core_bezel() {
    difference() {
        union() {
            rbox(tx0, tx1, ty0, ty1, z_wall_top, z_top, tray_r);
            // pads that bear on the display PCB (corners only: the FPC fold lives in the bottom strip)
            for (xr = [[cx0, boss_x_l], [boss_x_r, cx1]]) {
                box(xr[0], xr[1], win_y1, cy1, z_disp_front, z_wall_top + 0.3);
                box(xr[0], xr[1], disp_bot - 0.3, win_y0, z_disp_front, z_wall_top + 0.3);
            }
            // locating pegs: top ones go on through the display into the tray bosses
            for (x = disp_hole_x) {
                translate([x, disp_hole_yt, z_boss_top - boss_hole_depth + 0.6]) cylinder(d = peg_d, h = z_disp_front - (z_boss_top - boss_hole_depth + 0.6) + 0.5);
                intersection() {   // D-shaped: clipped so nothing pokes into the window
                    translate([x, disp_hole_yb, z_disp_rear - 0.3]) cylinder(d = peg_d, h = z_disp_front - z_disp_rear + 0.3 + 0.5);
                    box(x - 2, x + 2, disp_bot, win_y0 - 0.15, z_disp_rear - 1, z_disp_front + 1);
                }
            }
            mirror_x((tx0 + tx1) / 2) for (hy = hook_ys) hook_left(hy);
        }
        box(win_x0, win_x1, win_y0, win_y1, z_disp_front - 1, z_top + 1);
    }
}

// ================================================================ OUTER: default hanging sleeve
module rbox4(x0, x1, y0, y1, z0, z1, rb, rt)   // different radii at the y- (rb) and y+ (rt) corners
    hull() {
        for (x = [x0 + rb, x1 - rb]) translate([x, y0 + rb, z0]) cylinder(r = rb, h = z1 - z0);
        for (x = [x0 + rt, x1 - rt]) translate([x, y1 - rt, z0]) cylinder(r = rt, h = z1 - z0);
    }
module rail_left() {
    zc = (groove_z0 + groove_z1) / 2;
    ry0 = sy0 - eps; ry1 = iy1 - 0.5;
    // rail with a lead-in at the open end
    hull() {
        box(ix0 - eps, ix0 + 0.1,   ry0,       ry0 + 0.1, zc - rail_t/2, zc + rail_t/2);
        box(ix0 - eps, ix0 + rail_h, ry0 + 1.0, ry1,       zc - rail_t/2, zc + rail_t/2);
    }
    // detent bump matching the core's notch
    hull() {
        box(ix0 - eps, ix0 + rail_h + bump_h, notch_y0 + 0.5, notch_y1 - 0.5, zc - rail_t/2, zc + rail_t/2);
        box(ix0 - eps, ix0 + rail_h,          notch_y0 - 0.3, notch_y1 + 0.3, zc - rail_t/2, zc + rail_t/2);
    }
}
module outer_sleeve() {
    xc = (sx0 + sx1) / 2;  zc = (sz0 + sz1) / 2;
    difference() {
        union() {
            rbox4(sx0, sx1, sy0, sy1, sz0, sz1, 1.0, 5);
            // hanging loop, hole front-to-back
            translate([xc, sy1 + loop_od/2 - 2, zc]) cylinder(d = loop_od, h = loop_t, center = true);
        }
        translate([xc, sy1 + loop_od/2 - 2, zc]) cylinder(d = loop_id, h = loop_t + 2, center = true);
        // pocket, open at the y- end
        box(ix0, ix1, sy0 - 1, iy1, iz0, iz1);
        // front window
        rbox(glass_x0 - front_win_margin, glass_x1 + front_win_margin,
             glass_y0 - front_win_margin, glass_y1 + front_win_margin, iz1 - eps, sz1 + 1, 1.5);
        // cable holes, same places as the core's windows
        xslot(sx0 - 1, ix0 + 1, usb_win[0],  usb_win[1],  usb_win[2],  usb_win[3],  1.5);
        xslot(ix1 - 1, sx1 + 1, jack_win[0], jack_win[1], jack_win[2], jack_win[3], 2);
    }
    mirror_x(xc) rail_left();
}

// ================================================================ mock electronics for the assembly view
module mock_boards() {
    color("darkgreen", 0.6) difference() {
        box(0, pcb_w, 0, pcb_l, z_pcb, z_pcb_top);
        for (h = pcb_holes) translate([h[0], h[1], z_pcb - 1]) cylinder(d = 2.4, h = 4);
    }
    color("silver", 0.6) box(4.7, 22.8, -ant_over, 19.8, z_pcb_top, z_pcb_top + 3.1);          // WROOM-1
    color("black",  0.6) box(4.81, 22.69, 38.69, 41.33, z_pcb_top, z_pcb_top + socket_h);       // J1
    color("black",  0.6) box(4.81, 22.69, 38.69, 41.33, z_pcb_top + socket_h, z_disp_rear);     // display header
    color("gray",   0.6) box(-0.5, 7.75, usb_y - 4.5, usb_y + 4.5, z_pcb_top, z_pcb_top + 3.3); // USB-C
    color("gray",   0.6) box(15.75, 27.5, jack_y - 2.55, jack_y + 2.55, z_pcb_top, z_pcb_top + 7.3); // DC-002
    color("darkblue", 0.6) difference() {
        box(0, disp_w, disp_bot, disp_top, z_disp_rear, z_disp_front);
        for (x = disp_hole_x, y = [disp_hole_yt, disp_hole_yb]) translate([x, y, z_disp_rear - 1]) cylinder(d = disp_hole_d, h = 4);
    }
    color("black", 0.8) box(glass_x0, glass_x1, glass_y0, glass_y1, z_disp_front, z_glass);
}

// ================================================================ part selection
if (part == "tray")        core_tray();
if (part == "bezel")       core_bezel();
if (part == "sleeve")      outer_sleeve();
if (part == "tray_print")  translate([0, 0, floor_t]) core_tray();
if (part == "bezel_print") rotate([180, 0, 0]) translate([0, 0, -z_top]) core_bezel();     // face down, hooks up
if (part == "sleeve_print") translate([0, 0, -sy0]) rotate([90, 0, 0]) outer_sleeve();     // stands on its open end, loop up
if (part == "assembly") {
    core_tray();
    mock_boards();
    color("orange", 0.5) core_bezel();
    color("white", 0.3) outer_sleeve();
}
if (part == "core") { core_tray(); mock_boards(); color("orange", 0.7) core_bezel(); }

// ---------------------------------------------------------------- self-checks (render these to STL; volume should be ~0)
if (part == "check_tray_boards")  intersection() { core_tray();  mock_boards(); }
if (part == "check_bezel_boards") intersection() { core_bezel(); mock_boards(); }
if (part == "check_bezel_tray")   intersection() { core_bezel(); core_tray(); }
if (part == "check_sleeve_core")  intersection() { outer_sleeve(); union() { core_tray(); core_bezel(); } }
// core pushed 8 mm out of the sleeve: the only overlap should be the 4 detent bumps riding the groove floor
if (part == "check_sleeve_slide") intersection() { outer_sleeve(); translate([0, -8, 0]) union() { core_tray(); core_bezel(); } }
