// esp32-piano's box, DRAFT (see README.md), from esp32-mukklet's case v2: the same
// parts, plus a hole in the back for the 4-wire cable to the toy's MCP23017.
// A small wedge, the 1.69" ST7789V2 TFT (portrait) on
// the front panel tilted back, the KY-040 knob on top, USB-C out the back.
// Lifted from ../esp32-desk-display/enclosure/enclosure_v2.scad. The ESP32
// mini (D1 mini layout) lies flat on the floor at the back, pin headers
// pointing up; the module cables plug onto them. No sensors, so no
// sensor bay and no hood: only the ESP32's own warmth to vent.
// Sizes in mm, from MEASUREMENTS.md; values marked ASSUMED were not
// measured (the TFT's are the seller's, not ours, until measured).
//
// Axes: X = width, left to right seen from the front; Y = depth, front at
// y = 0; Z = up, table at z = 0 (rubber feet not modelled).
//
// Printed parts:
//   body  - side walls, top with the knob's hole and anti-turn ribs, back with a ledge
//           over the USB socket; open at the bottom and at the front
//   front - the tilted screen panel, a flat plate between the side walls
//           with the TFT's 4 screw posts: the TFT is screwed on from behind
//           while the plate is loose, then 4 screws from the front hold
//           the plate on the body's two rails
//   base  - floor plate, the whole footprint (the body's walls stand on
//           it), with the strip under the screen and the ESP32 mini's
//           cradle, 4 screws up into the body's corner bosses
// Pick one with -D 'part="body"' (see export.sh). Single parts come out in
// print orientation, "assembly" shows everything in place with stand-in
// blocks for the modules, "clash" is empty when nothing overlaps.

part = "assembly"; // [assembly, body, front, base, test_knob, clash, dims]
cut = -1; // assembly only: >= 0 cuts the printed parts away left of this x
show_body = true; // assembly only: untick to see inside
show_front = true;
show_base = true;
show_tft = true; // the module stand-ins
show_knob = true;
show_mini = true;
show_labels = true; // names over the module stand-ins
explode = 0; // assembly only: lifts the body, with the knob and the front plate, this far off the base
explode_front = 0; // ... and pulls the front plate, with the TFT on it, this far forward
mini_tilt = 0; // assembly only: the mini tilted this many degrees, as it goes into its cradle
mini_back = 0; // ... and pulled back this far

$fn = 48;
eps = 0.01;

// ------------------------------------------------------------ print
clr = 0.3; // fit clearance between printed parts / around boards

// ------------------------------------------------------------ modules
// TFT, portrait, pin header on the bottom edge (upright with FLIP_180 = 1
// in main/display.c, as it is now)
tft_w = 31.22; // across the two ears at the top (the widest part)
tft_h = 47.96; // ear tops to the bottom edge (48.01 the first time). The top edge has a notch
              // between the ears (holes on the ears); the glass's flex cable
              // wraps through it to a connector on the back (photos 2026-09-29)
tft_pcb_t = 1.21; // measured on an ear
tft_front = 2.92 - tft_pcb_t; // PCB front face to the glass front (2.92 glass front to board back)
tft_glass_w = 30.06; // measured, incl. the step at the top; centred left-right (by eye)
tft_glass_top = 4.73; // ear tops to the glass's top edge (measured; bottom gap 4.94 ~ 47.96 - 43.13)
tft_glass_h = 43.13 - tft_glass_top; // ear tops to the glass bottom 43.13 (measured). The
                                     // glass alone measured 37.43: that missed ~1 mm, likely the step
                                     // at the top; this keeps the whole glass stack out
tft_lit_w = 27.97; // pixel area, seller; its position under the glass isn't measured:
tft_lit_h = 32.63; // the window is sized from the glass instead (tft_lip)
tft_lip = 0.5; // the panel covers this much of the glass's edge all round
tft_hole_x = 26.19; // hole centres, measured; centred on the board (ASSUMED)
tft_hole_z = 42.91;
tft_hole_d = 1.79; // measured with the inside jaws, which read low (the OLED's 1.70 was
                   // likely 2.0): likely 2.0, for M2; if an M2 screw won't pass, M1.7
tft_hdr_edge = 2.0; // pin row's centre above the bottom edge (standard header, from the photo)
tft_pins = 8.5; // standard straight header on the back: 2.5 plastic + ~6 pin (not measured)
tft_dupont = 14; // a plugged-in 8-pin Dupont housing adds this
tft_back = 2; // parts on the PCB's back: tiny SMDs, flat cable soldered flat (photo; generous)
tft_notch_w = 18.91; // notch between the ears at the top
tft_notch_d = 5.13; // ear tops to the notch's bottom edge: the glass's top step
                    // (4.73) overhangs it by 0.4
