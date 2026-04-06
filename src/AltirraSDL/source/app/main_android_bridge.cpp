//	AltirraSDL - Android stderr → logcat bridge
//	Split out of main_sdl3.cpp (Phase 2e).
//
//	Android's C runtime does NOT forward stderr (or stdout) to logcat: any
//	fprintf(stderr, ...) call vanishes into the void unless we arrange for
//	it to be read. The existing logging.h macros (LOG_INFO / LOG_WARN /
//	LOG_ERROR) and many raw fprintf sites across the codebase all write to
//	stderr, so on Android we capture it by dup2'ing stderr onto a pipe and
//	spawning a reader thread that splits on newlines and forwards each line
//	to __android_log_write under the "AltirraSDL" tag.
//
//	This makes the entire existing diagnostic surface visible in
//	    adb logcat -s AltirraSDL SDL
//	with zero changes to any call site. Desktop is untouched.

#ifdef __ANDROID__

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>

namespace {

int s_stderrPipeRead = -1;

void *ATAndroidStderrReaderThread(void *) {
	char line[512];
	size_t pos = 0;
	for (;;) {
		ssize_t n = read(s_stderrPipeRead, line + pos, sizeof line - 1 - pos);
		if (n <= 0) {
			// EOF or error — drain what we have and stop.
			if (pos > 0) {
				line[pos] = 0;
				__android_log_write(ANDROID_LOG_INFO, "AltirraSDL", line);
			}
			return nullptr;
		}
		pos += (size_t)n;
		// Emit complete lines.
		size_t lineStart = 0;
		for (size_t i = 0; i < pos; ++i) {
			if (line[i] == '\n') {
				line[i] = 0;
				__android_log_write(ANDROID_LOG_INFO, "AltirraSDL", line + lineStart);
				lineStart = i + 1;
			}
		}
		// Shift any partial trailing line to the front.
		if (lineStart > 0 && lineStart < pos) {
			memmove(line, line + lineStart, pos - lineStart);
			pos -= lineStart;
		} else if (lineStart == pos) {
			pos = 0;
		} else if (pos >= sizeof line - 1) {
			// Pathologically long line — flush it and reset.
			line[sizeof line - 1] = 0;
			__android_log_write(ANDROID_LOG_INFO, "AltirraSDL", line);
			pos = 0;
		}
	}
}

} // namespace

void ATAndroidInstallStderrBridge() {
	int fds[2];
	if (pipe(fds) != 0)
		return;
	// Disable any buffering on stderr so messages land promptly.
	setvbuf(stderr, nullptr, _IOLBF, 0);
	// Redirect both stdout and stderr onto the pipe write end.
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	close(fds[1]);
	s_stderrPipeRead = fds[0];

	pthread_t tid;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&tid, &attr, ATAndroidStderrReaderThread, nullptr);
	pthread_attr_destroy(&attr);

	__android_log_write(ANDROID_LOG_INFO, "AltirraSDL",
		"stderr → logcat bridge installed");
}

#endif // __ANDROID__
