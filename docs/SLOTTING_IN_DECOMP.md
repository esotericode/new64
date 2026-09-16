# Slotting in decompiled movement code

This is the feature the engine was built around: take a movement source file
from a decompilation project, drop it in, and have it run against this engine
with no edits to the file.

## How to do it

Put `.c` files in `slot/`. On the next configure, any file whose basename
matches a reference implementation replaces it:

```sh
cp path/to/decomp/src/game/mario_actions_airborne.c slot/
cmake -S . -B build && cmake --build build -j
# -- new64: slot/ overrides mario_actions_airborne.c
```

Remove the file to go back to the reference implementation. You can slot one
file or all of them, and mix slotted and reference files freely — they all
compile against the same headers and call the same substrate.

## What the ABI provides

Include paths are set up so the `#include` lines in those files resolve as-is:

| Header | Contents |
| --- | --- |
| `sm64.h` | every `ACT_*`, `INPUT_*`, `MARIO_*`, `PARTICLE_*`, button and terrain constant |
| `types.h` | `MarioState`, `Object`, `Controller`, `Surface`, `Area`, `Camera`, `GraphNodeObject`, `AnimInfo`, … |
| `mario.h` | action transitions, floor queries, animation and sound entry points |
| `mario_step.h` | `perform_air_step`, `perform_ground_step`, `perform_hanging_step`, `apply_gravity`, step result codes |
| `engine/math_util.h` | `sins`, `coss`, `atan2s`, `approach_*`, vector helpers |
| `engine/surface_collision.h` | `find_floor`, `find_ceil`, `find_wall_collisions`, `find_water_level` |
| `audio/external.h`, `audio_defines.h` | `play_sound` and the cue ids |
| `camera.h`, `area.h`, `interaction.h`, `level_update.h`, `save_file.h`, `rumble_init.h`, `object_fields.h`, … | the rest of the surface, stubbed where behaviourless |
| `PR/ultratypes.h` | `s8`…`u64`, `f32`, `f64` |

`struct MarioState`'s field names and order match what such code expects. Exact
byte offsets are not preserved and do not need to be: new64 compiles the hosted
sources from source, so the compiler agrees with itself. Pointers are host-width.

## What to expect, file by file

**`mario_actions_airborne.c`, `mario_actions_moving.c`, `mario_actions_stationary.c`**
— the best case. These are almost purely state machine over the substrate.
Expect them to compile with at most a few missing `ACT_*` or `MARIO_ANIM_*`
identifiers, which are one-line additions to `sm64.h` /
`mario_animation_ids.h`.

**`mario_step.c`** — will compile and run. Slotting this one is a genuine
fidelity test: if the engine's collision substrate is right, replacing new64's
step functions with a decomp's should produce identical traces. If it doesn't,
the disagreement is in `engine/src/m64_surface.c`, and the trace tells you which
frame to look at.

**`mario.c`** — compiles, but pulls in the animation system. See the caveat
below.

**`mario_actions_automatic.c`** — compiles; pole and tree climbing will link
against object stubs and do nothing useful, since the demo level has no poles.

**`mario_actions_submerged.c`** — compiles and `perform_water_step()` is real,
but the demo level has no water, so nothing exercises it. Add a water plane with
`m64_set_water_level()` to test.

**`mario_actions_object.c`, `mario_actions_cutscene.c`** — will compile but have
little to act on: there are no objects and no cutscene system.

**`surface_load.c`** — *not* a slot-in candidate, and not needed. It parses N64
level data; new64 builds geometry through `m64_level_*` instead. The partition
and query behaviour it feeds is already reproduced.

## The one real gap: animation frame counts

new64 ships no animation data, and this is the single place where "compiles and
runs" falls short of "behaves identically".

Several original actions end when their animation ends —
`is_anim_at_end()`, `is_anim_past_end()`. Animation *length* is therefore
physics, not decoration. new64 has the animation machinery (`set_mario_animation`,
`animFrame`, `animAccel`, the end predicates all work) but its frame counts are
placeholder defaults from `assets/mario_anims.txt`.

Consequences:

- The engine's own reference actions mostly avoid the issue by using explicit
  timers where duration matters, so the placeholder table cannot make them wrong.
- Slotted-in code that *does* branch on animation end will run, but those states
  will last the wrong number of frames.

To fix it, fill in `assets/mario_anims.txt` with real per-animation frame
counts (one `NAME FRAMES` pair per line) and load it with
`m64_anim_load_table()` before `init_mario()`. Only the counts are needed —
no skeletal data, since the character is a capsule.

## Behavioural stubs worth knowing about

These return fixed answers, and where the answer influences movement it is noted:

| Stub | Answers | Movement consequence |
| --- | --- | --- |
| `save_file_get_flags()` | `0` | no caps owned, so wing-cap gravity and metal-cap handling never engage |
| `mario_check_object_grab()`, `able_to_grab_object()` | `FALSE` | grab branches out of walking/diving are never taken |
| `check_horizontal_wind()` | `FALSE` | air movement stays on its normal path |
| `should_get_stuck_in_ground()` | `FALSE` | the demo declares no snow/sand terrain |
| `mario_update_moving_sand()`, `mario_update_windy_ground()` | `FALSE` | no conveyor surfaces in the demo level |
| `level_trigger_warp()` | no-op | death planes do not warp; the app handles respawn |
| `set_camera_mode()` and the shake functions | no-op | movement only ever reads camera *yaw*, never mode |
| `play_sound()` | records the cue | see below |

The sound stub is worth calling out as a feature rather than a limitation: cues
are logged per frame and printed in the trace, so you get a free, very legible
record of what the state machine believed happened, on which frame.

## Verifying a slotted file

The trace is the tool. Run the same script before and after slotting and diff:

```sh
./build/new64_headless --script jumpchain --no-gif --no-png --trace before.txt
cp .../mario_actions_airborne.c slot/ && cmake --build build -j
./build/new64_headless --script jumpchain --no-gif --no-png --trace after.txt
diff before.txt after.txt
```

Input is deterministic and the engine has no randomness, so any difference is a
genuine behavioural difference. The first differing frame is where to look.

Also run `./build/new64_tests` — it asserts the movement constants directly, so a
slotted file that changes a jump height or a threshold will say so in plain
terms rather than as a vague "feels off".

## Legal note

new64 contains no third-party game code and the build downloads none. What it
provides is an interface — names, signatures, and the numeric constants of the
movement model — because that is what interoperating requires.

Bringing decompiled sources into `slot/` is a separate act with its own
licensing and legal considerations, and it is your decision to evaluate. `slot/`
is `.gitignore`d for that reason: what you put there stays local unless you
deliberately commit it.