tft_flex_w = 14; // flat cable, wraps round the notch's edge to the back (ASSUMED, photos)
tft_flex_bulge = 0.8; // its bend above the glass's top edge (ASSUMED)
tft_flex_conn = 1.2; // its connector on the back: height (ASSUMED)
tft_flex_conn_l = 7; // and length down from the notch (ASSUMED, photos)

p = 2.54;
// ESP32 mini (D1 mini layout), lying can up, USB at the back. MEASUREMENTS.md
// holds it can towards you, USB at the bottom: its "right" is -X here, the
// RST button's side is +X
mini_w = 31.51;
mini_l = 39.02;
mini_t = 1.49;
mini_can = 4.66; // underside to the top of the metal can
mini_usb_top = 4.61; // underside to the top of the USB socket
mini_usb_w = 8.89;
mini_usb_out = 1.22; // the socket past the board's back edge
mini_usb_off = mini_w / 2 - (11.06 + mini_usb_w / 2); // socket centre off the board's middle, towards -X
mini_usb_l = 7.3; // socket length (ASSUMED: standard part)
mini_antenna = 6; // antenna end, opposite the USB
mini_pin_y0 = 6.83 + 0.5; // antenna edge to the first hole's centre (approximate)
mini_pin_end = mini_pin_y0 + 9 * p + 1; // ... to the last pad's far side
// header rows, from the board's middle: both inner rows and the outer one on
// -X carry every pin in use (standard grid: inner 22.86 apart, outer 27.94;
// measured ~4.63 from the edge to an inner hole's far side, which agrees)
mini_rows = [-4.5 * p, 4.5 * p, -5.5 * p];
mini_hdr = p + 6; // header on the top face: plastic + pin (ASSUMED: standard)
mini_dupont = 14; // a plugged-in Dupont housing, on top of the plastic
mini_joint = 2; // solder joints and pin stubs under the board (ASSUMED)

ky_l = 26.18;
ky_w = 19.29;
ky_t = 1.47;
ky_cap_d = 14.78;
ky_shaft_y = 16 - ky_cap_d / 2; // shaft centre from the board's short edge
                                // away from the pins (desk display, +-1)
ky_cap_h = 31.79 - 15.58; // cap height
ky_cap_above = 15.58 - ky_t; // board front face to the cap's lower edge, pushed on fully
ky_body = 12.5; // EC11 body footprint (ASSUMED, standard part)
ky_body_h = 6.38; // EC11 body height above the board
ky_collar_d = 6.73; // M7 threaded collar round the shaft
ky_collar_h = 6.86; // above the body
ky_shaft_d = 6; // (standard EC11)
ky_shaft_h = 11.83; // past the collar's end
ky_nut_af = 9.87; // M7 nut across the flats
ky_nut_t = 2.24;
ky_washer_t = 0.37;
ky_washer_d = 11; // ASSUMED (not needed)
ky_hdr = p + 6; // header re-soldered straight, under the board at the pin end: plastic + pins (ASSUMED: standard)
ky_dupont = 14; // a plugged-in 5-pin Dupont housing adds this

// ------------------------------------------------------------ case
wall = 2.0;
tilt = 20; // screen panel, back from vertical
skirt_h = 12; // strip under the screen: lifts it off the desk, room for the TFT's plug
panel_margin = 3; // screen panel beyond the TFT board, top and bottom
W = 50;
D = 82; // kept for the cardboard mock-up already built (2026-09-30)
base_t = 3;

panel_len = tft_h + 2 * panel_margin; // along the slope
H = skirt_h + panel_len * cos(tilt);
run = panel_len * sin(tilt); // how far back the panel's top edge is

boss_d = 7;
boss_pilot = 2.5; // M3 x 10 self-tapping, round head (the user's kit; 3.64 x ~11.7 measured)
boss_hole = 4.0; // in the base: the v1 3.4 had to be drilled to 4
boss_cb_d = 6.5; // head counterbore under the base, 2 deep (1 mm of base left)
boss_cb_h = 2;
boss_pilot_h = 13; // from 1 mm under the base's top face
base_screw_l = 11.7; // the silver kit's "M3 x 10", measured
boss_in = wall + boss_d / 2 - 1; // centre from the outside; sunk 1 mm into the walls
boss_front = wall + clr + boss_d / 2; // front pair: behind the base's strip under the screen
boss_xy = [[boss_in, boss_front], [W - boss_in, boss_front], [boss_in, D - boss_in], [W - boss_in, D - boss_in]];

// front plate: drops in between the side walls, flush with their edges,
// onto a rail along each side wall; 4 x 2 x 6 self-tapping pan head
// screws from the front (the small black kit), heads left proud. v1 had
// 2.3 x 8 into 1.9 pilots: too tight, two snapped (2026-10-10)
rail_w = 6; // from the side wall's inner face: 1.4 clear of the TFT board
rail_d = 8; // behind the plate
front_gap = 0.2; // plate's bottom edge above the skirt's edge
strip_drop = 0.8; // the base's strip stops this far below the skirt's edge: 1.0
                  // under the plate. v1 had 0.2 and they touched once the
                  // plate was screwed on: the base wouldn't go fully in (2026-10-10)
