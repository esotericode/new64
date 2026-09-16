# slot/

Drop movement `.c` files here. Any file whose basename matches one of the
reference implementations in `sm64/src/game/` replaces it at build time — no
changes to `CMakeLists.txt` needed.

Overridable: `mario.c`, `mario_step.c`, `mario_actions_stationary.c`,
`mario_actions_moving.c`, `mario_actions_airborne.c`,
`mario_actions_automatic.c`, `mario_actions_submerged.c`,
`mario_actions_cutscene.c`, `mario_actions_object.c`.

See `../docs/SLOTTING_IN_DECOMP.md` for what the ABI covers and what to expect.

`*.c` here is `.gitignore`d, so whatever you put in this directory stays local
unless you deliberately commit it.
