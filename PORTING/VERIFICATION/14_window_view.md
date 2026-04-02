# Verification: Window Menu

Compares Windows `cmdwindow.cpp` and window-related commands from
`cmds.cpp`/`main.cpp` with SDL3 `ui_main.cpp` Window menu.

---

## Handler: OnCommandWindowClose
**Win source:** cmdwindow.cpp:24
**Win engine calls:**
1. Check `g_pMainWindow` exists
2. Check no modal or fullscreen frame active
3. Get active frame: `g_pMainWindow->GetActiveFrame()`
4. `g_pMainWindow->CloseFrame(w)`
**SDL3 source:** ui_main.cpp:2006 -- "Window > Close"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Notes:** SDL3 does not have a dockable pane system. The Close command applies to debug/tool panes in Windows, which don't exist in SDL3 yet.
**Severity:** N/A (depends on debugger/pane system)

---

## Handler: OnCommandWindowUndock
**Win source:** cmdwindow.cpp:38
**Win engine calls:**
1. Check `g_pMainWindow` exists
2. Check no modal or fullscreen frame active
3. Get active frame: `g_pMainWindow->GetActiveFrame()`
4. `g_pMainWindow->UndockFrame(w)`
**SDL3 source:** ui_main.cpp:2007 -- "Window > Undock"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Notes:** No dockable pane infrastructure in SDL3.
**Severity:** N/A (depends on debugger/pane system)

---

## Handler: OnCommandWindowNextPane
**Win source:** cmdwindow.cpp:52
**Win engine calls:**
1. `g_pMainWindow->CycleActiveFrame(+1)`
**SDL3 source:** ui_main.cpp:2008 -- "Window > Next Pane"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Notes:** No pane cycling in SDL3.
**Severity:** N/A (depends on debugger/pane system)

---

## Handler: OnCommandWindowPrevPane
**Win source:** cmdwindow.cpp:57
**Win engine calls:**
1. `g_pMainWindow->CycleActiveFrame(-1)`
**SDL3 source:** ui_main.cpp:2009 -- "Window > Previous Pane"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Notes:** No pane cycling in SDL3.
**Severity:** N/A (depends on debugger/pane system)

---

## Handler: OnCommandViewAdjustWindowSize
**Win source:** main.cpp:2241
**Win engine calls:**
1. `g_pMainWindow->AutoSize()`
   - Calculates optimal window size based on display output dimensions and current scaling
**SDL3 source:** ui_main.cpp:2012 -- "Window > Adjust Window Size"
**SDL3 engine calls:**
1. Submenu with 4 fixed size options:
   - 1x (336x240)
   - 2x (672x480)
   - 3x (1008x720)
   - 4x (1344x960)
2. `SDL_SetWindowSize(window, sz.w, sz.h)` for selected size
**Status:** DIVERGE
**Notes:**
- Windows uses `AutoSize()` which calculates the optimal size based on current display output (respects overscan, interlace, aspect ratio correction). It is a single menu action.
- SDL3 replaces this with a submenu of 4 fixed multiplier sizes. These assume a base resolution of 336x240 and do not account for overscan mode, interlace doubling, or pixel aspect ratio.
- The SDL3 approach is simpler but less accurate -- it won't produce correct sizes when overscan is extended/full, when interlace is enabled, or when aspect ratio correction changes the effective width.
**Severity:** Medium

---

## Handler: OnCommandViewResetWindowLayout
**Win source:** main.cpp:2246
**Win engine calls:**
1. `ATLoadDefaultPaneLayout()`
   - Restores the default dockable pane arrangement (which panes are open and where)
**SDL3 source:** ui_main.cpp:2026 -- "Window > Reset Window Layout"
**SDL3 engine calls:**
1. `SDL_SetWindowSize(window, 672, 480)` -- reset to 2x size
2. `SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED)` -- center on screen
**Status:** DIVERGE
**Notes:**
- Windows resets the dockable pane layout (console, registers, memory panes, etc.) to default positions. It does NOT resize or reposition the main window.
- SDL3 interprets "reset layout" as resizing the window to 2x and centering it. This is a completely different operation.
- The semantic mismatch is because SDL3 has no pane system to reset. The SDL3 behavior is still useful (user can fix a badly sized/positioned window) but is not equivalent to the Windows behavior.
**Severity:** Low

---

## Summary

| Feature | Status |
|---------|--------|
| Close Pane | MISSING (placeholder, N/A without pane system) |
| Undock Pane | MISSING (placeholder, N/A without pane system) |
| Next Pane | MISSING (placeholder, N/A without pane system) |
| Previous Pane | MISSING (placeholder, N/A without pane system) |
| Adjust Window Size | DIVERGE (fixed sizes vs auto-size) |
| Reset Window Layout | DIVERGE (window resize vs pane layout reset) |