front_screw_d = 2.4; // clearance hole in the plate
front_pilot = 1.6; // as the TFT's posts: 2 x 4 went in fine there
front_pilot_depth = 6; // behind the plate: the screw reaches 4
front_screw_x = W / 2 - wall - rail_w / 2 - 0.5; // 0.5 in from the rail's middle: 2 mm of plate to its edge
front_screw_l = 6;
front_screw_v = [4.5, panel_len - 4.5]; // up the slope: outside the window's chamfer

// TFT: glass flat against the panel's inner face, the PCB screwed from
// behind onto 4 posts at its corner holes (4 x M2 x 4 self-tapping: a 6
// would poke out of the front). The
// holes sit outside the glass (seller's drawing), so posts fit there
tft_v = panel_len / 2; // board centre, up the slope from the skirt's edge
tft_post_d = 5.5; // 1.95 of wall round the pilot
tft_glass_gap = 0.3; // posts to the glass
tft_pilot = 1.6;
tft_glass_z0 = tft_h / 2 - tft_glass_top - tft_glass_h; // glass bottom edge, from the board centre
// window: v1's was the glass minus the lip. On the print, with an edge
// test screen (EDGE_TEST in main/main.c), every edge pixel showed, with
// black glass between the window's edge and the pixels: ~0.5 left/right,
// 3.6 at the top (where the flat cable leaves the glass) and 1.25 at the
// bottom (user, 2026-10-10). v2 brings the top and bottom edges to 0.2
// outside the pixels
win_top_in = 3.6 - 0.2; // top edge moved down from v1's
win_bot_in = 1.25 - 0.2; // bottom edge moved up
lit_z = tft_glass_z0 + tft_glass_h / 2 + (win_bot_in - win_top_in) / 2; // window centre above the board centre
win_w = tft_glass_w - 2 * tft_lip;
win_h = tft_glass_h - 2 * tft_lip - win_top_in - win_bot_in;
// the pixel area's two bottom corners are rounded (the top ones aren't):
// with the corner test screen (EDGE_TEST 2) a 40 px arc was just cut,
// 45 px whole (user, 2026-10-10): ~42 px = 4.9 mm at 0.1165 mm/px. The
// window's bottom corners follow, a little outside them
win_r = 5;

// knob on top, centred: KY-040 board flat under the top, shaft end to the
// front, pins to the back. Its threaded collar goes up through a small hole
// and the nut that came with it, on a washer, clamps the encoder body
// against the inside of the top. Two ribs beside the board's pin end stop
// it turning. v1 had snap hooks: one didn't catch (2026-10-07)
knob_x = W / 2;
ky_y0 = 25; // board's front edge
knob_y = ky_y0 + ky_shaft_y;
knob_hole = 7.3; // M7 collar, 6.73 measured: FDM holes come out small
ky_face_z = H - wall - ky_body_h - 0.01; // board front (component) face; 0.01 so the clash check doesn't see the touching faces
ky_back_z = ky_face_z - ky_t;
knob_nut_z = H + ky_washer_t; // nut's underside
knob_cap_z = ky_face_z + ky_cap_above; // cap's lower edge, pushed on fully: 0.87 above the collar
rib_gap = 1; // board edge to rib: the shaft's position across the board is +-1
rib_t = 1.6;
rib_l = 8; // along the board, at its pin end
assert(ky_collar_h - wall - ky_washer_t - ky_nut_t >= 1.5, "not enough thread above the nut");
assert(knob_cap_z > knob_nut_z + ky_nut_t + 1, "cap sits on the nut");

// ESP32 mini flat on the floor at the back, in a cradle on the base: the
// antenna end slides under two lips at the front corners, the back drops
// onto two rests with a stop behind them, and the body's ledge over the
// USB socket holds it down once the base is screwed on. No screws (the
// board has no holes) and nothing that has to flex
mini_lift = 3; // underside above the floor: room for the solder joints
usb_gap = 1.6; // board's back edge to the back wall: the socket's mouth stops 0.4 short of it
mini_gap = 0.2; // cradle walls to the board's edges
mini_wall = 1.3;
mini_lip = [4, 2.5, 1.5]; // lip over each front corner: across, deep, thick
mini_lip_gap = 0.4; // lip to the board's top face: lets the board tilt in
mini_x0 = (W - mini_w) / 2;
mini_x1 = mini_x0 + mini_w;
mini_y1 = D - wall - usb_gap;
mini_y0 = mini_y1 - mini_l;
mini_z = base_t + mini_lift; // board underside
usb_x = W / 2 - mini_usb_off;
usb_z = mini_z + (mini_t + mini_usb_top) / 2;
usb_hole = [13, 8];
usb_ledge_w = 17; // wider than the hole, so it joins the wall beside it
usb_ledge_over = 2.5; // how much of the socket it covers
cable_hole = [10, 6]; // the 4 wires to the toy; a plug goes on inside (ASSUMED: not sized from a real cable)
cable_z = 30; // centre, on the back wall, in the middle (ASSUMED: over the ledge and the mini's plugs)

