# Architecture

## The split

new64 is two layers with a deliberate seam between them.

**`engine/`** is the substrate. Math, collision, level geometry, camera,
software renderer, image output. It knows nothing about a player state machine —
there is no `MarioState` in it, no actions, no jumps. Everything is namespaced
`m64_*`. You could build a different game on it.

**`sm64/`** is the hosted layer: the movement state machine, plus the vocabulary
and object model it expects. This is the part that decompilation sources
replace. It talks to the engine through thin forwarding headers
(`sm64/src/engine/math_util.h`, `sm64/src/engine/surface_collision.h`) that map
the names hosted code uses onto the engine's implementations.

The forwarding is one-line `static inline` functions, so it costs nothing at
runtime. It exists to guarantee that hosted code and engine code do the
arithmetic *identically* rather than through two similar-looking copies that
drift apart.

## How a frame runs

```
m64_world_step()
  ├── m64_controller_update()      raw stick -> dead zone, magnitude clamp, edges
  ├── m64_surface_clear_dynamic()  rebuild moving-platform surfaces
  ├── execute_mario_action()
  │     ├── mario_reset_bodystate()
  │     ├── update_mario_inputs()  buttons + stick + world under the player
  │     │                          -> the INPUT_* digest
  │     └── loop: mario_execute_<group>_action()
  │             an action returns TRUE to mean "I changed state, run again this
  │             same frame". One tick can traverse several states.
  └── m64_camera_update()          camera follows; its yaw feeds NEXT frame's input
```

Two things in that diagram are easy to miss and both matter:

**The dispatch loop.** Actions are re-entered within a single frame whenever one
transitions. Landing from a jump can go `ACT_JUMP` → `ACT_JUMP_LAND` →
`ACT_WALKING` before the frame ends. Action code is written assuming transitions
are free. It also means an action can observe the *same* input edge that caused
it to be entered — which is a real hazard, and one that bit this implementation
during development (see the comment in `act_crouch_slide`).

**The camera is an input to the physics.** Stick direction is interpreted
relative to camera yaw, so the camera is not a presentation concern. Running the
camera *after* movement means next frame's stick reading is resolved against
where the player actually ended up.

## Ownership of position

Action code sets a velocity and calls a step function. It must never write
`m->pos`. The step functions own position, the floor/ceiling/wall pointers, and
the copy of the transform used for rendering. Keeping that ownership in one
place is what stops collision state and render state from disagreeing.

## The fidelity quirks

These are behaviours that a from-scratch engine would not produce by accident.
They are reproduced on purpose, and each is annotated at its implementation
site. Collectively they are why this is more than "a character controller with
similar numbers".

**1. Collision queries truncate to `s16`.**
`find_floor`, `find_ceil` and `find_wall_collisions` all cast the query position
to `s16` before any triangle test. Movement therefore resolves on an integer
lattice: two positions within the same unit cell get the same floor at the same
height. A great deal of frame-perfect SM64 behaviour follows from this one cast.

**2. Floors resolve by list order, not by proximity.**
Cell lists are sorted by the *first vertex's* Y — not by the triangle's height
at the query point — and the scan takes the first containing triangle and stops.
For overlapping sloped floors this can order them against the intuitive answer.

**3. The ±78 unit buffers.**
A floor up to 78 units above the query point still counts (this is the step-up
allowance); a ceiling up to 78 below still counts.

**4. Wall pushout accumulates against the original position.**
Inside one query, each wall is tested against the position the query *started*
with, while the correction accumulates separately. In a corner both walls push
by their full overlap, so deep corners eject further than either wall alone.

**5. Walls are tested in a projected 2D plane.**
A wall triangle is projected onto either the XY or ZY plane depending on whether
its normal leans more along X or Z (threshold 0.707). Seams between two walls
that disagree are the origin of SM64's wall-clip behaviour.

**6. Partition binning has a 50-unit slack — which is only just enough.**
Triangles are registered in cells their bounding box touches, plus 50 units of
slack at cell borders, because queries are a point while wall checks have a
radius. The slack *equals* the largest wall radius, so walls sitting right on a
cell boundary can still be missed. Widening it would silently "fix" wall clips
that hosted code may depend on, so it is left alone.

**7. Quarter steps.**
Motion is applied in four sub-steps, each doing a full collision query, with
different early-exit conditions for ground and air. This is observable, not an
accuracy optimisation: a frame that would tunnel a thin platform at full
velocity instead lands on it.

**8. Angles are a 4096-entry table.**
`sins`/`coss` index a table with the top 12 bits of an `s16` angle, discarding
the low 4. Code that accumulates small yaw deltas quantises in a specific,
reproducible way. An implementation calling `sinf()` would drift.

**9. Floating point contraction is disabled.**
The build passes `-ffp-contract=off`. Fused multiply-add carries extra
intermediate precision and changes results in the last bits.

## Where the movement constants live

Concentrated deliberately, so "how high is a double jump" has one answer:

| What | Where |
| --- | --- |
| every jump's launch velocity | `set_mario_action_airborne()` in `mario.c` |
| gravity, terminal velocity, variable jump height | `apply_gravity()` in `mario_step.c` |
| ground acceleration, speed caps, turn rate | `update_walking_speed()` in `mario_actions_moving.c` |
| slope acceleration per surface class | `apply_slope_accel()` in `mario_actions_moving.c` |
| slope/steep/slippery thresholds | `mario_floor_is_*()` in `mario.c` |
| air control and drag | `update_air_without_turn()` in `mario_actions_airborne.c` |
| the jump chain | the `LandingAction` tables in `mario_actions_moving.c` |
| the wall kick window | `act_air_hit_wall()` in `mario_actions_airborne.c` |

## Frame rate

The movement model is frame-rate dependent by design — gravity is "−4 per
frame", not "−120 per second". Everything assumes 30 Hz (`FRAME_RATE` in
`sm64.h`). Running the state machine at another rate changes jump heights and
speeds. The X11 front-end paces itself to a fixed 30 Hz for this reason; it does
not scale by delta time, and it should not.
