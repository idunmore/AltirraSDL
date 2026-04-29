//	AltirraSDL - in-app debug log capture & viewer
//
//	Captures every ATLogChannel write into a thread-safe ring buffer so
//	the user can read the log on platforms where stderr is unreachable
//	(Android — logcat is gated by a USB-cable + adb pairing the user
//	doesn't have).  The ring keeps the most recent N lines; older lines
//	are dropped silently.
//
//	The viewer (rendered from the About dialog) shows the captured text
//	in a read-only multiline input with two action buttons:
//	  - Copy to clipboard  — snapshots the buffer into the system
//	    clipboard via ImGui::SetClipboardText / SDL3's Android bridge
//	  - Close              — dismisses the viewer
//
//	The capture function chains to whatever stderr/logcat writers were
//	registered earlier so existing platform sinks keep working.

#ifndef f_AT_DEBUG_LOG_H
#define f_AT_DEBUG_LOG_H

struct ATUIState;

// Install ATLog capture (hooks ATLogSetWriteCallbacks).  Call once at
// startup AFTER the existing stderr/logcat callbacks have been
// installed — this function snapshots the current callbacks and chains
// to them so all sinks continue to work.
void ATDebugLogInstall();

// Render the in-app debug log viewer.  Caller drives visibility via
// state.showDebugLog.  Modeled on the crash-report viewer: read-only
// InputTextMultiline with Copy / Close buttons.
void ATUIRenderDebugLogDialog(ATUIState &state);

#endif