echo(str("case ", W, " x ", D, " x ", H, " mm (W x D x H)"));
echo(str("usb centre z ", usb_z, ", knob board face z ", ky_face_z));
assert(mini_x0 - mini_gap - mini_wall > wall + clr, "mini cradle hits a side wall");
assert(cable_z - cable_hole[1] / 2 > mini_z + mini_usb_top + 0.2 + 1 + usb_gap - mini_usb_out + usb_ledge_over + 1, "cable hole cuts into the USB ledge");
assert(cable_z + cable_hole[1] / 2 < H - wall - 1, "cable hole cuts into the top");
assert(mini_joint < mini_lift, "solder joints touch the floor");
assert(tft_hole_z / 2 - tft_pilot / 2 - 0.6 > tft_glass_z0 + tft_glass_h + tft_glass_gap, "TFT post flats cut into the screw holes");
assert(tft_hole_x / 2 - tft_post_d / 2 > tft_notch_w / 2, "TFT top posts off the ears");
assert(win_w > tft_lit_w && win_h > tft_lit_h, "window smaller than the pixel area");

// ------------------------------------------------------------ pre-print checks
// What the clash check can't see (the enclosure-preprint-review skill;
// v1's misses: holes 0.2 from an edge, pilots too short for the screw, a
// 0.2 gap that closed). Fails the export.
function edge_ok(hole_d, centre_to_edge) = centre_to_edge - hole_d / 2 >= max(2, 0.6 * hole_d) - 0.01;
// plastic round the holes, to the nearest edge of the part they're in
assert(edge_ok(boss_hole, min(boss_in, boss_front)), "base screw hole too close to the base's edge");
assert(min(boss_in, boss_front) - boss_cb_d / 2 >= 1.2, "base screw counterbore too close to the base's edge");
plate_bottom_v = (front_gap + wall * sin(tilt)) / cos(tilt); // the plate's level-cut bottom edge, inner face, along the slope
assert(edge_ok(front_screw_d, W / 2 - wall - clr - front_screw_x), "front plate screw hole too close to the plate's side");
assert(edge_ok(front_screw_d, front_screw_v[0] - plate_bottom_v), "front plate screw hole too close to the plate's bottom");
assert(front_screw_x - front_pilot / 2 >= W / 2 - wall - rail_w + 1, "front pilot too close to the rail's inner edge");
// pilot depth against the screw's reach: >= 1 mm left at the tip
base_screw_tip = boss_cb_h + base_screw_l; // the head sits at the counterbore's top
assert(base_t - 1 + boss_pilot_h - base_screw_tip >= 1, "base screw bottoms out in its pilot");
assert(front_pilot_depth - (front_screw_l - wall) >= 1, "front plate screw bottoms out in its pilot");
// the TFT's 2 x 4: 0.1 left at the tip, the post can't be deeper (0.8 to the plate's
// front); went in fine on v1, so only checked not to go past it
assert(tft_front + wall - 0.8 - (4 - tft_pcb_t) >= 0, "TFT screw longer than its pilot");
// gaps that aren't meant to touch: print tolerance + screw-hole play
assert(front_gap + strip_drop >= 0.8, "front plate too close to the base's strip");
echo(str("CHECKS: base hole plastic ", min(boss_in, boss_front) - boss_hole / 2, ", counterbore ", min(boss_in, boss_front) - boss_cb_d / 2,
         "; plate hole to side ", W / 2 - wall - clr - front_screw_x - front_screw_d / 2, ", to bottom ", front_screw_v[0] - plate_bottom_v - front_screw_d / 2,
         "; pilot spare: base ", base_t - 1 + boss_pilot_h - base_screw_tip, ", front ", front_pilot_depth - (front_screw_l - wall),
         ", TFT ", tft_front + wall - 0.8 - (4 - tft_pcb_t), "; plate over strip ", front_gap + strip_drop,
         "; thread above the knob nut ", ky_collar_h - wall - ky_washer_t - ky_nut_t));

// ------------------------------------------------------------ helpers
module profile() polygon([[0, 0], [0, skirt_h], [run, H], [D, H], [D, 0]]);

module extrude_x(x0, w) translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(w) children();

module outer() extrude_x(0, W) profile();

// inside of the case, open at the bottom
module cavity() extrude_x(wall, W - 2 * wall) hull() {
  offset(delta = -wall) profile();
  translate([0, -wall - 1]) offset(delta = -wall) profile();
}

// panel coordinates: x across (0 = centre), y inwards (0 = outer face),
// z up the slope (0 = the skirt's top edge)
module panel_frame() translate([W / 2, 0, skirt_h]) rotate([-tilt, 0, 0]) children();

