//	AltirraSDL - in-app debug log capture & viewer (impl).
//	See debug_log.h for design notes.

#include <stdafx.h>
#include "debug_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <mutex>

#include <imgui.h>
#include <SDL3/SDL.h>

#include <vd2/system/VDString.h>
#include <at/atcore/logging.h>

#include "ui_main.h"

namespace {

// Ring buffer.  Sized for ~256 KB of text — large enough to keep an
// entire netplay session's NETPLAY-channel chatter (~50 lines per
// session × a few hundred bytes per line) plus headroom for whatever
// other channels the user has enabled.  When the buffer fills we drop
// the oldest complete line(s) — never split a line.
constexpr size_t kRingCapacity = 256 * 1024;

struct LogRing {
	std::mutex mu;
	VDStringA  buf;             // monotonically grown, then trimmed
	size_t     totalLines = 0;
	size_t     droppedLines = 0;
};

LogRing& Ring() {
	static LogRing r;
	return r;
}

// Previously-installed callbacks.  We chain to them so the existing
// stderr / logcat sinks keep working.  Captured once in
// ATDebugLogInstall().
ATLogWriteFn  s_chainWrite  = nullptr;
ATLogWriteVFn s_chainWriteV = nullptr;

// Per-channel timestamp prefix.  The user asked for sub-second relative
// timestamps on every NETPLAY log line so the post-Accept handshake /
// snapshot transfer phases can be timed without having to add manual
// sprintfs at every g_ATLCNetplay call site.  Other channels stay
// untouched — those are emulator-internal and already noisy enough.
//
// Reference: app start (first call to FormatRelTimestamp).  Format:
// "[T+sss.mmm] " — seconds and milliseconds since reference.  The
// six-character seconds field is enough for a 27-hour session before
// the field overflows to seven characters; nothing breaks past that,
// the column just widens.
bool ChannelWantsTimestamp(const ATLogChannel *ch) {
	if (!ch) return false;
	const char *name = ch->GetName();
	return name && strcmp(name, "NETPLAY") == 0;
}

void FormatRelTimestamp(char *out, size_t cap) {
	static uint64_t s_startMs = 0;
	const uint64_t now = (uint64_t)SDL_GetTicks();
	if (s_startMs == 0) s_startMs = now;
	const uint64_t rel = now - s_startMs;
	const uint64_t sec = rel / 1000;
	const uint32_t ms  = (uint32_t)(rel % 1000);
	std::snprintf(out, cap, "[T+%llu.%03u] ",
		(unsigned long long)sec, ms);
}

void Append(const char *channelName, const char *text) {
	if (!text) text = "";

	LogRing& r = Ring();
	std::lock_guard<std::mutex> lock(r.mu);

	r.buf.append_sprintf("[%s] %s\n", channelName ? channelName : "?", text);
	++r.totalLines;

	// Trim from the front in line-aligned chunks once we exceed the
	// capacity.  Reserve ~25% headroom so trims aren't per-line.
	if (r.buf.size() > kRingCapacity) {
		size_t want = kRingCapacity * 3 / 4;
		size_t cut = r.buf.size() - want;
		// Snap to next newline so we never split a line.
		const char *p = r.buf.c_str();
		size_t i = cut;
		while (i < r.buf.size() && p[i] != '\n') ++i;
		if (i < r.buf.size()) ++i;  // skip the \n itself
		// Count dropped lines for the header.
		size_t droppedNow = 0;
		for (size_t k = 0; k < i; ++k)
			if (p[k] == '\n') ++droppedNow;
		r.droppedLines += droppedNow;
		r.buf.erase(0, i);
	}
}

void CaptureWrite(ATLogChannel *ch, const char *s) {
	const char *name = ch ? ch->GetName() : nullptr;
	if (ChannelWantsTimestamp(ch)) {
		char ts[32];
		FormatRelTimestamp(ts, sizeof ts);
		VDStringA pref;
		pref.append_sprintf("%s%s", ts, s ? s : "");
		Append(name, pref.c_str());
		// Bypass the stderr chain — it doesn't know about the prefix
		// and would emit the un-stamped line.  Inline the stderr
		// write instead.
		std::fprintf(stderr, "[%s] %s\n", name ? name : "?", pref.c_str());
		return;
	}
	Append(name, s);
	if (s_chainWrite)
		s_chainWrite(ch, s);
}

void CaptureWriteV(ATLogChannel *ch, const char *fmt, va_list ap) {
	// Format once into a stack buffer, fall back to heap on overflow.
	char stack[1024];
	va_list ap_copy;
	va_copy(ap_copy, ap);
	int n = vsnprintf(stack, sizeof stack, fmt, ap_copy);
	va_end(ap_copy);

	const char *name = ch ? ch->GetName() : nullptr;
	const bool wantTs = ChannelWantsTimestamp(ch);
	char ts[32] = "";
	if (wantTs) FormatRelTimestamp(ts, sizeof ts);

	auto deliver = [&](const char *line) {
		if (wantTs) {
			VDStringA pref;
			pref.append_sprintf("%s%s", ts, line ? line : "");
			Append(name, pref.c_str());
			// Stderr inline (bypass the un-stamped chain).
			std::fprintf(stderr, "[%s] %s\n",
				name ? name : "?", pref.c_str());
		} else {
			Append(name, line);
		}
	};

	if (n < 0) {
		// Encoding error — record the raw format string so the user
		// at least sees something landed.
		deliver(fmt ? fmt : "(vformat error)");
	} else if ((size_t)n < sizeof stack) {
		deliver(stack);
	} else {
		VDStringA big;
		big.resize((size_t)n + 1);
		va_copy(ap_copy, ap);
		vsnprintf(&big[0], big.size(), fmt, ap_copy);
		va_end(ap_copy);
		big.resize((size_t)n);
		deliver(big.c_str());
	}

	// Chain so stderr / logcat still sees the line — but only when we
	// haven't already written it inline above (NETPLAY case).
	if (!wantTs && s_chainWriteV)
		s_chainWriteV(ch, fmt, ap);
}

VDStringA Snapshot() {
	LogRing& r = Ring();
	std::lock_guard<std::mutex> lock(r.mu);
	VDStringA copy;
	if (r.droppedLines > 0) {
		copy.append_sprintf(
			"[debug-log] (%zu earlier lines dropped from ring)\n",
			r.droppedLines);
	}
	copy += r.buf;
	if (copy.empty())
		copy = "(no log lines captured yet)\n";
	return copy;
}

} // namespace

