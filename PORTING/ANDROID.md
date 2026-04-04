# Android Platform Support

## Goal

Run Altirra on Android phones and tablets with a streamlined, touch-first
UI designed for playing games. The Android build is a compile-time variant
of the existing AltirraSDL frontend -- it shares the same emulation core,
display backend, and input manager. No separate application.

## Design Principles

1. **Touch-first UX.** Every interaction must work with fingers on a
   touchscreen. No hover states, no tiny hit targets, no desktop
   assumptions.

2. **Streamlined, not stripped.** The mobile UI exposes a curated subset
   of settings (PAL/NTSC, memory size, BASIC toggle, disk drives, audio,
   reset). Advanced configuration remains available on desktop. The goal
   is a great game-playing experience, not a portable settings editor.

3. **Existing infrastructure.** Touch controls feed into `ATInputManager`
   and the POKEY keyboard path -- the same code paths that physical
   gamepads and keyboards use. Zero changes to the emulation core.

4. **Resolution and orientation independence.** All control positions
   are expressed as fractions of screen dimensions. Layout adapts to
   portrait, landscape, and runtime rotation.

5. **ImGui overlay.** Touch controls and menus render as ImGui draw
   commands on top of the emulator display, integrated into the existing
   `ATUIRenderFrame()` pipeline. This gives consistent theming, proper
   text rendering, and alpha blending.

## Screen Layout

### Landscape (primary gaming orientation)

```
+-----------------------------------------------------+
| [START] [SELECT] [OPTION]                    [= Menu]|  <- Top bar
|                                                       |     (semi-transparent,
|                                                       |      auto-hides after
|                  EMULATOR DISPLAY                     |      3s inactivity)
|               (aspect-ratio preserved,                |
|                centered, black bars)                  |
|                                                       |
|   ,---.                                    [FIRE B]   |  <- Bottom overlay
|  ( Joy )                                   [FIRE A]   |     (semi-transparent)
|   `---'                                               |
+-----------------------------------------------------+
```

### Portrait

```
+---------------------------+
| [START][SEL][OPT]  [= Menu]|
|                           |
|                           |
|     EMULATOR DISPLAY      |
|     (letterboxed,         |
|      aspect-ratio kept)   |
|                           |
|                           |
|    ,---.          [FB]    |
|   ( Joy )         [FA]   |
|    `---'                  |
+---------------------------+
```

### Layout Zones

The screen is divided into non-overlapping zones calculated as fractions
of the display size. The emulator output occupies the center, preserving
its native aspect ratio (e.g. 4:3 for NTSC, slightly taller for PAL).
Touch controls occupy the remaining space around the display.

```
Zone map (landscape):

  +--[TOP BAR: 8% height]-----------------------------+
  |                                                    |
  |  [LEFT: 35% width]   [CENTER]   [RIGHT: 20% width]|
  |  joystick zone        display    fire buttons zone |
  |                                                    |
  +----------------------------------------------------+
```

In portrait, the bottom 30% of the screen is reserved for controls,
and the display fills the remaining area above.

Zone percentages are configurable (see Settings below).

## Touch Controls

### Virtual Joystick (default)

Appears where the left thumb touches within the left control zone.
A base circle renders at the touch-down point; a smaller knob follows
the finger within a maximum radius. When the finger lifts, both
disappear.

- **Dead zone:** 15% of max radius (configurable)
- **Max radius:** 12% of screen short dimension
- **Visual:** Outer ring (white, 30% opacity) + inner knob (white, 60% opacity)
- **Output:** Normalized direction fed to `ATInputManager` as digital joystick
  (4-way or 8-way, matching Atari hardware)

Implementation:

