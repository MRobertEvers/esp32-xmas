// Shared construction for selected image concepts 02 and 05.
// Open ironman_trapezoid_armor.scad or ironman_reactor_medallion.scad.
include <xmas_orn_case.scad>
design = 0;
part = "trapezoid_armor";
preview_display = true;
cp_themes = ["trapezoid_armor","reactor_medallion"];
cp_names = ["body","black","gold","cyan"];
cp_colours = ["#b62232","#292e35","#dfb65e","#a8eeff"];
cp_cx = (glass_x0+glass_x1)/2;
cp_cy = (glass_y0+glass_y1)/2-4;
cp_bottom = [-39,-37];
cp_top = [46,49];
cp_inlay = 0.8;
cp_bevel = 1.5;
assert(cp_inlay <= s_wall-0.8);

module cp_pair() { children(); mirror([1,0,0]) children(); }
module cp_line(points,w=0.7) for(i=[0:len(points)-2]) hull() {
    translate(points[i]) circle(d=w,$fn=16);
    translate(points[i+1]) circle(d=w,$fn=16);
}
module cp_window(expand=0) translate([0,4]) offset(delta=expand) offset(r=1.5)
    square([glass_w+2*front_win_margin-3,glass_l+2*front_win_margin-3],center=true);
function cp_polar(r,a) = [r*cos(a),r*sin(a)];
module cp_arc(ri,ro,angle) polygon(concat(
    [for(i=[0:12]) cp_polar(ro,-angle/2+angle*i/12)],
    [for(i=[12:-1:0]) cp_polar(ri,-angle/2+angle*i/12)]));
module cp_outline(t) {
    if(t==0) polygon([
        [-10,46],[10,46],[18,51],[29,48],[39,39],[46,31],[46,19],
        [43,13],[42,1],[39,-13],[34,-25],[20,-39],[-20,-39],
        [-34,-25],[-39,-13],[-42,1],[-43,13],[-46,19],[-46,31],
        [-39,39],[-29,48],[-18,51]]);
    if(t==1) intersection() {
        translate([0,4]) circle(r=45,$fn=192);
        translate([-50,cp_bottom[t]]) square([100,100]);
    }
}
module cp_trapezoid() polygon([[-27,25],[27,25],[21,-22],[-21,-22]]);

// --------------------------------------------------------------- raw colour regions
module cp_gold(t) {
    if(t==0) {
        difference() { cp_trapezoid(); offset(delta=-2.5) cp_trapezoid(); }
        cp_pair() {
            polygon([[15,46],[19,49],[22,44],[18,40],[14,42]]);
            polygon([[31,42],[37,37],[41,28],[36,30],[30,37]]);
            polygon([[35,13],[41,10],[39,-4],[33,-10],[30,-5],[32,7]]);
        }
    }
    if(t==1) translate([0,4]) for(a=[0:60:300]) rotate(a)
        polygon([[-6.5,38.5],[6.5,38.5],[8,36],[5,29.5],[-5,29.5],[-8,36]]);
}
module cp_arm_red_panels() cp_pair() {
    polygon([[34,39],[43,31],[44,22],[39,17],[36,28]]);
    polygon([[39,-6],[36,-20],[26,-31],[28,-20],[33,-10]]);
}
module cp_black(t) {
    if(t==0) difference() {
        union() {
            offset(delta=1.0) cp_trapezoid();
            polygon([[-14,41],[14,41],[12,37],[-12,37]]);
            cp_pair() {
                polygon([[28,45],[33,42],[38,34],[43,30],[42,16],[35,12],
                         [30,17],[32,28],[27,36]]);
                polygon([[34,15],[43,13],[41,0],[37,-10],[34,-25],[23,-34],
                         [25,-23],[30,-9],[28,1]]);
            }
        }
        // A small clearance removes point-only contacts at the elbow seams.
        offset(delta=0.15) cp_arm_red_panels();
    }
    if(t==1) translate([0,4]) circle(r=40.5,$fn=192);
}
module cp_cyan(t) {
    if(t==0) difference() {
        offset(delta=-2.5) cp_trapezoid();
        offset(delta=-4.1) cp_trapezoid();
    }
    if(t==1) translate([0,4]) {
        for(a=[0:30:330]) rotate(a)
            offset(r=0.45) offset(delta=-0.45) cp_arc(22.6,26.6,27);
        for(a=[0:60:300]) rotate(a)
            offset(r=0.35) offset(delta=-0.35) cp_arc(36.5,37.8,18);
    }
}
module cp_raw(t,c) {
    if(c==1) cp_black(t);
    if(c==2) cp_gold(t);
    if(c==3) cp_cyan(t);
}
module cp_all_ink(t) union() for(c=[1:3]) cp_raw(t,c);
module cp_ink(t,c) difference() {
    intersection() { offset(delta=-cp_bevel) cp_outline(t); cp_raw(t,c); }
    if(c<3) for(k=[c+1:3]) cp_raw(t,k);
}