void ATDebugLogInstall() {
	// We rely on the existing main_sdl3.cpp call to ATLogSetWriteCallbacks
	// having registered stderr writers BEFORE we run.  The atcore logging
	// API doesn't expose getters for the current callbacks, so we
	// re-implement the stderr sink locally and chain to it from our
	// capture callbacks — that way the user still sees logs in the
	// terminal AND in the in-app viewer.
	s_chainWrite = [](ATLogChannel *ch, const char *s) {
		std::fprintf(stderr, "[%s] %s\n",
			ch ? ch->GetName() : "?", s ? s : "");
	};
	s_chainWriteV = [](ATLogChannel *ch, const char *fmt, va_list ap) {
		std::fprintf(stderr, "[%s] ", ch ? ch->GetName() : "?");
		std::vfprintf(stderr, fmt, ap);
		std::fputc('\n', stderr);
	};

	ATLogSetWriteCallbacks(&CaptureWrite, &CaptureWriteV);
}

void ATUIRenderDebugLogDialog(ATUIState &state) {
	if (!state.showDebugLog)
		return;

	ImGuiIO& io = ImGui::GetIO();

	// Cap to display so this works on phones too.  Same sizing
	// strategy as the crash-report viewer.
	const float maxW = io.DisplaySize.x * 0.95f;
	const float maxH = io.DisplaySize.y * 0.85f;
	float w = 760.0f, h = 540.0f;
	if (w > maxW) w = maxW;
	if (h > maxH) h = maxH;

	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(
		ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Debug Log", &state.showDebugLog,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose())
		state.showDebugLog = false;

	// Snapshot once per frame so the multiline input has a stable
	// buffer to render against — without this the buffer can grow
	// while ImGui is iterating and cause selection / scroll glitches.
	static VDStringA s_snapshot;
	s_snapshot = Snapshot();

	ImGui::TextWrapped(
		"Captures log output from this session "
		"(NETPLAY, audio, disk, etc.).  Useful when there's no "
		"terminal to read stderr from — typically on Android.");
	ImGui::Separator();

	if (ImGui::Button("Copy to clipboard")) {
		// Goes through ImGui's PlatformIO hook, which on WASM is
		// overridden in ui_main.cpp to call ATCopyTextToClipboard ->
		// navigator.clipboard.writeText (the deprecated execCommand
		// path SDL3 uses on WASM is silently broken in modern
		// browsers).  Native builds still hit SDL_SetClipboardText
		// underneath, same as before.
		ImGui::SetClipboardText(s_snapshot.c_str());
	}
	ImGui::SameLine();
	if (ImGui::Button("Close")) {
		state.showDebugLog = false;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(%zu bytes)", s_snapshot.size());

	ImGui::Separator();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	// Read-only multiline so the user can select / copy a portion via
	// long-press on Android or click-drag on desktop.  The buffer is
	// const-cast since ImGui's API takes char* but we pass the
	// ReadOnly flag.  ImGui's InputTextMultiline keeps the scroll
	// position pinned to the bottom while the caret is at the end of
	// the text — which is its initial state on first open and after
	// any user-initiated "End" jump — so newly-arrived log lines
	// appear without manual intervention.
	ImGui::InputTextMultiline("##debuglog",
		const_cast<char*>(s_snapshot.c_str()),
		s_snapshot.size() + 1,
		avail,
		ImGuiInputTextFlags_ReadOnly);

	ImGui::End();
}