```cpp
// On FINGER_DOWN in left zone:
joy_active = true;
base = touch_pos;          // joystick appears here

// On FINGER_MOTION:
delta = touch_pos - base;
if (length(delta) > dead_zone) {
    // Convert to 4/8 directional mask
    uint32_t dirs = AnalogToDirectionMask(delta);
    // Feed to input manager as joystick directions
    g_inputManager.OnDigitalInput(kATInputCode_JoyLeft,  dirs & kDir_Left);
    g_inputManager.OnDigitalInput(kATInputCode_JoyRight, dirs & kDir_Right);
    g_inputManager.OnDigitalInput(kATInputCode_JoyUp,    dirs & kDir_Up);
    g_inputManager.OnDigitalInput(kATInputCode_JoyDown,  dirs & kDir_Down);
}

// On FINGER_UP:
joy_active = false;
// Release all directions
```

### Virtual D-Pad (alternative, selectable in settings)

A fixed-position cross rendered in the left zone. Four directional
hit areas (up/down/left/right) with optional diagonal corners for
8-way input. Always visible (does not follow the thumb).

- **Visual:** Four arrow segments in a cross pattern, highlights on touch
- **Hit areas:** Pie-slice sectors centered on each cardinal direction
- **Diagonal:** Corner sectors between cardinals (8-way mode)

### Fire Buttons

Two buttons stacked vertically in the right zone:

- **FIRE A** (primary, lower) -- maps to Joystick Trigger (TRIG0)
- **FIRE B** (secondary, upper) -- maps to second button (5200 mode) or
  user-configurable action

Visual: Rounded rectangles, ~15% of screen short dimension, labeled
"A" and "B". Highlight on press. Haptic pulse on touch-down.

### Console Keys (START / SELECT / OPTION)

Three smaller buttons in the top bar, left-aligned:

- **START** -- POKEY CONSOL bit 0 (active low)
- **SELECT** -- POKEY CONSOL bit 1
- **OPTION** -- POKEY CONSOL bit 2

These feed directly into the console switch register via the existing
`ATSimulator::SetConsoleSwitch()` path, the same as keyboard F2/F3/F4
on desktop.

Visual: Pill-shaped, labeled, semi-transparent. Smaller than fire
buttons (these are pressed less frequently).

### Haptic Feedback

All button presses trigger a short haptic pulse via `SDL_RumbleGamepad()`
or `SDL_RumbleGamepadTriggers()`. If no gamepad is connected, use the
Android vibrator via JNI or the SDL3 haptic subsystem.

Haptic feedback is optional, toggled in settings.

### Multi-Touch

Each control zone tracks its own `SDL_FingerID`:

- Left zone: joystick/dpad finger
- Right zone: fire button finger(s) -- both can be held simultaneously
- Top bar: console key finger

Fingers are independent. Pressing START while holding fire and
joystick works correctly. A finger that starts in one zone stays
assigned to that zone until lifted, even if it drifts outside.

## Hamburger Menu

Tapping the menu icon (top-right) pauses emulation and slides in
a panel from the right edge covering ~60% of screen width.

```
+----------------------------------+
|  Altirra                    [X]  |
|----------------------------------|
|  > Resume                        |
|  > Load Game                     |
|  > Disk Drives                   |
|  > Toggle Audio  [speaker icon]  |
|  > Warm Reset                    |
|  > Cold Reset                    |
|  > Virtual Keyboard              |
|  > Settings                      |
|----------------------------------|
|  > About                         |
+----------------------------------+
```

### Menu Items

| Item | Action |
|------|--------|
| **Resume** | Close menu, unpause emulation |
| **Load Game** | Open file browser (see below) |
| **Disk Drives** | Open disk drive management panel (mount/eject/swap for D1:-D4:) |
| **Toggle Audio** | Mute/unmute audio output, visual indicator |
| **Warm Reset** | `ATSimulator::WarmReset()`, close menu, resume |
| **Cold Reset** | `ATSimulator::ColdReset()`, close menu, resume |
| **Virtual Keyboard** | Show on-screen Atari keyboard overlay (Phase 2 feature) |
| **Settings** | Open settings panel (see below) |
| **About** | Version, credits, license |

