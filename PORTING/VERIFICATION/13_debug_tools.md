# Verification: Debug and Tools Menus

Compares Windows `cmddebug.cpp` and `cmdtools.cpp` with SDL3
`ui_main.cpp` Debug and Tools menus.

Note: The debugger and tools are largely unimplemented in SDL3. Most
items are menu placeholders (always disabled). This is expected at the
current porting stage -- the debugger is a complex Windows-native UI
with multiple dockable panes. Items are listed as MISSING with N/A
severity unless they have non-debugger functionality.

---

## Handler: OnCommandDebugToggleDebugger
**Win source:** cmddebug.cpp:70
**Win engine calls:**
1. If active: `ATCloseConsole()`
2. Else: `ATOpenConsole()`
**SDL3 source:** ui_main.cpp:1709 -- "Debug > Enable Debugger"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A (debugger infrastructure not ported)

---

## Handler: OnCommandDebuggerOpenSourceFile
**Win source:** cmddebug.cpp:34
**Win engine calls:**
1. `VDGetLoadFileName()` for source file
2. `ATOpenSourceWindow(fn.c_str())`
**SDL3 source:** ui_main.cpp:1710 -- "Debug > Open Source File..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebuggerOpenSourceFileList
**Win source:** cmddebug.cpp:42
**Win engine calls:**
1. `ATUIShowSourceListDialog()`
**SDL3 source:** ui_main.cpp:1711 -- "Debug > Source File List..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: Debug Window Panes (Console, Registers, Disassembly, Call Stack, History, Memory 1-4, Watch 1-4, Breakpoints, Targets, Debug Display)
**Win source:** cmddebug.cpp:209-247 (registered commands for pane activation)
**Win engine calls:** Various `ATActivateUIPane()` calls for dockable debug panes
**SDL3 source:** ui_main.cpp:1713-1736 -- "Debug > Window > ..."
**SDL3 engine calls:** None (all placeholders, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugRun
**Win source:** cmddebug.cpp:77
**Win engine calls:**
1. `ATGetDebugger()->Run(kATDebugSrcMode_Same)`
**SDL3 source:** ui_main.cpp:1763 -- "Debug > Run/Break"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugBreak
**Win source:** cmddebug.cpp:81
**Win engine calls:**
1. `ATOpenConsole()`
2. `ATGetDebugger()->Break()`
**SDL3 source:** ui_main.cpp:1764 -- "Debug > Break"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugRunStop
**Win source:** cmddebug.cpp:86
**Win engine calls:**
1. If running or commands queued: `OnCommandDebugBreak()`
2. Else: `OnCommandDebugRun()`
**SDL3 source:** ui_main.cpp:1763 -- "Debug > Run/Break"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugStepInto
**Win source:** cmddebug.cpp:107
**Win engine calls:**
1. Try active debugger pane command first
2. Fallback: `ATGetDebugger()->StepInto(ATUIGetDebugSrcMode())`
**SDL3 source:** ui_main.cpp:1768 -- "Debug > Step Into"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugStepOver
**Win source:** cmddebug.cpp:131
**Win engine calls:**
1. Try active debugger pane command first
2. Fallback: `ATGetDebugger()->StepOver(ATUIGetDebugSrcMode())`
**SDL3 source:** ui_main.cpp:1769 -- "Debug > Step Over"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugStepOut
**Win source:** cmddebug.cpp:119
**Win engine calls:**
1. Try active debugger pane command first
2. Fallback: `ATGetDebugger()->StepOut(ATUIGetDebugSrcMode())`
**SDL3 source:** ui_main.cpp:1770 -- "Debug > Step Out"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugNewBreakpoint
**Win source:** cmddebug.cpp:143
**Win engine calls:**
1. `ATUIShowDialogNewBreakpoint()`
**SDL3 source:** N/A (not in menu)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugToggleBreakpoint
**Win source:** cmddebug.cpp:147
**Win engine calls:**
1. Get active debugger pane
2. `dbgp->OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint)`
**SDL3 source:** N/A (not in menu)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebuggerToggleBreakAtExeRun
**Win source:** cmddebug.cpp:46
**Win engine calls:**
1. `dbg->SetBreakOnEXERunAddrEnabled(!dbg->IsBreakOnEXERunAddrEnabled())`
**SDL3 source:** ui_main.cpp:1755 -- "Debug > Options > Break at EXE Run Address"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugToggleAutoReloadRoms
**Win source:** cmddebug.cpp:53
**Win engine calls:**
1. `g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled())`
**SDL3 source:** ui_main.cpp:1753 -- "Debug > Options > Auto-Reload ROMs on Cold Reset"
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugToggleAutoLoadKernelSymbols
**Win source:** cmddebug.cpp:57
**Win engine calls:**
1. `g_sim.SetAutoLoadKernelSymbolsEnabled(!g_sim.IsAutoLoadKernelSymbolsEnabled())`
**SDL3 source:** N/A (not in menu)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugToggleAutoLoadSystemSymbols
**Win source:** cmddebug.cpp:61
**Win engine calls:**
1. `dbg->SetAutoLoadSystemSymbols(!dbg->IsAutoLoadSystemSymbolsEnabled())`
**SDL3 source:** N/A (not in menu)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Severity:** N/A

---

## Handler: Debug.PreStartSymbolLoad* / PostStartSymbolLoad*
**Win source:** cmddebug.cpp:218-223
**Win engine calls:**
1. `ATGetDebugger()->SetSymbolLoadMode(preStart, mode)`
**SDL3 source:** N/A (not in menu)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Severity:** N/A

---

## Handler: Debug.ScriptAutoLoad*
**Win source:** cmddebug.cpp:225-227
**Win engine calls:**
1. `ATGetDebugger()->SetScriptAutoLoadMode(mode)`
**SDL3 source:** N/A (not in menu)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Severity:** N/A

---

## Handler: Debug.ToggleDebugLink
**Win source:** cmddebug.cpp:229-233
**Win engine calls:**
1. `d.SetDebugLinkEnabled(!d.GetDebugLinkEnabled())`
**SDL3 source:** N/A (not in menu)
**SDL3 engine calls:** N/A
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugChangeFontDialog
**Win source:** cmddebug.cpp:66
**Win engine calls:**
1. `ATUIShowDialogDebugFont(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_main.cpp:1757 -- "Debug > Options > Change Font..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugVerifierDialog
**Win source:** cmddebug.cpp:159
**Win engine calls:**
1. `ATUIShowDialogVerifier(ATUIGetNewPopupOwner(), g_sim)`
**SDL3 source:** ui_main.cpp:1778 -- "Debug > Verifier..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: OnCommandDebugShowTraceViewer
**Win source:** cmddebug.cpp:166
**Win engine calls:**
1. `ATUIOpenTraceViewer(nullptr, g_sim.GetTraceCollection())`
**SDL3 source:** ui_main.cpp:1779 -- "Debug > Performance Analyzer..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** N/A

---

## Handler: Visualization (GTIA/ANTIC analysis modes)
**Win source:** (not in cmddebug.cpp, part of view commands)
**SDL3 source:** ui_main.cpp:1739-1750 -- "Debug > Visualization"
**SDL3 engine calls:**
1. "Cycle GTIA Visualization": `gtia.SetAnalysisMode((next))` -- IMPLEMENTED
2. "Cycle ANTIC Visualization": placeholder, always disabled
**Status:** DIVERGE
**Notes:** GTIA visualization cycling is functional. ANTIC visualization is placeholder.
**Severity:** N/A

---

## Handler: OnCommandToolsCompatDBDialog
**Win source:** cmdtools.cpp:34
**Win engine calls:**
1. `ATUIShowDialogCompatDB(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_main.cpp:1997 -- "Tools > Compatibility Database..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** DIVERGE
**Notes:** The standalone Tools menu item is a placeholder, but the same functionality is available in Configure System > Compat DB (ui_system.cpp:1588), which is fully implemented.
**Severity:** Low

---

## Handler: OnCommandToolsAnalyzeTapeDecoding
**Win source:** cmdtools.cpp:38
**Win engine calls:**
1. Open source tape file dialog
2. Open save analysis file dialog
3. Check paths are different
4. `ATLoadCassetteImage(src, &analysisFile, ctx)`
**SDL3 source:** ui_main.cpp:1992 -- "Tools > Analyze tape decoding..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** Low

---

## Handler: OnCommandToolsAdvancedConfiguration
**Win source:** cmdtools.cpp:58
**Win engine calls:**
1. `ATUIShowDialogAdvancedConfigurationModeless(nullptr)`
**SDL3 source:** ui_main.cpp:1998 -- "Tools > Advanced Configuration..."
**SDL3 engine calls:** None (placeholder, always disabled)
**Status:** MISSING
**Severity:** Low

---

## SDL3-only Tools menu placeholders (no Windows cmdtools equivalent):
- "Disk Explorer..." (ui_main.cpp:1989) -- MISSING, N/A
- "Export ROM set..." (ui_main.cpp:1991) -- MISSING, N/A
- "First Time Setup..." (ui_main.cpp:1994) -- MISSING, N/A
- "Keyboard Shortcuts..." (ui_main.cpp:1996) -- MISSING, N/A

---

## Summary

| Feature | Status |
|---------|--------|
| Debugger enable/disable | MISSING (placeholder) |
| Debug pane windows (13 types) | MISSING (all placeholders) |
| Run/Break/Step commands | MISSING (all placeholders) |
| Breakpoint management | MISSING |
| Debug options (6 toggles) | MISSING (placeholders) |
| Symbol load modes | MISSING |
| Script auto-load modes | MISSING |
| Debug link | MISSING |
| Verifier dialog | MISSING (placeholder) |
| Trace/Performance viewer | MISSING (placeholder) |
| GTIA Visualization cycling | MATCH |
| ANTIC Visualization cycling | MISSING (placeholder) |
| Compat DB (standalone) | DIVERGE (in Configure System instead) |
| Analyze tape decoding | MISSING (placeholder) |
| Advanced Configuration | MISSING (placeholder) |
| Convert SAP to EXE | MISSING (placeholder, see 11_recording.md) |