// at the TFT board's centre, on the panel's inner face (= the glass front)
module tft_frame() panel_frame() translate([0, wall, tft_v]) children();

module box(p0, p1) translate(p0) cube(p1 - p0);

module slots(x0, x1, y0, y1, z0, z1, pitch = 5, w = 2) {
  n = floor((x1 - x0 - w) / pitch);
  for (i = [0:n]) box([x0 + i * pitch, y0, z0], [x0 + i * pitch + w, y1, z1]);
}

module stadium(w, h, len) // along y, centred in x/z
  hull() for (s = [-1, 1]) translate([s * (w - h) / 2, 0, 0]) rotate([-90, 0, 0]) cylinder(d = h, h = len);

tft_holes = [for (sx = [-1, 1], sz = [-1, 1]) [sx * tft_hole_x / 2, sz * tft_hole_z / 2]];

// ------------------------------------------------------------ front plate
// posts flattened on the glass side, tft_glass_gap clear of it, so they fit
// whatever the holes' exact position
module tft_mount() tft_frame() difference() {
  for (h = tft_holes) translate([h.x, -0.5, h.y]) rotate([-90, 0, 0]) cylinder(d = tft_post_d, h = tft_front + 0.5);
  g = tft_glass_gap;
  box([-tft_glass_w / 2 - g, -1, tft_glass_z0 - g], [tft_glass_w / 2 + g, tft_front + 1, tft_glass_z0 + tft_glass_h + g]);
}

module tft_pilots() tft_frame() for (h = tft_holes)
  translate([h.x, tft_front + eps, h.y]) rotate([90, 0, 0]) cylinder(d = tft_pilot, h = tft_front + wall - 0.8);

// the glass minus the lip; straight for win_edge next to the glass, then
// chamfered 45 degrees outwards. The straight part keeps the window's
// edge from ending in a knife edge (flagged as a thin wall, 2026-10-02).
// A straight cut right through plus the chamfer's funnel, with no
// eps-thin slabs: with those, hubs.com's checker showed a skin across the
// whole window (the mesh itself had none); this version passes it
win_edge = 1.2;
// a w x h slab across the window, y from y0 to y0 + t, bottom corners rounded
module win_slab(w, h, y0, t, r) hull() {
  for (sx = [-1, 1]) {
    translate([sx * (w / 2 - r), y0, -h / 2 + r]) rotate([-90, 0, 0]) cylinder(r = r, h = t, $fn = 48);
    translate([min(sx * w / 2, sx * (w / 2 - r)), y0, h / 2 - r]) cube([r, t, r]);
  }
}
module tft_window() tft_frame() translate([0, 0, lit_z]) {
  c = wall - win_edge + 1; // chamfer's spread at 1 mm in front of the face
  win_slab(win_w, win_h, -wall - 1, wall + 2, win_r);
  hull() {
    win_slab(win_w, win_h, -win_edge, 0.5, win_r);
    win_slab(win_w + 2 * c, win_h + 2 * c, -wall - 1.5, 0.5, win_r + c);
  }
}

module front_holes(d, depth, from = -1) panel_frame() for (sx = [-1, 1], v = front_screw_v)
  translate([sx * front_screw_x, from, v]) rotate([-90, 0, 0]) cylinder(d = d, h = depth);

module front() difference() {
  union() {
    intersection() {
      outer();
      panel_frame() box([-W / 2 + wall + clr, 0, -5], [W / 2 - wall - clr, wall, panel_len + 5]);
      box([0, -1, skirt_h + front_gap], [W, D, H]);
    }
    tft_mount();
  }
  tft_window();
  tft_pilots();
  front_holes(front_screw_d, wall + 2);
}

// ------------------------------------------------------------ body
// what the front plate and the base's strip take: everything between the
// side walls in front of the plate's inner face, and of the strip below it
// (eps deeper than the plate, so the clash check sees a gap, not a touch)
module front_cut() {
  w = wall + eps;
  extrude_x(wall, W - 2 * wall) polygon([
    [-5, -5], [wall + clr, -5], [wall + clr, skirt_h + (wall + clr - w * cos(tilt)) / tan(tilt) - w * sin(tilt)],
    [w * cos(tilt) + 80 * sin(tilt), skirt_h - w * sin(tilt) + 80 * cos(tilt)], [-5, H + 20]]);
}

// a rail along each side wall, from the floor to the top: the plate lies on
// them (the cut trims their front to the plate's inner face). Full length,
// so nothing overhangs with the body printed upside down
module front_rails() intersection() {
  panel_frame() for (sx = [-1, 1])
    box([min(sx * (W / 2 - wall + 0.5), sx * (W / 2 - wall - rail_w)), wall - 0.5, -30],
        [max(sx * (W / 2 - wall + 0.5), sx * (W / 2 - wall - rail_w)), wall + rail_d, panel_len + 10]);
  box([0, 0, base_t + eps], [W, D, H]);
}

