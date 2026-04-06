//	AltirraSDL - ImGui progress dialog
//	Replaces Windows ATUIProgressDialogW32 / ATUIProgressBackgroundTaskDialogW32.

#pragma once

// Initialize/shutdown the SDL3 progress handler (call from main_sdl3.cpp).
void ATUIInitProgressSDL3();
void ATUIShutdownProgressSDL3();

// Render the progress popup (call each frame from ATUIRenderFrame).
void ATUIRenderProgress();