Emulation is paused while the menu is open. Tapping outside the menu
or pressing the device back button closes it and resumes.

## File Browser

Opened from the hamburger menu ("Load Game") or shown on the main screen
if no image is loaded (centered "Load Game" button over the display area).

### Layout

```
+----------------------------------+
|  < Back          Load Game       |
|----------------------------------|
|  Recent:                         |
|    Star Raiders.xex              |
|    Mule.atr                      |
|    Jumpman.car                   |
|----------------------------------|
|  Browse: /storage/emulated/0/    |
|    [Downloads/]                  |
|    [Atari/]                      |
|    game1.xex                     |
|    game2.atr                     |
|    disk_collection.zip           |
|----------------------------------|
|  [Internal Storage] [SD Card]   |
+----------------------------------+
```

### Behavior

- **File types:** `.xex`, `.atr`, `.car`, `.bin`, `.rom`, `.cas`, `.dcm`,
  `.atz`, `.zip` (auto-extract single-file archives)
- **Recent files:** Last 20 loaded images, stored in settings
- **Default directory:** `Downloads` on first boot, user-configurable
- **Directory navigation:** Tap folder to enter, "< Back" to go up
- **Storage roots:** Buttons to jump to internal storage or SD card
- **Android permissions:** Uses Storage Access Framework (SAF) on
  Android 11+ via `SDL_ShowOpenFileDialog()` or JNI to
  `Intent.ACTION_OPEN_DOCUMENT` as fallback

### ROM/Firmware Discovery

On first boot (no firmware found), the file browser is shown with a
message: "Altirra needs Atari firmware ROMs to run. Select a folder
containing ROM files, or use the built-in replacement kernel."

Firmware search is recursive within the selected directory. Known
firmware is identified by CRC32 match against the existing firmware
database in `ATFirmwareManager`. The user can also use the built-in
HLE kernel (no external ROMs required for basic functionality).

## Settings Screen

Full-screen panel replacing the hamburger menu when opened.

```
+----------------------------------+
|  < Back          Settings        |
|----------------------------------|
|                                  |
|  SYSTEM                          |
|    Video Standard    [PAL    v]  |
|    Memory Size       [64KB   v]  |
|    BASIC             [Off    v]  |
|    SIO Patch         [On     v]  |
|                                  |
|  CONTROLS                        |
|    Input Style       [Joystick v]|
|    Control Size      [Medium  v] |
|    Control Opacity   [====o---]  |
|    Haptic Feedback   [On     v]  |
|                                  |
|  DISPLAY                         |
|    Filter Mode       [Bilinear v]|
|    Stretch Mode      [Aspect  v] |
|    Show FPS          [Off    v]  |
|                                  |
|  FIRMWARE                        |
|    ROM Directory     [Browse   ] |
|    Status: 3 ROMs found          |
|                                  |
|  AUDIO                           |
|    Volume            [====o---]  |
|                                  |
+----------------------------------+
```

### Setting Details

| Setting | Values | Maps to |
|---------|--------|---------|
| Video Standard | PAL / NTSC | `ATSimulator::SetVideoStandard()` |
| Memory Size | 16K / 48K / 64K / 128K / 320K / 1088K | `ATSimulator::SetMemoryMode()` |
| BASIC | On / Off | `ATSimulator::SetBASICEnabled()` |
| SIO Patch | On / Off | `ATSimulator::SetSIOPatchEnabled()` |
| Input Style | Joystick / D-Pad | Touch controls layout selector |
| Control Size | Small / Medium / Large | Scales touch control zones |
| Control Opacity | 10%--100% slider | Alpha of touch overlays |
| Haptic Feedback | On / Off | Enable/disable vibration |
| Filter Mode | Sharp / Bilinear | Display texture filtering |
| Stretch Mode | Aspect / Fill / Integer | Display scaling mode |
| Show FPS | On / Off | FPS counter overlay |
| ROM Directory | Directory picker | Firmware search root |
| Volume | 0%--100% slider | `ATSimulator` audio volume |

