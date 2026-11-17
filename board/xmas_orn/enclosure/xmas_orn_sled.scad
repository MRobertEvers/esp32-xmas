// xmas_orn_sled.scad
// One-piece sled for the xmas_orn rev 2 board + 1.3" display.  A drop-in alternative to the
// two-piece core (tray + snap bezel) in xmas_orn_case.scad: same footprint, same side grooves and
// detent notches, so it slides into every outer that takes the core (default sleeve, the
// Funko Pop! outers, outers v2, the Iron Man set).
//
//   sled    floor + four M2 PCB posts + low side rails carrying the grooves + full-height end walls.
//           The top end wall carries two bosses; the display's top two holes screw into them (M2 x 6).
//           The sides are open above the rails, so the jack and USB-C are reachable with the sled out.
//   bridge  a small H that stands on the ornament PCB under the display's bottom corners.  Its
//           D-shaped pegs locate in the display's bottom holes (which sit right at the glass edge,
//           so they cannot take screw heads).  It holds the display up; the top screws hold it down.
//
// Assembly: drop the board straight in (USB-C on the left, header on the boss end) and screw it
// down, 4 x M2 x 6.  Set the bridge on the board, plug the display onto J1 so its bottom holes
// take the bridge pegs, then 2 x M2 x 6 through the display's top holes into the bosses.  Slide the
// sled into an outer antenna-end last until the detents click, then one M2 x 8 flat head through
// the countersunk hole in the outer's back into the fixing boss.  Unscrew, then pull by the nail groove.
//
// Frame and all board/display numbers come from xmas_orn_case.scad (z = 0 at the floor top).
// Render: ./export_sled.sh

include <xmas_orn_case.scad>
part = "sled_assembly";   // sled | bridge | sled_print | bridge_print | sled_assembly | sled_in_sleeve | check_*

// ---------------------------------------------------------------- sled
rail_top    = 3.6;                    // side rails: groove 0.6..2.6, 1.0 above it, PCB bottom at 3.0
end_top     = z_wall_top;             // 17.6, end walls stop 1.9 below an outer's pocket roof
disp_seat   = z_disp_rear - 0.1;      // bosses + bridge, 0.1 under the J1 header seat
sboss_y0    = pcb_l + 0.2;            // 41.7: bosses never overhang the PCB, so it drops straight in
sboss_pilot = post_hole;              // M2 self-tapping (the pilot breaks 0.15 out of the boss face: fine)
sboss_depth = 6.5;
nail_z      = [7.5, 9.0];             // pull groove across the antenna-end face
nail_d      = 1.0;
fix_boss_w   = 6.0;                   // fixing-screw boss (fix_x / fix_y from the outer interface)
fix_boss_top = 8.0;                   // well under the display rear (15.6); takes M2 x 8 .. x 10
fix_pilot_top = 7.2;

module sled() {
    difference() {
        union() {
            // floor + low side rails
            difference() {
                rbox(tx0, tx1, ty0, ty1, -floor_t, rail_top, tray_r);
                box(cx0, cx1, cy0, cy1, 0, rail_top + 1);
            }
            // end walls, full height
            intersection() {
                rbox(tx0, tx1, ty0, ty1, -floor_t, end_top, tray_r);
                union() {
                    box(tx0, tx1, ty0, cy0 + eps, -floor_t, end_top);
                    box(tx0, tx1, cy1 - eps, ty1, -floor_t, end_top);
                }
            }
            // PCB posts
            for (h = pcb_holes) translate([h[0], h[1], 0]) cylinder(d = post_d, h = post_h);
            // display bosses, off the top end wall, clear of J1 (x 4.81 .. 22.69)
            for (xr = [[cx0 - eps, boss_x_l], [boss_x_r, cx1 + eps]])
                box(xr[0], xr[1], sboss_y0, cy1 + eps, 0, disp_seat);
            // fixing-screw boss between them, the screw comes up through the outer's back and the floor
            box(fix_x - fix_boss_w/2, fix_x + fix_boss_w/2, sboss_y0, cy1 + eps, 0, fix_boss_top);
        }
        translate([fix_x, fix_y, -floor_t - 1]) cylinder(d = post_hole, h = fix_pilot_top + floor_t + 1);
        // PCB screw pilots
        for (h = pcb_holes) translate([h[0], h[1], -floor_t - 1]) cylinder(d = post_hole, h = post_h + floor_t + 2);
        // display screw pilots
        for (x = disp_hole_x) translate([x, disp_hole_yt, disp_seat - sboss_depth]) cylinder(d = sboss_pilot, h = sboss_depth + 1);
        // USB-C plug overmould dips into the left rail
        xslot(tx0 - 1, cx0 + 1, usb_win[0], usb_win[1], usb_win[2], usb_win[3], 1.5);
        // grooves + detent notches: the outer interface, identical to the core tray
        mirror_x((tx0 + tx1) / 2) {
            box(tx0 - 1, tx0 + groove_d, ty0 - 1, ty1 + 1, groove_z0, groove_z1);
            box(tx0 - 1, tx0 + groove_d + notch_d, notch_y0, notch_y1, groove_z0, groove_z1);
        }
        // nail groove on the antenna-end face
        box(tx0 + 4, tx1 - 4, ty0 - 1, ty0 + nail_d, nail_z[0], nail_z[1]);
    }
}