module bosses() for (p = boss_xy) translate([p.x, p.y, base_t + eps]) cylinder(d = boss_d, h = H);

module boss_pilots() for (p = boss_xy) translate([p.x, p.y, base_t - 1]) cylinder(d = boss_pilot, h = boss_pilot_h);

// two ribs beside the board's long edges at its pin end, down past the
// board: the nut holds the encoder, these only stop it turning
module knob_mount() for (sx = [-1, 1]) {
  e = knob_x + sx * (ky_w / 2 + rib_gap);
  box([min(e, e + sx * rib_t), ky_y0 + ky_l - rib_l, ky_back_z - 1.5], [max(e, e + sx * rib_t), ky_y0 + ky_l, H - wall + 0.5]);
}

module vents() {
  // top, left and right of the knob, over the ESP32: its warm air leaves here
  // (the right side mirrors the left, so the slots sit the same from both edges)
  for (xl = [boss_in + 1:4:knob_x - ky_w / 2 - 3.5], x = [xl, W - xl - 2])
    box([x, ky_y0 + 4, H - wall - 1], [x + 2, D - boss_d - 4, H + 1]);
  // back, low: intake, left and right of the USB
  for (xl = [boss_d + 2:4:usb_x - 11], x = [xl, W - xl - 2])
    box([x, D - wall - 1, base_t + 3], [x + 2, D + 1, base_t + 12]);
}

// over the USB socket, on the back wall: holds the mini's back end down.
// Flat underneath, sloped 45 degrees on top (the body prints upside down)
module usb_ledge() {
  z0 = mini_z + mini_usb_top + 0.2;
  reach = usb_gap - mini_usb_out + usb_ledge_over;
  x0 = usb_x - usb_ledge_w / 2;
  x1 = usb_x + usb_ledge_w / 2;
  hull() {
    box([x0, D - wall - reach, z0], [x1, D - wall + 0.5, z0 + 1]);
    box([x0, D - wall - 0.1, z0], [x1, D - wall + 0.5, z0 + 1 + reach]);
  }
}

module body() difference() {
  union() {
    difference() {
      outer();
      cavity();
    }
    intersection() {
      outer();
      union() {
        bosses();
        front_rails();
        knob_mount();
        usb_ledge();
      }
    }
  }
  front_cut();
  front_holes(front_pilot, front_pilot_depth, wall - eps);
  boss_pilots();
  box([-1, -1, -1], [W + 1, D + 1, base_t]); // the walls stand on the base
  vents();
  translate([usb_x, D - wall - eps, usb_z]) stadium(usb_hole[0], usb_hole[1], wall + 2); // not into the ledge
  translate([W / 2, D - wall - eps, cable_z]) stadium(cable_hole[0], cable_hole[1], wall + 2);
  translate([knob_x, knob_y, H - wall - 1]) cylinder(d = knob_hole, h = wall + 2);
}

// ------------------------------------------------------------ base
module mini_cradle() {
  g = mini_gap;
  top = mini_z + mini_t;
  lip_z = top + mini_lip_gap;
  z0 = base_t - eps;
  for (sx = [-1, 1]) {
    xe = sx < 0 ? mini_x0 : mini_x1; // the board's long edge
    xo = xe + sx * (g + mini_wall); // cradle's outside
    yo = mini_y0 - g - mini_wall;
    // front corner: rest, front and side walls, lip
    box([min(xo, xe - sx * 5), yo, z0], [max(xo, xe - sx * 5), mini_y0 + 5, mini_z]);
    box([min(xo, xe - sx * 5), yo, z0], [max(xo, xe - sx * 5), mini_y0 - g, lip_z + mini_lip[2]]);
    box([min(xo, xe + sx * g), yo, z0], [max(xo, xe + sx * g), mini_y0 + 5, lip_z + mini_lip[2]]);
    box([min(xo, xe - sx * mini_lip[0]), yo, lip_z], [max(xo, xe - sx * mini_lip[0]), mini_y0 + mini_lip[1], lip_z + mini_lip[2]]);
    // side guide, beside the pin rows, before the step in the +X edge
    box([min(xo, xe + sx * g), mini_y0 + 24, z0], [max(xo, xe + sx * g), mini_y0 + 30, top]);
    // back: a rest behind the pin rows, clear of the socket's tabs, and a stop behind the edge
    xa = xe - sx * 5;
    xb = xe - sx * 9;
    box([min(xa, xb), mini_y0 + mini_pin_end + 1, z0], [max(xa, xb), mini_y1 + 0.15, mini_z]);
    box([min(xa, xb), mini_y1 + 0.15, z0], [max(xa, xb), D - wall - clr, top]);
  }
}

