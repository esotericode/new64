# new64 -- a custom engine reproducing Super Mario 64's movement model, built so
# that movement code from a decompilation project can be dropped in unmodified.
#
# Layout:
#   engine/  the substrate: math, collision, level, camera, renderer, image I/O.
#            Knows nothing about the player state machine.
#   sm64/    the hosted layer: the movement state machine and the ABI it expects.
#            This is the part that decomp sources replace.
#   slot/    drop decomp .c files here; they override sm64/src/game/ by basename.
#   app/     front-ends (headless runner, X11 window) and the demo level host.
cmake_minimum_required(VERSION 3.13)
project(new64 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(NEW64_X11 "Build the interactive X11 front-end (non-Windows)" ON)
option(NEW64_WERROR "Treat warnings as errors" OFF)

# On Windows, link the runtime statically so the executables have no DLL
# dependencies beyond what ships with the OS. Without this they would need
# libgcc_s_seh-1.dll and libwinpthread-1.dll alongside them, which defeats the
# point of handing someone a single file.
if(MINGW)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc")
endif()

add_compile_options(-Wall -Wextra)
if(NEW64_WERROR)
  add_compile_options(-Werror)
endif()

# -ffp-contract=off keeps the compiler from fusing multiply-adds.  Fused
# operations carry extra intermediate precision, which changes results in the
# last bits -- and this engine's whole point is bit-level reproducibility of a
# movement model, so the contraction is disabled deliberately.
add_compile_options(-ffp-contract=off)

# No external dependencies: deflate, PNG and GIF are all implemented in-tree
# (engine/src/m64_deflate.c). This is what lets the Windows executables ship as
# single files and lets the project build with any C compiler.
set(ENGINE_SOURCES
  engine/src/m64_math.c
  engine/src/m64_sintable.c
  engine/src/m64_surface.c
  engine/src/m64_level.c
  engine/src/m64_camera.c
  engine/src/m64_input.c
  engine/src/m64_render.c
  engine/src/m64_font.c
  engine/src/m64_png.c
  engine/src/m64_gif.c
  engine/src/m64_deflate.c
)

# The hosted movement code.  Anything with the same basename in slot/ replaces
# the file here -- see the override logic below.
set(SM64_REFERENCE_SOURCES
  sm64/src/game/mario.c
  sm64/src/game/mario_step.c
  sm64/src/game/mario_actions_stationary.c
  sm64/src/game/mario_actions_moving.c
  sm64/src/game/mario_actions_airborne.c
  sm64/src/game/mario_actions_automatic.c
  sm64/src/game/mario_actions_submerged.c
  sm64/src/game/mario_actions_cutscene.c
  sm64/src/game/mario_actions_object.c
)

set(SM64_SUPPORT_SOURCES
  sm64/src/shims/audio_stub.c
  sm64/src/shims/game_stubs.c
)

# --- The slot ---------------------------------------------------------------
# Any .c file placed in slot/ is compiled instead of the reference file with the
# same basename.  This is the mechanism that lets a decompilation's mario.c or
# mario_actions_airborne.c take over with no edits to this build file: the ABI
# they compile against is sm64/include and sm64/src, which is what the reference
# files use too.
file(GLOB SLOT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/slot/*.c")

set(SM64_SOURCES "")
foreach(ref ${SM64_REFERENCE_SOURCES})
  get_filename_component(ref_name "${ref}" NAME)
  set(overridden FALSE)
  foreach(slot ${SLOT_SOURCES})
    get_filename_component(slot_name "${slot}" NAME)
    if(ref_name STREQUAL slot_name)
      set(overridden TRUE)
    endif()
  endforeach()
  if(overridden)
    message(STATUS "new64: slot/ overrides ${ref_name}")
  else()
    list(APPEND SM64_SOURCES "${ref}")
  endif()
endforeach()
list(APPEND SM64_SOURCES ${SLOT_SOURCES} ${SM64_SUPPORT_SOURCES})

add_library(new64_core STATIC ${ENGINE_SOURCES} ${SM64_SOURCES})
target_include_directories(new64_core PUBLIC
  engine/include
  sm64/include
  sm64/src
  sm64/src/game
)
# Only libm, and only where it is a separate library at all (it is not on
# Windows, where the math functions live in the C runtime).
if(NOT WIN32)
  target_link_libraries(new64_core PUBLIC m)
endif()

set(APP_SOURCES app/demo_app.c app/input_script.c)

add_executable(new64_headless app/main_headless.c ${APP_SOURCES})
target_include_directories(new64_headless PRIVATE app)
target_link_libraries(new64_headless PRIVATE new64_core)

# --- Interactive front-end --------------------------------------------------
# One per platform, both driving the same engine core and both blitting the
# software framebuffer directly: no GL, no SDL, nothing to install.
if(WIN32)
  # The WIN32 flag makes this a GUI-subsystem binary with WinMain, so
  # double-clicking it does not also open a console window.
  add_executable(new64_play WIN32 app/main_win32.c ${APP_SOURCES})
  target_include_directories(new64_play PRIVATE app)
  # winmm is for timeBeginPeriod: without 1ms timer resolution the 30Hz tick
  # pacing wobbles by up to 15ms. XInput is loaded at runtime, not linked.
  target_link_libraries(new64_play PRIVATE new64_core user32 gdi32 winmm)
  message(STATUS "new64: building the Win32 front-end (new64_play.exe)")
elseif(NEW64_X11)
  find_path(X11_INCLUDE_DIR X11/Xlib.h)
  find_library(X11_LIBRARY X11)
  if(X11_INCLUDE_DIR AND X11_LIBRARY)
    add_executable(new64_play app/main_x11.c ${APP_SOURCES})
    target_include_directories(new64_play PRIVATE app ${X11_INCLUDE_DIR})
    target_link_libraries(new64_play PRIVATE new64_core ${X11_LIBRARY})
    message(STATUS "new64: building the X11 front-end (new64_play)")
  else()
    message(STATUS "new64: X11 not found, skipping the interactive front-end")
  endif()
endif()

enable_testing()
# The tests exercise the host layer too, so they link the same app sources the
# front-ends use rather than a separate harness.
add_executable(new64_tests tests/test_main.c ${APP_SOURCES})
target_include_directories(new64_tests PRIVATE tests app)
target_link_libraries(new64_tests PRIVATE new64_core)
add_test(NAME new64_tests COMMAND new64_tests)