// ---------------------------------------------------------------- display bridge
// Legs on the ornament PCB at the display's bottom holes (2.5 / 25, 8.4).  The foot is clipped to
// stay off the M2 screw heads at y = 5 (head dia 3.8, to y 6.9 and z 5.9), the LEDs (D1 from y 10.2,
// x <= 3.7) and the WROOM-1 edge pads (x 4.4 .. 23.1).
br_foot_top = z_pcb_top + 1.9;        // 6.5: clear of the screw heads above this
br_leg_d    = 3.8;               // 0.2 / 0.3 clear of the WROOM-1 can sides (x 4.7 / 22.8)
br_bar      = [7.4, 9.4, disp_seat - 2.4, disp_seat - 0.6];   // y0 y1 z0 z1, crossbar under the display, 0.7 off its rear
br_peg_h    = 1.0;                    // < display thickness 1.2, so nothing reaches the front

function br_foot_x(x) = x < pcb_w / 2 ? [0.6, 4.1] : [pcb_w - 4.1, pcb_w - 0.6];

module bridge() {
    for (x = disp_hole_x) {
        fx = br_foot_x(x);
        intersection() {   // foot
            translate([x, disp_hole_yb, z_pcb_top]) cylinder(d = br_leg_d, h = br_foot_top - z_pcb_top + eps);
            box(fx[0], fx[1], 7.2, 9.9, z_pcb_top, br_foot_top + 1);
        }
        hull() {           // leg: widens from the clipped foot to the full round over 0.6 mm (no flat overhang)
            intersection() {
                translate([x, disp_hole_yb, br_foot_top]) cylinder(d = br_leg_d, h = eps);
                box(fx[0], fx[1], 7.2, 9.9, br_foot_top, br_foot_top + 1);
            }
            translate([x, disp_hole_yb, br_foot_top + 0.6]) cylinder(d = br_leg_d, h = disp_seat - br_foot_top - 0.6);
        }
        intersection() {   // D-shaped peg, clipped off the glass exactly as the old bezel's
            translate([x, disp_hole_yb, disp_seat - eps]) cylinder(d = peg_d, h = br_peg_h + eps);
            box(x - 2, x + 2, disp_bot, glass_y0 - 0.15, disp_seat - 1, disp_seat + 2);
        }
    }
    box(disp_hole_x[0], disp_hole_x[1], br_bar[0], br_bar[1], br_bar[2], br_bar[3]);
}

// ---------------------------------------------------------------- extra mock parts for the checks
module mock_extras() {
    // M2 pan heads on the PCB screws
    for (h = pcb_holes) translate([h[0], h[1], z_pcb_top]) cylinder(d = 3.8, h = 1.3);
    // D1, D2, R5, R6 on the left edge; the WROOM-1 pads either side of its can
    box(0.66, 3.7, 10.2, 18.25, z_pcb_top, z_pcb_top + 0.8);
    box(4.4, 5.6, -0.5, 19.8, z_pcb_top, z_pcb_top + 0.1);
    box(21.9, 23.1, -0.5, 19.8, z_pcb_top, z_pcb_top + 0.1);
    // display screws, M2 x 6 from the front through the top holes
    for (x = disp_hole_x) translate([x, disp_hole_yt, z_disp_front - 6]) cylinder(d = 1.5, h = 6);   // root diameter: the threads cut the pilot
}
module mock_all() { mock_boards(); mock_extras(); }

// ---------------------------------------------------------------- part selection
if (part == "sled")          sled();
if (part == "bridge")        bridge();
if (part == "sled_print")    translate([0, 0, floor_t]) sled();
if (part == "bridge_print")  translate([0, 0, -z_pcb_top]) bridge();       // feet down; the crossbar bridges 22 mm
if (part == "sled_assembly") { sled(); color("orange") bridge(); mock_boards(); }
if (part == "sled_in_sleeve") { sled(); color("orange") bridge(); mock_boards(); color("white", 0.3) outer_sleeve(); }

// self-checks: each should render empty (OpenSCAD reports "Current top level object is empty")
if (part == "check_sled_boards")    intersection() { sled(); mock_all(); }
if (part == "check_bridge_boards")  intersection() { bridge(); difference() { mock_all(); bridge_contact(); } }
if (part == "check_bridge_sled")    intersection() { bridge(); sled(); }
if (part == "check_sled_in_core")   difference() { sled(); hull() core_tray(); }      // sled inside the core envelope
if (part == "check_sleeve_sled")    intersection() { outer_sleeve(); union() { sled(); bridge(); } }
// sled pushed 8 mm out of the sleeve: only the four detent bumps should overlap (in the grooves)
if (part == "check_sleeve_slide")   intersection() { outer_sleeve(); translate([0, -8, 0]) sled(); }
// the fixing screw (M2 x 8 flat head, seated in the sleeve's countersink) against the sled: only the
// thread biting the 1.7 pilot should overlap
if (part == "check_fix_screw")      intersection() { fix_screw(); union() { sled(); mock_all(); } }
if (part == "check_fix_screw_sleeve") intersection() { fix_screw(); outer_sleeve(); }
module fix_screw(core_d = 1.5) translate([fix_x, fix_y, iz0 - fix_back_t]) {
    cylinder(d1 = 3.8, d2 = core_d, h = (3.8 - core_d) / 2);
    cylinder(d = core_d, h = 8);
}

// the faces the bridge is meant to touch: the PCB top under the feet, the display rear and its holes
module bridge_contact() {
    box(-1, pcb_w + 1, 6, 11, z_pcb_top - 0.05, z_pcb_top + 0.05);
    for (x = disp_hole_x) translate([x, disp_hole_yb, z_disp_rear - 0.05]) cylinder(d = disp_hole_d + 0.1, h = disp_t + 0.1);
}