// the whole footprint, so the screw holes have plastic all round (in v1
// it sat inside the walls and the holes were 0.2 from its edge)
module base() {
  x0 = wall + clr;
  box([x0, 0, 0], [W - x0, wall, skirt_h - strip_drop]); // the strip under the screen, between the side walls
  difference() {
    box([0, 0, 0], [W, D, base_t]);
    for (p = boss_xy) translate([p.x, p.y, -1]) {
      cylinder(d = boss_hole, h = base_t + 2);
      cylinder(d = boss_cb_d, h = 1 + boss_cb_h); // head counterbore
    }
    slots(12, W - 12, mini_y0 + 8, mini_y0 + 30, -1, base_t + 1); // intake under the ESP32
  }
  mini_cradle();
}

// ------------------------------------------------------------ stand-ins
module tft_standin(plug = true) tft_frame() {
  color("black") box([-tft_glass_w / 2, eps, tft_glass_z0], [tft_glass_w / 2, tft_front, tft_glass_z0 + tft_glass_h]);
  color("steelblue") difference() {
    box([-tft_w / 2, tft_front + eps, -tft_h / 2], [tft_w / 2, tft_front + tft_pcb_t, tft_h / 2]);
    box([-tft_notch_w / 2, 0, tft_h / 2 - tft_notch_d], [tft_notch_w / 2, 10, tft_h / 2 + 1]);
    for (h = tft_holes) translate([h.x, 0, h.y]) rotate([-90, 0, 0]) cylinder(d = tft_hole_d, h = 10);
  }
  // flat cable: out of the glass's top step, round the notch's edge, to its connector on the back
  nz = tft_h / 2 - tft_notch_d;
  fz = tft_h / 2 - tft_glass_top + tft_flex_bulge; // top of the bend
  back = tft_front + tft_pcb_t;
  color("orange") {
    box([-tft_flex_w / 2, 0.6, tft_glass_z0 + tft_glass_h - 0.5], [tft_flex_w / 2, tft_front, fz]);
    box([-tft_flex_w / 2, 0.6, nz + eps], [tft_flex_w / 2, back + 0.3, fz]);
    box([-tft_flex_w / 2 - 0.5, back + eps, nz - tft_flex_conn_l], [tft_flex_w / 2 + 0.5, back + tft_flex_conn, nz + 1]);
  }
  color("steelblue") box([-11, tft_front + tft_pcb_t, -tft_h / 2 + tft_hdr_edge + 3], [11, tft_front + tft_pcb_t + tft_back, 15]);
  hz = -tft_h / 2 + tft_hdr_edge;
  color("dimgray") box([-4 * p, tft_front + tft_pcb_t, hz - p / 2], [4 * p, tft_front + tft_pcb_t + tft_pins, hz + p / 2]);
  if (plug) color("gold", 0.35) box([-4 * p - 0.6, tft_front + tft_pcb_t + tft_pins, hz - 1.6], [4 * p + 0.6, tft_front + tft_pcb_t + tft_pins + tft_dupont, hz + 1.6]);
}

module mini_standin(plug = true) translate([mini_x0, mini_y0, mini_z]) {
  ux = mini_w / 2 - mini_usb_off;
  color("royalblue") box([0, 0, eps], [mini_w, mini_l, mini_t]);
  color("silver") box([mini_w / 2 - 9, mini_antenna, mini_t], [mini_w / 2 + 9, 25.5, mini_can]);
  color("silver") box([ux - mini_usb_w / 2, mini_l + mini_usb_out - mini_usb_l, mini_t], [ux + mini_usb_w / 2, mini_l + mini_usb_out, mini_usb_top]);
  color("orange", 0.5) box([mini_w / 2 - 9, 0, mini_t], [mini_w / 2 + 9, mini_antenna, mini_t + 1]); // antenna: keep clear
  for (r = mini_rows) {
    x = mini_w / 2 + r;
    y0 = mini_pin_y0 - p / 2;
    y1 = mini_pin_y0 + 9.5 * p;
    color("dimgray") box([x - p / 2, y0, mini_t], [x + p / 2, y1, mini_t + mini_hdr]);
    color("dimgray") box([x - 0.9, y0, -mini_joint], [x + 0.9, y1, 0]); // solder joints
    if (plug) color("gold", 0.35) box([x - p / 2, y0, mini_t + p], [x + p / 2, y1, mini_t + p + mini_dupont]);
  }
}

