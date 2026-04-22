//	AltirraSDL - Online Play emote fade overlay.
//
//	Single-slot overlay (2-player netplay) that fades in, holds, and
//	fades out the last-received emote over 4.0 seconds.  A new Show()
//	replaces the current emote and restarts the animation.

#pragma once

#include <stdint.h>

namespace ATEmoteOverlay {

// Trigger the overlay with icon [0..15].  Starts fade-in at nowMs.
void Show(int iconId, uint64_t nowMs);

// Immediately hide without animating out.  Called on netplay teardown.
void Clear();

// Render the overlay if active.  Safe to call every frame.  Positions
// the icon in the top-right of the main viewport.
void Render(uint64_t nowMs);

} // namespace ATEmoteOverlay
