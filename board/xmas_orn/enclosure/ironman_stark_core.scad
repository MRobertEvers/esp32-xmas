// Stark Core: implements image concept 03 with a raised reactor above the display.
// Shared mechanical interface is imported from the original case, without modifying it.
// part: stark_core | body/gold/black/cyan[_print] | check_core/colours/backing/window
include <xmas_orn_case.scad>
part = "stark_core";
preview_display = true;

sc_cx = (glass_x0+glass_x1)/2;
sc_cy = (glass_y0+glass_y1)/2-4;
sc_bottom = -32;
sc_top = 60;
sc_half_width = 29;
sc_bevel = 1.5;
sc_inlay = 0.8;
sc_reactor_y = 38.5;
sc_reactor_r = 16.2;
sc_reactor_raise = 1.0;
sc_names = ["body","gold","black","cyan"];
sc_colours = ["#b62232","#dfb65e","#292e35","#a8eeff"];
assert(sc_inlay > 0 && sc_inlay <= s_wall-0.8);
assert(sc_reactor_y-sc_reactor_r > 4+glass_l/2+front_win_margin+1);

// --------------------------------------------------------------- 2D artwork and silhouette
module sc_pair() { children(); mirror([1,0,0]) children(); }
module sc_outline() polygon([
    [-23,sc_bottom],[23,sc_bottom],[sc_half_width,-26],[sc_half_width,54],
    [23,sc_top],[-23,sc_top],[-sc_half_width,54],[-sc_half_width,-26]]);
module sc_window(expand=0) translate([0,4]) offset(delta=expand) offset(r=1.5)
    square([glass_w+2*front_win_margin-3,glass_l+2*front_win_margin-3],center=true);
module sc_ring(inner,outer) difference() {
    circle(r=outer,$fn=160); circle(r=inner,$fn=160);
}
module sc_line(points,w=0.6) for(i=[0:len(points)-2]) hull() {
    translate(points[i]) circle(d=w,$fn=16);
    translate(points[i+1]) circle(d=w,$fn=16);
}
module sc_rails_art() sc_pair() polygon([
    [24,51],[26,48],[26,-24],[23.5,-27],[23,-24],[23,48]]);
module sc_panel_seams() {
    sc_line([[0,58],[0,55]],0.55);
    sc_line([[0,-30],[0,-15]],0.55);
    sc_pair() {
        sc_line([[21.5,57],[18.2,53],[18.2,48]],0.55);
        sc_line([[22,-29],[18,-25],[18,-15]],0.55);
    }
}
function sc_polar(r,a) = [r*cos(a),r*sin(a)];
module sc_segment() offset(r=0.45) offset(delta=-0.45) polygon(concat(
    [for(a=[-15:3:15]) sc_polar(12.7,a)],
    [for(a=[15:-3:-15]) sc_polar(8.0,a)]));
module sc_lenses() {
    circle(r=4.8,$fn=120);
    for(a=[0:45:315]) rotate(a+90) sc_segment();
}
module sc_reactor_gold() sc_ring(14.3,15.0);

// All front artwork uses the board's native coordinates and common 0.8 mm inlay depth.
module sc_face(z,h) translate([sc_cx,sc_cy,z]) linear_extrude(h) children();
module sc_reactor_face(z,h) translate([sc_cx,sc_cy+sc_reactor_y,z]) linear_extrude(h) children();
module sc_stand() translate([0,0,-(sc_cy+sc_bottom)]) rotate([90,0,0]) children();

// --------------------------------------------------------------- base sleeve
module sc_blank() {
    // Front perimeter bevel is actual geometry, not a painted outline.
    translate([sc_cx,sc_cy,0]) hull() {
        translate([0,0,sz0]) linear_extrude(sz1-sz0-sc_bevel) sc_outline();
        translate([0,0,sz1-eps]) linear_extrude(eps) offset(delta=-sc_bevel) sc_outline();
    }
}
module sc_body() {
    difference() {
        union() { sc_blank(); hang_loop(sc_cx,sc_cy+sc_top); }
        hang_loop_hole(sc_cx,sc_cy+sc_top);
        sleeve_cuts(ext=40,finger=true);
        // Flush gold rails and display bezel.
        sc_face(sz1-sc_inlay,sc_inlay+eps) { sc_rails_art(); sc_window(1.0); }
        // Reactor module seats 0.8 mm into the body and projects 1 mm above it.
        sc_reactor_face(sz1-sc_inlay,sc_inlay+sc_reactor_raise+eps)
            circle(r=sc_reactor_r,$fn=160);
        sc_face(sz1-0.35,0.35+eps) sc_panel_seams();
    }
    sleeve_rails();
}