module knob_standin(plug = true) {
  color("firebrick") {
    box([knob_x - ky_w / 2, ky_y0, ky_back_z], [knob_x + ky_w / 2, ky_y0 + ky_l, ky_face_z]);
    translate([knob_x, knob_y, ky_face_z + ky_body_h / 2]) cube([ky_body, ky_body, ky_body_h], center = true);
  }
  bt = ky_face_z + ky_body_h;
  color("silver") translate([knob_x, knob_y, bt]) {
    cylinder(d = ky_collar_d, h = ky_collar_h);
    cylinder(d = ky_shaft_d, h = ky_collar_h + ky_shaft_h);
  }
  color("dimgray") translate([knob_x, knob_y, H + 0.01]) {
    cylinder(d = ky_washer_d, h = ky_washer_t);
    translate([0, 0, ky_washer_t]) cylinder(d = ky_nut_af / cos(30), h = ky_nut_t, $fn = 6);
  }
  // header straight down under the board's pin end, plug below it
  hy = ky_y0 + ky_l - p / 2;
  color("dimgray") box([knob_x - 2.5 * p, hy - p / 2, ky_back_z - ky_hdr], [knob_x + 2.5 * p, hy + p / 2, ky_back_z]);
  if (plug) color("gold", 0.35) box([knob_x - 2.5 * p - 0.6, hy - p / 2 - 0.6, ky_back_z - ky_hdr - ky_dupont], [knob_x + 2.5 * p + 0.6, hy + p / 2 + 0.6, ky_back_z - ky_hdr + p]);
  color("gray") translate([knob_x, knob_y, knob_cap_z]) cylinder(d = ky_cap_d, h = ky_cap_h);
}

module stand_ins() {
  tft_standin();
  mini_standin();
  knob_standin();
}

// a name on a stick from point p, the text at dz above the case top,
// facing the front
module label(txt, p, dz) color("black") translate(p) {
  cylinder(d = 0.4, h = H + dz - p.z, $fn = 6);
  translate([0, 0, H + dz - p.z + 0.5]) rotate([90, 0, 0]) linear_extrude(0.4) text(txt, size = 2.5, halign = "center");
}

// panel coordinates (x, inwards, up the slope) to world
function panel_pt(x, y, z) = [W / 2 + x, y * cos(tilt) + z * sin(tilt), skirt_h + z * cos(tilt) - y * sin(tilt)];

module labels() {
  label("TFT", panel_pt(0, wall + tft_front + tft_pcb_t + tft_back, tft_v + 12), 12);
  label("ESP32 mini", [W / 2, mini_y0 + 16, mini_z + mini_can], 3);
  label("antenna", [W / 2, mini_y0 + 3, mini_z + mini_t + 1], 16);
  label("USB-C", [usb_x, mini_y1, mini_z + mini_usb_top], 20);
  label("KY-040", [knob_x, knob_y, knob_cap_z + ky_cap_h], 18);
}

// ------------------------------------------------------------ output
// test print: the front of the top, with the knob mount
module test_knob() intersection() {
  body();
  box([-1, -1, H - 14], [W + 1, ky_y0 + ky_l + 3, H + 1]);
}

// assembly only: the printed parts cut away left of x = cut
module cutaway() intersection() {
  children();
  if (cut >= 0) box([cut, -10, -10], [W + 10, D + 10, H + 10]);
  else box([-10, -10, -10], [W + 10, D + 10, H + 10]);
}

if (part == "body") translate([0, D, H]) rotate([180, 0, 0]) body();
else if (part == "test_knob") translate([0, D, H]) rotate([180, 0, 0]) test_knob();
else if (part == "front") rotate([90, 0, 0]) rotate([tilt, 0, 0]) translate([-W / 2, 0, -skirt_h]) front(); // face down
else if (part == "base") base();
else if (part == "dims") echo(W = W, D = D, H = H, wall = wall, tilt = tilt, skirt_h = skirt_h, run = run,
  panel_len = panel_len, tft_v = tft_v, tft_w = tft_w, tft_h = tft_h, lit_z = lit_z, win_w = win_w, win_h = win_h,
  knob_x = knob_x, knob_y = knob_y, knob_hole = knob_hole, ky_y0 = ky_y0, ky_l = ky_l, ky_w = ky_w,
  usb_x = usb_x, usb_z = usb_z, usb_w = usb_hole[0], usb_h = usb_hole[1], boss_in = boss_in, boss_d = boss_d,
  mini_x0 = mini_x0, mini_y0 = mini_y0, mini_w = mini_w, mini_l = mini_l); // for cardboard.py
else if (part == "clash") {
  // printed parts against every stand-in
  intersection() {
    union() {
      body();
      front();
      base();
    }
    stand_ins();
  }
  // printed parts against each other
  intersection() {
    body();
    union() {
      front();
      base();
    }
  }
  intersection() {
    front();
    base();
  }
  // stand-ins against each other (plugs included)
  intersection() {
    tft_standin();
    union() {
      mini_standin();
      knob_standin();
    }
  }
  intersection() {
    knob_standin();
    mini_standin();
  }
}
else {
  translate([0, 0, explode]) {
    if (show_body) cutaway() color("white") body();
    translate([0, -explode_front, 0]) {
      if (show_front) cutaway() color("gainsboro") front();
      if (show_tft) tft_standin();
    }
    if (show_knob) knob_standin();
    if (show_labels) labels();
  }
  if (show_base) cutaway() color("khaki") base();
  if (show_mini) translate([0, mini_y0 + mini_back, mini_z]) rotate([mini_tilt, 0, 0]) translate([0, -mini_y0, -mini_z]) mini_standin(plug = mini_tilt == 0);
}
