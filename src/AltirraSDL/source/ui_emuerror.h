//	AltirraSDL - ImGui emulation error dialog
//	Replaces Windows IDD_PROGRAM_ERROR / ATUIDialogEmuError.

#pragma once

class ATSimulator;

// Initialize/shutdown the error handler (call from main_sdl3.cpp).
void ATInitEmuErrorHandlerSDL3(ATSimulator *sim);
void ATShutdownEmuErrorHandlerSDL3();

// Render the error dialog popup (call each frame from ATUIRenderFrame).
void ATUIRenderEmuErrorDialog(ATSimulator &sim);