Settings persist to `~/.config/altirra/settings.ini` (same file as
desktop, Android path resolved via `SDL_GetPrefPath()`).

## First-Boot Experience

1. Splash screen with Altirra logo (brief, <1s)
2. "Welcome to Altirra" screen:
   - "Altirra emulates the Atari 800/XL/5200 computer family."
   - "To get started, select a folder containing Atari ROM firmware,
     or tap Skip to use the built-in replacement kernel."
   - [Select ROM Folder] [Skip]
3. If ROMs found: show file browser to load a game
4. If skipped: show file browser to load a game (HLE kernel active)
5. If no game loaded: main screen with "Load Game" button centered

On subsequent boots, go straight to the last-loaded game (if any)
or the "Load Game" screen.

## Virtual Keyboard (Phase 2)

A full Atari keyboard overlay covering the bottom ~50% of the screen.
Layout matches the Atari 800XL keyboard: 4 rows, QWERTY layout, with
special keys (BREAK, CAPS, HELP, INVERSE, CLEAR).

```
+--------------------------------------------------+
|  Esc 1 2 3 4 5 6 7 8 9 0 < > BS                 |
|  Tab  Q W E R T Y U I O P - = Ret               |
|  Ctrl  A S D F G H J K L ; + *                   |
|  Shift  Z X C V B N M , . / Shift               |
|       [HELP] [     SPACE     ] [CAPS] [BRK]     |
+--------------------------------------------------+
```

Each key press sends the corresponding Atari scancode through the
existing POKEY keyboard path (`ATInputSDL3_PushRawKey()`).

The keyboard slides up from the bottom with animation. While visible,
the emulator display scales down to fit above it. A "hide keyboard"
button (or swipe-down gesture) dismisses it.

This is a Phase 2 feature due to complexity (key repeat, shift states,
CTRL combinations, layout for multiple Atari models).

## Architecture

### New Files

| File | Purpose |
|------|---------|
| `src/AltirraSDL/source/ui_mobile.h` | Mobile UI state, hamburger menu, settings panel |
| `src/AltirraSDL/source/ui_mobile.cpp` | Mobile UI rendering and input handling |
| `src/AltirraSDL/source/touch_controls.h` | Touch control definitions and state |
| `src/AltirraSDL/source/touch_controls.cpp` | Touch control rendering, hit testing, input routing |
| `src/AltirraSDL/source/touch_layout.h` | Adaptive layout engine (zone calculation) |
| `src/AltirraSDL/source/touch_layout.cpp` | Portrait/landscape layout computation |
| `src/AltirraSDL/source/file_browser_mobile.h` | Mobile file browser UI |
| `src/AltirraSDL/source/file_browser_mobile.cpp` | Directory scanning, recent files, SAF integration |

### Modified Files

| File | Change |
|------|--------|
| `main_sdl3.cpp` | `#ifdef __ANDROID__` blocks for mobile init, touch event routing |
| `CMakeLists.txt` | Android toolchain detection, mobile source files, gradle integration |

### Build System

The Android build uses the SDL3 android-project template:

```
android-project/
  app/
    build.gradle
    src/main/
      AndroidManifest.xml
      java/       <- SDL3 Android activity (from SDL3 source)
      jni/
        CMakeLists.txt  <- includes our top-level CMakeLists.txt
  gradle/
  build.gradle
  settings.gradle
```

SDL3 provides `SDL_android.c` and the Java activity
(`SDLActivity.java`) that handles the Android lifecycle and creates
the GL surface. Our CMakeLists.txt compiles into a shared library
(`libmain.so`) that SDL3's activity loads.

The existing CMakeLists.txt already has `if(ANDROID)` detection.
The Android build adds:

