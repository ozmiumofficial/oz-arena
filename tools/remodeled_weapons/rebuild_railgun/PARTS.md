# Railgun rebuild: part inventory

Measured on Tripo's railgun (study.py renders and sections, gridcrop.py crops) in the work frame: x along the gun,
muzzle at x -0.50, butt at x 0.50; y the gun's right (the left side faces -y); z up, 0 at the grip's foot, 0.395 at
the sight. Tripo's units (railgun.json scale 30.5 makes it 30.5 game units long).

Detail levels: **P** primary silhouette, **S** secondary (modeled: plates, pockets, bolts, lips, collars, fillets),
**T** tertiary (the high-detail copy, baked into the normal map: seams, ring grooves, knurl, stipple, hex skin,
carbon weave). Materials: `paint` (the dark charcoal body), `gunmetal`, `steel` (bright accents), `rubber`,
`carbon`, `glass_cell` (the red cells: rail color, glowing), `trim` (the red light strips: rail color, pulsing),
`vent_glow` (behind the grills: rail color, glowing).

## 1. Emitter head (x -0.50 .. -0.29), ~7k tris

- Function: the muzzle end. Holds the emitter nozzle, cools it (side vents), protects it (face pad).
- Real build: a cast two-piece housing (front bulb + rear neck) bolted together, a face plate screwed on the front,
  vent inserts in the flanks, a skid plate underneath.
- P: bell cross-section (a domed neck y +-0.060 over z 0.29..0.377, flaring to a bulb y +-0.105 at z 0.18..0.235,
  rounded bottom at z 0.133); the bulb is x -0.495..-0.37, the neck runs on to -0.30. Behind the bulb the section
  narrows downward (y +-0.067 at the top, +-0.040 at the bottom, x -0.34). Every edge rounded (`rim=`), the flanks
  lean in toward the bottom.
- Face (x -0.50): an outer lip following the bell, a trim channel inside it with the red light strip, a recessed
  field; in the field's neck the emitter (nozzle: a collar r 0.025 at z 0.320, a stepped tip r 0.013, the bore),
  below it the raised carbon pad (rounded bell, chamfered edge, 4 screws).
- Flanks: a framed vent panel (x -0.46..-0.37, z 0.15..0.29) with 4 slots (rounded, with a glow behind), a trim
  strip along its frame's front and bottom; a lower badge box; panel seams and bolts.
- Rear neck flank: the sloped armor plate edge (from x -0.42 z 0.375 down to -0.35 z 0.31, then level) with its
  trim strip; a small status panel with three light bars (trim) at x -0.36..-0.30 z 0.15..0.18.
- Under: two skids (y +-0.035..0.070, z 0.12..0.15) under the bulb; bolts.
- T: hex skin on the vent panel, panel seams, screw slots.
- Joints: the face plate sits in the housing's lip; the upper barrel housing slides into the neck (a collar); the
  lower cell's front cap sits in a socket in the neck's back face; the guide rod enters a boss under it.

## 2. Upper barrel housing and the two top cells (x -0.45 .. -0.11, z 0.28 .. 0.385), ~5k tris

