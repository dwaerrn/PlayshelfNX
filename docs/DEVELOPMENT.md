# Development notes

How the code is organised, how to build and test it, and the traps found along the way.

## Repository layout

| Path | Contents |
| --- | --- |
| `core/` | Plain C++17, no libnx and no Borealis, so it builds and is tested on a PC: sessions from the play event log, statistics, claims, playtime adjustments, history store, name cache, NACP names (including the compressed title block), dump read/write, library builder, game details, SteamGridDB lookup logic (`griddb.*`, network behind an interface) |
| `platform/switch/` | `switch_source.*`: the libnx data source (play log, profiles, installed titles, system statistics) |
| `tests/core_tests.cpp` | Unit tests for `core/` (run with `ctest`); the compressed-NACP tests need zlib |
| `tools/libcli.cpp` | Prints a library from a dump file or the demo data |
| `app/` | The Borealis user interface (PC and Switch builds), built from this folder |
| `demo.dump` | Recorded demo data (fake profiles and games) for `libcli` and the PC app |

## Building

### Core, tests and CLI (PC, from the repository root)

    cmake -B build_core -G Ninja
    cmake --build build_core
    ctest --test-dir build_core --output-on-failure
    ./build_core/libcli --demo                  # or: ./build_core/libcli demo.dump --profile Sam --sort last

### App (always from `app/`)

    cd app
    git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/xfangfang/borealis extern/borealis
    sh patch_borealis.sh                        # once; safe to repeat

Switch (devkitPro; e.g. `pacman -S switch-glm switch-curl`, deko3d includes `<glm/...>`):

    cmake -B build_switch -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -DCMAKE_DEPENDS_USE_COMPILER=OFF
    cmake --build build_switch --target Playshelf.nro

PC (Linux; needs the X11/GL development packages and `libcurl4-openssl-dev`):

    cmake -B build -G Ninja -DPLATFORM_DESKTOP=ON -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=ON
    cmake --build build
    cd build && ./Playshelf

- The repository-root CMake refuses the Switch options on purpose.
- Sources are found by a glob: run the configure step again after adding a file.
- On MSYS, keep `-DCMAKE_DEPENDS_USE_COMPILER=OFF` in the Switch configure command.
- Borealis changes its API now and then (for example `EasingFunction` became a `std::function` in November 2026). `ui::easeOut()` shows how to stay compatible with both versions.

### Running on a PC

Without a `library.dump` next to the binary the PC app shows demo data (two profiles, about 20 games, homebrew) and saves nothing, so the "New titles found" prompt appears on every start. A dump written by the console's Y button can be copied next to the binary as `library.dump`.

Headless: `xvfb-run -a dbus-run-session -- ./Playshelf`, keys with `xdotool` (Enter = A, Esc = B, X, Y, P = −, L, R), screenshots with `import -window root`.

The repository ships no Nintendo fonts. On the Switch the system fonts are used; on a PC the app's own button hints show the button's letter instead of the controller glyph, and Borealis' footers show placeholders. To get the glyphs on a PC, put a Switch symbol font at `app/resources/font/switch_icons.ttf` (git-ignored by convention, not part of the repo).

`LIBRARY_SGDB_BASE=http://127.0.0.1:PORT` points the SteamGridDB client at a local fake server (PC builds only).

## The UI (`app/src/`)

All views are built in C++ (no XML layouts; `app/resources/xml` is unused Borealis demo material). Each screen is an activity with a `brls::AppletFrame`, boxes and labels.

**Shared look, `ui_style.*`:** palette (`ui::kBackground`, `kSurface`, `kText`, `kMuted`, `kFaint`, `kAccent` amber, `kWarn`), `ui::label` / `ui::bold` (IBM Plex Sans Regular / SemiBold from `resources/fonts/`), `ui::panel`, `ui::topBar` (title, subtitle, button hints), `ui::frame` / `ui::bareFrame` (AppletFrame with an opaque background, the latter without Borealis' header and footer). `main.cpp` calls `ui::setFontPath()` before `Application::init` and `ui::loadFonts()` / `ui::applyTheme()` after it. Style rules: sentence-case captions, few separators, one accent colour, posters with radius 6.

| File | Screen / role |
| --- | --- |
| `library_activity.*` | Main screen: top bar, shelf of poster cards, panel at the bottom (icon, name, playtime, last played, launches, 7-day chart). Card size: `VirtualShelf(games, 288, 432, 16, ...)`. Button actions at the end of the file |
| `virtual_shelf.*` | Horizontal shelf that keeps only about 15 cards alive (6 behind, 8 ahead of the focus), with spacers for the rest |
| `game_card.*` | One card: poster, or icon and name, or plain name; homebrew badge; fade-in. Focus: white border and 1.06x scale drawn in `frame()` |
| `details_activity.*` | Details page: poster card, totals, month and weekday charts, the last 50 sessions |
| `adjust_activity.*` | Adjust playtime |
| `claim_activity.*` | "New titles found" prompt and "Claim titles" |
| `poster_activity.*` / `poster_fetch.*` | Change poster, SteamGridDB search and downloads (curl), background lookup |
| `profile_activity.*` | Choose a profile |
| `image_loader.*` | Background decoding of poster and icon files (one worker thread, pinned to core 1 on the Switch), scaled to the card size × 1.5 (docked output is 1080p); textures go into Borealis' `TextureCache`. Use `imageload::show` instead of `Image::setImageFromFile` on scrolling screens |
| `app_data.*` | Loads the library, merges events into the history, saves the data files, claims, renames, export. The UI's only way to data |
| `mock_data.*` | The `Game` struct the UI shows, `formatPlaytime`, `formatAgo`, stress mode |

## Traps

- **Draw-command buffer.** Borealis draws every view in the tree each frame into a fixed buffer; about 300 card views crash the console. Keep view counts bounded and virtualize long rows and lists. `patch_borealis.sh` raises the buffer (`DynamicCmdSize` 0x400000) and the GPU image pool (64 MiB); `main.cpp` caps the texture cache at 36.
- **Opaque frames.** During the fade between screens Borealis still draws the screen below. A frame without a background shows its panels over the old screen; use `ui::frame` / `ui::bareFrame`.
- **Off-screen drawing.** `GameCard` and `ui::CulledBox` skip drawing while outside the screen; use `CulledBox` for rows of long lists.
- **Game lists stay alive.** After a data change `LibraryActivity::rebuild()` swaps the content. Older game lists are kept on purpose (`mock_data.cpp`) because views hold references to `Game`s.
- **Focus after a rebuild** stays on the same title; keep it that way.
- **Background threads.** Callbacks from worker threads go through `brls::sync` and check a weak token, so none runs after its screen is gone.
- **Scrolling.** The details page uses `ScrollingBehavior::CENTERED`; NATURAL barely moves on a button press.
- **Stress mode.** `stress.txt` in the data folder (`<count>` or `<count> small`) repeats the library up to that many titles ("STRESS TEST" in the top bar). Delete it for normal use.

## Data and numbers

- The app keeps its own copy of the system's play event log (`history.txt`), so play time survives deleted titles and log rotation. Every start merges the new events.
- System statistics replace the log's numbers only when they count more launches (events were lost); otherwise the log's focus time is used, which matches NX Activity Log.
- Claims, ignores and adjustments are stored per profile; own names and hidden homebrew badges per title ID for all profiles.
- Deleted titles get their names from the system's control-data cache (`nsGetApplicationControlData` with the cache-only source); titles the system has forgotten show as "Unknown title (<ID>)".