```cmake
if(ANDROID)
    target_sources(AltirraSDL PRIVATE
        source/ui_mobile.cpp
        source/touch_controls.cpp
        source/touch_layout.cpp
        source/file_browser_mobile.cpp
    )
    target_compile_definitions(AltirraSDL PRIVATE ALTIRRA_MOBILE=1)
endif()
```

Desktop builds do not compile or link any mobile UI code.

### Input Routing

Touch events integrate with the existing input infrastructure:

```
SDL_EVENT_FINGER_DOWN / MOTION / UP
    |
    v
touch_controls.cpp (zone assignment, gesture recognition)
    |
    +-- Joystick/D-Pad direction --> ATInputManager::OnDigitalInput()
    |                                (kATInputCode_Joy*)
    |
    +-- Fire buttons --> ATInputManager::OnDigitalInput()
    |                    (kATInputCode_JoyButton0/1)
    |
    +-- Console keys --> ATSimulator::SetConsoleSwitch()
    |                    (START/SELECT/OPTION bits)
    |
    +-- Virtual keyboard --> ATInputSDL3_PushRawKey()
    |                        (POKEY scancode path)
    |
    +-- UI elements --> ImGui (hamburger, file browser, settings)
```

Touch events that land in UI zones (hamburger menu, file browser) are
forwarded to ImGui via `ImGui_ImplSDL3_ProcessEvent()`. Touch events
in control zones are consumed by `touch_controls.cpp` and never reach
ImGui.

### State Management

The mobile UI introduces a simple state machine:

```
BOOT --> FIRST_RUN_WIZARD --> FILE_BROWSER --> RUNNING
                                  ^               |
                                  |               v
                              HAMBURGER <---> PAUSED
                                  |
                                  v
                              SETTINGS
```

- **RUNNING:** Emulation active, touch controls visible, top bar auto-hides
- **PAUSED:** Emulation paused (hamburger open, or explicit pause)
- **FILE_BROWSER:** Full-screen file browser, emulation paused
- **SETTINGS:** Full-screen settings, emulation paused
- **FIRST_RUN_WIZARD:** One-time firmware setup

State transitions are explicit. Every state that pauses emulation
resumes it on exit (or returns to the previous state).

## Orientation Handling

SDL3 on Android receives `SDL_EVENT_WINDOW_RESIZED` on rotation.
The layout engine recalculates all zones on every resize:

```cpp
void TouchLayout_Update(int screen_w, int screen_h) {
    bool landscape = (screen_w > screen_h);

    if (landscape) {
        joy_zone   = { 0, 0.08f, 0.35f, 1.0f };      // left 35%, below top bar
        fire_zone  = { 0.80f, 0.50f, 1.0f, 1.0f };    // right 20%, bottom half
        top_bar    = { 0, 0, 1.0f, 0.08f };            // top 8%
        display    = { 0.15f, 0.08f, 0.80f, 1.0f };    // center
    } else {
        joy_zone   = { 0, 0.70f, 0.50f, 1.0f };       // left half, bottom 30%
        fire_zone  = { 0.65f, 0.70f, 1.0f, 1.0f };    // right, bottom 30%
        top_bar    = { 0, 0, 1.0f, 0.06f };            // top 6%
        display    = { 0, 0.06f, 1.0f, 0.70f };        // above controls
    }
}
```

The Android manifest declares support for all orientations:

```xml
<activity
    android:screenOrientation="fullSensor"
    android:configChanges="orientation|screenSize|screenLayout|keyboardHidden">
```

SDL3 hint for allowed orientations:

```cpp
SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
```

## Android-Specific Considerations

### Permissions

- **Storage:** `READ_EXTERNAL_STORAGE` (API < 33) or
  `READ_MEDIA_IMAGES` + SAF for API 33+. ROM files and disk images
  must be readable.
- **Vibration:** `android.permission.VIBRATE` for haptic feedback.
- No network permissions needed (emulated network is internal).

### Performance