// --------------------------------------------------------------- four colour parts
module sc_gold() {
    difference() {
        sc_face(sz1-sc_inlay,sc_inlay) sc_rails_art();
        sleeve_cuts(ext=40,finger=true);
    }
    sc_reactor_face(sz1+sc_reactor_raise-sc_inlay,sc_inlay) sc_reactor_gold();
}
module sc_reactor_housing() difference() {
    translate([sc_cx,sc_cy+sc_reactor_y,sz1-sc_inlay]) {
        cylinder(r=sc_reactor_r,h=sc_inlay,$fn=160);
        // 45-degree sloping shoulder supports the raised detail when printed upright.
        translate([0,0,sc_inlay]) cylinder(r1=sc_reactor_r,
            r2=sc_reactor_r-sc_reactor_raise,h=sc_reactor_raise,$fn=160);
    }
    sc_reactor_face(sz1+sc_reactor_raise-sc_inlay,sc_inlay+eps) {
        sc_reactor_gold(); sc_lenses();
    }
}
module sc_black() {
    difference() {
        sc_face(sz1-sc_inlay,sc_inlay) sc_window(1.0);
        sleeve_cuts(ext=40,finger=true);
    }
    sc_reactor_housing();
}
module sc_cyan() sc_reactor_face(sz1+sc_reactor_raise-sc_inlay,sc_inlay) sc_lenses();
module sc_piece(c) {
    if(c==0) sc_body();
    if(c==1) sc_gold();
    if(c==2) sc_black();
    if(c==3) sc_cyan();
}
module sc_solid() union() for(c=[0:3]) sc_piece(c);

// --------------------------------------------------------------- assembly and exports
if(part=="stark_core") {
    for(c=[0:3]) color(sc_colours[c]) render() sc_piece(c);
    if(preview_display) color("#101a25") sc_face(sz1-sc_inlay-0.1,0.05) sc_window();
}
for(c=[0:3]) {
    if(part==sc_names[c]) sc_piece(c);
    if(part==str(sc_names[c],"_print")) sc_stand() sc_piece(c);
}

// --------------------------------------------------------------- geometric checks
if(part=="check_core") intersection() { sc_solid(); union() { core_tray(); core_bezel(); } }
if(part=="check_colours") for(a=[0:2],b=[a+1:3])
    intersection() { sc_piece(a); sc_piece(b); }
if(part=="check_window") intersection() {
    sc_solid();
    // Use a slight inset to ignore shared boundary faces of the actual opening.
    sc_face(iz1,5) sc_window(-0.02);
}
if(part=="check_backing") {
    // Flush inlays and the reactor's seat must have body material immediately behind.
    difference() {
        sc_face(sz1-2*sc_inlay,sc_inlay) sc_rails_art();
        sc_body();
    }
    difference() {
        sc_reactor_face(sz1-2*sc_inlay,sc_inlay) circle(r=sc_reactor_r,$fn=160);
        sc_body();
    }
    difference() {
        translate([0,0,-sc_inlay]) difference() {
            sc_face(sz1-sc_inlay,sc_inlay) sc_window(1.0);
            sleeve_cuts(ext=40,finger=true);
        }
        sc_body();
    }
    // Raised reactor lenses and gold ring have their own charcoal structural backing.
    difference() {
        sc_reactor_face(sz1+sc_reactor_raise-2*sc_inlay,sc_inlay) {
            sc_reactor_gold(); sc_lenses();
        }
        sc_reactor_housing();
    }
}