// Raised red panels give the armour real relief; no painted fake highlights.
module cp_red_islands(t) difference() {
    intersection() {
        offset(delta=-cp_bevel-0.5) cp_outline(t);
        union() {
            if(t==0) {
                polygon([[-9,46],[9,46],[10,42],[-10,42]]);
                polygon([[-10,35],[10,35],[8,27],[-8,27]]);
                polygon([[-16,-26],[16,-26],[14,-35],[-14,-35]]);
                cp_pair() {
                    polygon([[13,34],[19,38],[26,34],[30,27],[26,22],[25,26],[12,26]]);
                    polygon([[31,15],[34,2],[30,-18],[24,-29],[21,-23],[25,-5]]);
                }
                cp_arm_red_panels();
            }
            if(t==1) translate([0,4]) for(a=[0:60:300]) rotate(a) cp_arc(40.8,44,56);
        }
    }
    offset(delta=0.6) cp_all_ink(t);
}
module cp_seams(t) {
    if(t==0) cp_pair() {
        cp_line([[29,44],[24,39],[26,34]],0.6);
        cp_line([[45,20],[40,17]],0.6);
        cp_line([[36,-9],[32,-20],[25,-26]],0.6);
    }
    if(t==1) translate([0,4]) for(a=[0:60:300]) rotate(a)
        cp_line([[40.8,0],[45,0]],0.65);
}

// --------------------------------------------------------------- common mechanical body
module cp_face(z,h) translate([cp_cx,cp_cy,z]) linear_extrude(h) children();
module cp_relief(z,h,bevel) {
    // Overlap the shoulder slightly so its rounded footprint cannot leave coplanar
    // numerical seams along the straight base edges when exported to binary STL.
    cp_face(z,h-bevel+2*eps) children();
    // A tapered Minkowski shoulder keeps separate panels and annular holes intact.
    // A global hull would incorrectly bridge the reactor ring and separate gold blocks.
    translate([cp_cx,cp_cy,z+h-bevel]) minkowski() {
        linear_extrude(eps) offset(delta=-bevel) children();
        // Keep the rounded shoulder strictly inside the base footprint. Exact tangency
        // creates sub-micron fins when Clipper offsets meet Minkowski mesh coordinates.
        cylinder(r1=bevel-0.03,r2=0,h=bevel-eps,$fn=16);
    }
}
module cp_blank(t) cp_relief(sz0,sz1-sz0,cp_bevel) cp_outline(t);
module cp_body(t) {
    difference() {
        union() {
            cp_blank(t);
            // Each island starts inside the base, with a 45-degree bevel on its raised top.
            cp_relief(sz1-0.1,0.75,0.65) cp_red_islands(t);
            hang_loop(cp_cx,cp_cy+cp_top[t]);
        }
        hang_loop_hole(cp_cx,cp_cy+cp_top[t]);
        sleeve_cuts(ext=45,finger=true);
        cp_face(sz1-cp_inlay,3) cp_all_ink(t);
        cp_face(sz1-0.35,2) cp_seams(t);
    }
    sleeve_rails();
}
module cp_colour(t,c) difference() {
    // Gold is raised 0.8 mm, cyan 0.35 mm; both have sloping edges on a full-depth seat.
    if(c==1) cp_face(sz1-cp_inlay,cp_inlay) cp_ink(t,c);
    else let(rise=c==2?0.8:0.35)
        cp_relief(sz1-cp_inlay,cp_inlay+rise,rise) cp_ink(t,c);
    sleeve_cuts(ext=45,finger=true);
}
module cp_piece(t,c) { if(c==0) cp_body(t); else cp_colour(t,c); }
module cp_solid(t) union() for(c=[0:3]) cp_piece(t,c);
module cp_stand(t) translate([0,0,-(cp_cy+cp_bottom[t])]) rotate([90,0,0]) children();

// --------------------------------------------------------------- views / export selectors
if(part==cp_themes[design]) {
    for(c=[0:3]) color(cp_colours[c]) render() cp_piece(design,c);
    if(preview_display) color("#101a25") cp_face(sz1-cp_inlay-0.1,0.05) cp_window();
}
for(c=[0:3]) {
    if(part==cp_names[c]) cp_piece(design,c);
    if(part==str(cp_names[c],"_print")) cp_stand(design) cp_piece(design,c);
}
if(part=="check_core") intersection() { cp_solid(design); union() { core_tray(); core_bezel(); } }
if(part=="check_colours") for(a=[0:2],b=[a+1:3])
    intersection() { cp_piece(design,a); cp_piece(design,b); }
if(part=="check_window") intersection() {
    cp_solid(design); cp_face(iz1,5) cp_window(-0.02);
}
if(part=="check_backing") for(c=[1:3]) difference() {
    translate([0,0,-cp_inlay]) difference() {
        cp_face(sz1-cp_inlay,cp_inlay) cp_ink(design,c);
        sleeve_cuts(ext=45,finger=true);
    }
    cp_body(design);
}