- OpenGL ES 3.0 is the minimum (maps well to our GL 3.3 backend with
  minor shader adjustments: `#version 300 es`, precision qualifiers).
- The emulation core is CPU-bound and single-threaded. Modern phones
  have more than enough single-core performance for 6502 emulation at
  1.79 MHz.
- Frame pacing: Use `SDL_SetSwapInterval(1)` for vsync. The emulator's
  existing frame pacing logic handles the rest.

### Lifecycle

Android can pause/resume/destroy the activity at any time. SDL3
translates these to `SDL_EVENT_DID_ENTER_BACKGROUND` and
`SDL_EVENT_WILL_ENTER_FOREGROUND`.

On background:
- Pause emulation
- Mute audio
- Save state snapshot (optional, for instant resume)

On foreground:
- Restore state
- Resume audio
- Resume emulation (or stay paused if user paused manually)

On destroy:
- Save settings
- Flush any pending state

### Audio

SDL3 audio works on Android without changes. The existing
`ATAudioOutputSDL3` implementation handles device open/close and
callback-based mixing. Android audio latency varies by device; the
existing buffer size negotiation should work.

### App Icon and Metadata

The APK needs:
- App icon (Altirra logo, adaptive icon format for Android 8+)
- App name: "Altirra"
- Min SDK: 24 (Android 7.0 -- covers 99%+ of active devices)
- Target SDK: 34 (current requirement for Play Store)

## Implementation Phases

### Phase 1: Foundation

- Android build system (gradle + CMake, APK packaging)
- Basic touch input routing (joystick + fire → ATInputManager)
- Adaptive layout engine (portrait/landscape zone calculation)
- Touch control rendering (ImGui overlay)
- Console key buttons (START/SELECT/OPTION)
- Hamburger menu (pause, resume, reset)

### Phase 2: Usability

- File browser with directory navigation
- ROM/firmware discovery (recursive scan, CRC matching)
- First-boot wizard
- Settings screen (PAL/NTSC, memory, controls, display)
- Recent files list
- Audio mute toggle
- Haptic feedback

### Phase 3: Polish

- Virtual D-Pad alternative
- Control size/opacity customization
- Virtual Atari keyboard overlay
- Android lifecycle handling (background/foreground)
- State save/restore on backgrounding
- App icon, Play Store metadata

### Phase 4: Advanced (future)

- Save states (quick save/load from hamburger)
- Shader effects (CRT filter via librashader, if GPU allows)
- Bluetooth gamepad support (already works via SDL3 gamepad API)
- Multiplayer (second player via gamepad)
- Tape control (cassette transport)
- 5200 controller mode (analog joystick + numeric keypad overlay)

## Comparison to ANDROID.md Example

The `NEW-ANDROID/ANDROID.md` example demonstrates basic SDL3 touch
principles (multi-touch finger tracking, normalized coordinates,
orientation detection) but is not suitable as an implementation base:

| Aspect | Example | Required |
|--------|---------|----------|
| Architecture | Standalone demo | Integrated into AltirraSDL |
| Controls | 2 buttons + joystick | START/SELECT/OPTION + 2 fire + joystick/dpad |
| Menu system | None | Hamburger with slide-in panel |
| Configuration | None | PAL/NTSC, memory, controls, firmware |
| File loading | None | File browser with SAF, recent files |
| Rendering | SDL_RenderFillRect | ImGui overlay on GL display backend |
| Input routing | Direct float vars | ATInputManager + POKEY keyboard |
| Haptics | None | SDL3 haptic / Android vibrator |
| Layout | Hardcoded percentages | Adaptive zone engine |
| State management | None | Pause/resume/lifecycle/settings persistence |

The example's finger-tracking pattern (per-zone `SDL_FingerID`
assignment) is a valid technique and should be reused in
`touch_controls.cpp`. The rest should be built from scratch on top
of the existing AltirraSDL infrastructure.
