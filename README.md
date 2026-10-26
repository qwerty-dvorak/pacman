# Pac-Man — C17, raylib and xmake

A playable Pac-Man with menus, pause, five maze layouts, an in-game level editor,
persistent settings/high scores, and synthesized chiptune music and sound effects.
Linux/X11, tuned for the build machine; OpenGL 4.5 is required.

## Build and play

```sh
xmake
xmake run pacman
xmake bench -s 10
```

xmake downloads and builds raylib 5.5 through the local package in
`xmake/repo/packages/r/raylib-native`. No CMake build is used. Install GCC, xmake ≥ 2.9,
X11 development headers and a working Mesa/OpenGL driver. The default release build uses
`-O3 -march=native -mtune=native` and LTO, including raylib. The resulting binary is local
to the build CPU; use `xmake f --native=n` for portable CPU code generation.

```sh
xmake f -m debug
xmake build -a                    # address/undefined-behaviour sanitizers
xmake f -m release                # restore optimized mode
xmake run bench_cpu --games 8     # headless behavior checks and CPU timings
```

## Controls and screens

- Main menu: Up/Down selects; Left/Right changes maze set or starting level; Enter selects.
- Gameplay: arrows or WASD steers. Esc/P pauses; losing window focus also pauses.
- Pause: resume, restart, options or main menu. Simulation time does not advance while paused.
- Options: independent music/SFX volume, unlimited/vsync/60/120/144/240/360 FPS,
  performance overlay, and starting lives. F1 toggles the overlay during gameplay.
- High scores: qualifying games open three-letter initials entry. Type initials or use
  arrows, then Enter to save. Help and score-table screens are available from the menu.

The **ARCADE** pack keeps the original maze. **MAZE TOUR** rotates Classic, Blossom,
Glacier, Amber and Garden, two levels per maze. Each new maze has distinct topology,
tunnels and wall color. Starting levels 1–21 are selectable; play continues beyond them
with the last difficulty-table settings and repeating maze rotation.
**CUSTOM** plays all valid saved editor slots in slot order.

### Level editor

Choose **MAZE EDITOR**, then one of 24 slots. New slots start from the classic layout.

| Control | Action |
| --- | --- |
| Mouse / arrows | Position cursor |
| Left click / Space / Enter | Paint selected tool |
| Right click | Erase |
| 1 / 2 / 3 / 4, or Tab | Wall / dot / energizer / empty |
| M | Toggle horizontal mirror painting |
| K | Cycle wall colors |
| F | Fill reachable empty corridors with dots |
| Z / Y | Undo / redo tile edits, up to 64 snapshots |
| C | Restore classic template |
| F2 | Rename slot; type then Enter |
| Page Up / Page Down | Switch slots, saving current changes |
| S | Save |
| T | Test-play; Esc returns to editor |
| H | Editor help |
| Esc | Save changes and return to menu |

House geometry, its exit, the player start and the fruit spot are protected. Validation
requires solid top/bottom boundaries, paired tunnel openings, intact landmarks and
reachable pellets. Invalid drafts can be saved, but cannot be tested or included in the
custom pack. Save failures leave the draft open rather than silently discarding changes.

### Saved data

Settings, scores and maze files are written under `$XDG_DATA_HOME/pacman`, or
`~/.local/share/pacman` when unset. Writes use a temporary file and rename. Maze slots are
`levels/slot_01.maze` through `slot_24.maze`: optional `name NAME` and `color R G B` headers,
then 31 rows of 28 tiles (`#` wall, `-` door, `.` dot, `o` energizer, space empty).

### Command-line options

`--window fixed|resizable|fullscreen`, `--scale N`, `--vsync`, `--no-overlay`,
`--autoplay`, `--bench SECONDS`, `--seed N`.

Fixed-size windows avoid tiling an unnecessarily large framebuffer. Normal UI/editor
screens are capped at 120 FPS, unfocused windows at 30 FPS; gameplay defaults to unlimited.
Benchmark mode bypasses saved settings and menus, disables audio, and does not apply UI caps.

## Implementation

| Directory | Responsibility |
| --- | --- |
| `src/core` | Monotonic clock, frame pacing, statistics |
| `src/game` | Integer simulation, maze validation, level packs, ghost AI, autopilot |
| `src/scene` | CPU sprite batches, cached static geometry, procedural custom-maze art |
| `src/gpu` | OpenGL 4.5 renderer, atlas composition, asynchronous GPU timer queries |
| `src/audio` | Allocation-free wavetable synth and raylib audio-stream/SPSC queue |
| `src/app` | Screens, editor, settings, scores and main loop |
| `bench` | Headless checks and CPU benchmarks |
| `assets/sprites` | Supplied sprite sheets, copied next to the executable |

Simulation runs at a fixed 60 Hz with Q16.16 distance accumulators and interpolated actor
rendering. Ghosts use scatter/chase scheduling, individual targeting, frightened mode,
returning eyes, house-release counters and per-level speeds. Fruit, score chains, extra
lives, death animations and level flashing are included. Custom layouts retain fixed
house/start landmarks; generated walls are arcade-inspired, not pixel-identical copies.

Music and effects are generated at 48 kHz from short wavetable sequences: intro jingle,
looping menu tune, waka, siren, frightened/eyes ambience, fruit, ghost-eaten, death, extra
life and menu cues. A lock-free command queue separates the main thread from the audio
callback. A missing audio device leaves the game playable without sound.

## Efficiency and measurements

One atlas, one draw call, packed 12-byte vertices, persistently mapped triple-buffered
vertex storage and no full-screen clear. Maze/dot geometry is rebuilt only when dots,
maze flashing or framebuffer dimensions change; each GPU ring slot retains its static
copy. Unchanged frames append only animated sprites and HUD content. Modern ISA selection
is left to `-march=native`; forcing wider SIMD is not assumed to help this branch-heavy game.

Measured on i5-1135G7 / Iris Xe, Mesa 26.1.8, X11/i3. Turbo was disabled and the CPU used a
power-saving policy; results vary with system load.

| CPU measurement | Mean |
| --- | ---: |
| Game tick | 0.108 µs |
| Static-layer update, mixed hits/rebuilds | 0.060 µs |
| Static-layer cache hit | 0.017 µs |
| Custom maze artwork generation, on edit/level change only | 1.45 ms |
| Synth, 1,024 samples / 21.3 ms of audio | 12.4 µs |

A 672×864 uncapped render smoke run reached **2,729 FPS**, with **1.07 µs** of CPU
vertex building per frame and **55.4 µs** GPU draw time. The earlier uncached renderer
spent roughly 6 µs building vertices. X11 buffer presentation remained the dominant cost.
Use `xmake bench` for current measurements; short render runs mostly include the READY
sequence, so use 10 seconds or longer to include moving gameplay.

Verification included all five layouts and maze-file round trips, deterministic simulation,
ASan/UBSan CPU runs, actual keyboard-driven menu/pause/options/editor/test-play flows,
unreachable-pellet rejection, editor save/reload, a full game-over/initials/save flow, and
PulseAudio stream startup/shutdown. Intro synthesis produced no clipped samples and ended
within four seconds; every effect produced non-silent output.

## Assets and license

The supplied `general_sprites.png` supplies the classic maze and actors;
`all_assets_palettes.png` supplies the arcade font. `hud_assets.png` and `maze_parts.png`
are preserved alongside them. Sources use the zlib/libpng license in [LICENSE](LICENSE).
Pac-Man artwork belongs to its respective rights holders; this is an unofficial fan project.