- Function: the rail channel; the two capacitor cells ride on top of it, visible through windows.
- Real build: an extruded housing (flat floor, vertical walls, domed roof) with two windows machined into the roof,
  three clamp rings (round-profiled bands about 3 mm proud: at the head's end, between the windows, at the
  receiver's front), arch-shaped frames round each window, glass cells seated in the windows.
- P: section y +-0.067, z 0.281..0.382 (flat bottom, domed top), small step on the walls at z 0.29..0.30. Windows
  x -0.290..-0.222 and -0.188..-0.120 cut through the roof and walls down to z ~0.33; hoops x -0.333..-0.296,
  -0.220..-0.190, -0.120..-0.090 stand ~3 mm proud of the roof, rounded.
- S: cell glass (red cylinders along x, seated in the windows with end rings), the hoops' face grooves, the wall
  step, a raised armor strip on the neck; bolts at the hoops' feet.
- T: ring grooves on the hoops, seams along the walls.
- Joints: front end inside the emitter neck's collar; rear end butts into the receiver's front face (a lip).

## 3. Lower cell and guide rod (x -0.29 .. -0.10), ~2.5k tris

- Function: the main energy cell (glowing red glass) and the rod that ties the head to the receiver.
- Real build: a glass tube between two metal end caps (with clamp rings), each cap seated in a socket; the rod a
  steel tube with collars.
- P: cell axis y 0, z 0.228, r 0.046; the rod r 0.0092 at z 0.1614, x -0.295..-0.090, collars (r 0.013) at
  x -0.283..-0.261 and -0.141..-0.1085 (profile.py sections).
- S: end caps with a stepped profile and ring grooves, rod collars and a boss where it enters the receiver.
- T: knurl on the caps' grip rings.

## 4. Receiver (x -0.12 .. 0.205, z 0.115 .. 0.385), ~7k tris

- Function: the core; carries the vent window, the trigger housing, the grip, the stock and the sight.
- Real build: a box frame (upper) with an angular lower shell; the side panels are separate armor plates screwed
  on, the vent window a grill insert, a pivot boss at the back.
- P: upper box y +-0.069, z 0.265..0.384, domed roof (like the housing's), front face at x -0.12, roof slopes down
  from x 0.10 to 0.155 and the back face leans back down to x 0.19 z 0.30; lower shell y +-0.065 from x -0.10 to
  0.205, its front edge curving from (-0.10, 0.27) to (-0.075, 0.12), its bottom rising from (-0.07, 0.115) to
  (0.05, 0.145) and on to the grip, its back a rounded corner at (0.205, 0.24). Flanks lean in toward the bottom.
- S: the angled side armor plate (outlined by the trim strip: down the front, along the bottom, diagonally up to
  the back); the vent window x -0.0605..0.089 z 0.2135..0.260 (recessed to y 0.050, 5 uprights and 2 rails, glow
  behind); a raised latch x 0.05..0.12 z 0.345..0.365; the upper panel's chamfered frame; the pivot boss at
  (0.155, 0.226) (stepped rings); the trigger well (a slot with a lip) under the receiver at x 0.06..0.10; a keel
  plate under the front; rear ears (y +-0.05..0.06, x 0.15..0.20, z 0.29..0.355) clamping the stock; bolts.
- T: panel seams, screw slots, fine vent slots, the lettering area left plain.
- Joints: the housing and the lower cell seat into its front face; the grip seats into a socket under the back
  (a collar, no gap); the stock tube passes between the ears into a socket in the back face.

## 5. Top sight (x -0.008 .. 0.047, z 0.380 .. 0.395), ~0.3k tris

- A base pad x -0.008..0.047 and a block x 0.0..0.039, y +-0.017 with rounded top edges, a notch (rear sight) and a front post groove; seated on a
  base pad on the roof.

## 6. Pistol grip (x 0.10 .. 0.22, z 0.0 .. 0.17), ~2.5k tris

- Function: the hand. A right hand: palm on the back, fingers wrap the front, thumb over the left flank.
- Real build: a moulded rubber-over-polymer grip, lofted oval sections (fuller at the palm), finger grooves on the
  front (3 bumps), palm swell low on the back, a beavertail at the top back, a flared pommel (base plate) with a
  screw; a red trim strip down the back flank.
- P: front edge (0.10, 0.115) .. (0.12, 0.09) .. (0.135, 0.04) .. (0.15, 0.0); back edge (0.17, 0.17) .. (0.19,
  0.11) .. (0.218, 0.05); width y +-0.025 (palm side fuller); leans back about 20 degrees.
- S: finger grooves, beavertail, pommel plate with its screw, the trim channel.
- T: stipple on the side panels, a ribbed back strap.
- Joints: the top sits in the receiver's grip socket (a collar), the pommel plate is a separate cap.

## 7. Trigger (x 0.06 .. 0.10, z 0.105 .. 0.155), ~0.4k tris

- A curved blade swept from its pivot in the trigger well, thick at the pivot, a wider shoe where the finger pulls,
  rounded edges. The reference has no guard; none added.

## 8. Stock (x 0.19 .. 0.50), ~4k tris

- Tube: a rounded-rectangle tube (y +-0.026, z 0.283..0.358) from the receiver's back face to the butt, with a
  collar where it leaves the receiver (x 0.19..0.215), a step at x 0.27 (outer sleeve over inner tube: telescoping),
  a trim strip on the sleeve's flank, grooves along its sides (z 0.335).
- Butt: a raked block (front edge (0.40, 0.385)..(0.43, 0.155), back (0.47, 0.385)..(0.50, 0.17)), y +-0.045 at
  the top narrowing toward the bottom, a cheek-rest cap on top (z 0.325..0.352 at x 0.48), a rubber recoil pad on
  the back face (raised, ribbed in T), a trim strip down its front edge; the tube enters it through a collar.

## Triangle budget

Head 7k, housing and top cells 5k, lower cell and rod 2.5k, receiver 7k, sight 0.3k, grip 2.5k, trigger 0.4k,
stock 4k, bolts and small parts 2k: about 31k (limit 40k).

## Masks (the rail color)

`trim` (strips) -> `_tint` + `_trim` (pulse); `glass_cell` -> `_tint` + `_cell` (breathing glow);
`vent_glow` -> `_tint` + `_cell`. Stored gray at their brightness in the base color, as the shipped railgun's.
