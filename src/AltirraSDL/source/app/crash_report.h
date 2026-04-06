//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 frontend - crash report persistence & viewer
//
//	Two-layer mechanism for surfacing fatal errors to the user:
//
//	  1) ATCrashReportWrite() — called from ATReportFatal in main_sdl3.cpp
//	     at the moment the exception is caught.  Writes a plain-text report
//	     to <configdir>/last_crash.txt so the next launch can display it,
//	     even if the process dies before any UI comes up.
//
//	  2) ATCrashReportLoadPrevious() — called from main() during startup,
//	     before the main loop.  If last_crash.txt exists, reads it into
//	     an in-memory buffer and renames the file to last_crash.txt.seen
//	     so it is shown at most once.  Subsequent launches see nothing.
//
//	  3) ATCrashReportRender() — called from ATUIRenderFrame each frame.
//	     If a report is pending, opens an ImGui window with the text in a
//	     read-only multiline input (naturally selectable + system clipboard
//	     aware on both desktop and Android) and a one-tap "Copy to clipboard"
//	     button.  Dismissing the window clears the pending report for the
//	     remainder of the session.
//
//	Intentionally uses plain C FILE* for the write side so the writer can
//	run from inside a catch block without depending on anything that might
//	itself have thrown (streams, VDFile, the registry, ImGui, SDL video).

#ifndef AT_CRASH_REPORT_H
#define AT_CRASH_REPORT_H

// Append a fatal error report to <configdir>/last_crash.txt.
// Safe to call from inside a catch block.  Best-effort — failures are
// silent (there is nothing useful to do if we cannot even write a file).
// `phase` and `message` may be null.
void ATCrashReportWrite(const char *phase, const char *message);

// If <configdir>/last_crash.txt exists from a previous session, load its
// contents into an in-memory buffer and rename the file so it is shown
// at most once.  Call once at startup, before the main loop.
void ATCrashReportLoadPrevious();

// Inject a crash report for the current session (e.g. from an exception
// caught in the main loop after ImGui is up).  Makes it visible via the
// same viewer as a previous-session report.
void ATCrashReportShowNow(const char *phase, const char *message);

// Render the crash report viewer window if a report is pending.  No-op
// otherwise.  Must be called between ImGui::NewFrame and ImGui::Render,
// i.e. inside the normal per-frame UI rendering pass.
void ATCrashReportRender();

#endif
