//	Altirra SDL3 frontend - input handling
//
//	Keyboard input has two paths:
//
//	1. POKEY direct path: SDL scancode → Atari KBCODE → PushKey()
//	   This handles actual Atari keyboard typing (letters, numbers, etc.)
//
//	2. ATInputManager path: SDL scancode → ATInputCode (VK equivalent)
//	   → ATInputManager::OnButtonDown/Up()
//	   This handles input mapping — arrow keys as joystick, function keys
//	   as console buttons, etc.  The input map system routes these to
//	   emulated Atari controllers (joystick, paddle, 5200, etc.)
//
//	The Windows version (uivideodisplaywindow.cpp) sends VK codes through
//	ATInputManager for the controller mapping path.  We match that by
//	translating SDL scancodes to the same VK code values (ATInputCode).

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <at/ataudio/pokey.h>
#include "inputmanager.h"
#include "inputdefs.h"

// -------------------------------------------------------------------------
// SDL scancode → Atari KBCODE mapping (POKEY direct path)
// Reference: Atari 800 Hardware Reference Manual, Chapter 3
// -------------------------------------------------------------------------

// Returns 0xFF if not mapped
static uint8 SDLScancodeToAtari(SDL_Scancode sc, bool shift, bool ctrl) {
	switch (sc) {
	// Row 0: digits
	case SDL_SCANCODE_1:       return 0x1F;
	case SDL_SCANCODE_2:       return 0x1E;
	case SDL_SCANCODE_3:       return 0x1A;
	case SDL_SCANCODE_4:       return 0x18;
	case SDL_SCANCODE_5:       return 0x1D;
	case SDL_SCANCODE_6:       return 0x1B;
	case SDL_SCANCODE_7:       return 0x33;
	case SDL_SCANCODE_8:       return 0x35;
	case SDL_SCANCODE_9:       return 0x30;
	case SDL_SCANCODE_0:       return 0x32;
	case SDL_SCANCODE_MINUS:   return 0x38;
	case SDL_SCANCODE_EQUALS:  return 0x3A;

	// Row 1: QWERTYUIOP
	case SDL_SCANCODE_Q:       return 0x2F;
	case SDL_SCANCODE_W:       return 0x2E;
	case SDL_SCANCODE_E:       return 0x2A;
	case SDL_SCANCODE_R:       return 0x28;
	case SDL_SCANCODE_T:       return 0x2D;
	case SDL_SCANCODE_Y:       return 0x2B;
	case SDL_SCANCODE_U:       return 0x0B;
	case SDL_SCANCODE_I:       return 0x0D;
	case SDL_SCANCODE_O:       return 0x08;
	case SDL_SCANCODE_P:       return 0x0A;
	case SDL_SCANCODE_LEFTBRACKET:  return 0x60;
	case SDL_SCANCODE_RIGHTBRACKET: return 0x60;

	// Row 2: ASDFGHJKL
	case SDL_SCANCODE_A:       return 0x3F;
	case SDL_SCANCODE_S:       return 0x3E;
	case SDL_SCANCODE_D:       return 0x3A;
	case SDL_SCANCODE_F:       return 0x38;
	case SDL_SCANCODE_G:       return 0x3D;
	case SDL_SCANCODE_H:       return 0x39;
	case SDL_SCANCODE_J:       return 0x01;
	case SDL_SCANCODE_K:       return 0x05;
	case SDL_SCANCODE_L:       return 0x00;
	case SDL_SCANCODE_SEMICOLON:   return 0x02;
	case SDL_SCANCODE_APOSTROPHE:  return 0x02;

	// Row 3: ZXCVBNM
	case SDL_SCANCODE_Z:       return 0x17;
	case SDL_SCANCODE_X:       return 0x16;
	case SDL_SCANCODE_C:       return 0x12;
	case SDL_SCANCODE_V:       return 0x10;
	case SDL_SCANCODE_B:       return 0x15;
	case SDL_SCANCODE_N:       return 0x23;
	case SDL_SCANCODE_M:       return 0x25;
	case SDL_SCANCODE_COMMA:   return 0x20;
	case SDL_SCANCODE_PERIOD:  return 0x22;
	case SDL_SCANCODE_SLASH:   return 0x26;

	// Special keys
	case SDL_SCANCODE_SPACE:   return 0x21;
	case SDL_SCANCODE_RETURN:  return 0x0C;
	case SDL_SCANCODE_BACKSPACE: return 0x34;
	case SDL_SCANCODE_TAB:     return 0x2C;
	case SDL_SCANCODE_ESCAPE:  return 0x1C;

	// Function keys → Atari function keys
	case SDL_SCANCODE_F1:      return 0x03;
	case SDL_SCANCODE_F2:      return 0x04;
	case SDL_SCANCODE_F3:      return 0x06;
	case SDL_SCANCODE_F5:      return 0x14;
	case SDL_SCANCODE_DELETE:  return 0x34;

	case SDL_SCANCODE_CAPSLOCK: return 0x3C;

	default: return 0xFF;
	}
}

// -------------------------------------------------------------------------
// SDL scancode → ATInputCode (Windows VK equivalent) for input mapping
//
// ATInputCode values are Windows VK codes.  This table maps SDL3 physical
// scancodes to the same VK values so that Altirra's input maps (which
// reference VK codes) work identically on SDL3.
// -------------------------------------------------------------------------

static uint32 SDLScancodeToInputCode(SDL_Scancode sc) {
	switch (sc) {
	// Letters
	case SDL_SCANCODE_A: return kATInputCode_KeyA;
	case SDL_SCANCODE_B: return kATInputCode_KeyB;
	case SDL_SCANCODE_C: return kATInputCode_KeyC;
	case SDL_SCANCODE_D: return kATInputCode_KeyD;
	case SDL_SCANCODE_E: return kATInputCode_KeyE;
	case SDL_SCANCODE_F: return kATInputCode_KeyF;
	case SDL_SCANCODE_G: return kATInputCode_KeyG;
	case SDL_SCANCODE_H: return kATInputCode_KeyH;
	case SDL_SCANCODE_I: return kATInputCode_KeyI;
	case SDL_SCANCODE_J: return kATInputCode_KeyJ;
	case SDL_SCANCODE_K: return kATInputCode_KeyK;
	case SDL_SCANCODE_L: return kATInputCode_KeyL;
	case SDL_SCANCODE_M: return kATInputCode_KeyM;
	case SDL_SCANCODE_N: return kATInputCode_KeyN;
	case SDL_SCANCODE_O: return kATInputCode_KeyO;
	case SDL_SCANCODE_P: return kATInputCode_KeyP;
	case SDL_SCANCODE_Q: return kATInputCode_Keyq;
	case SDL_SCANCODE_R: return kATInputCode_KeyR;
	case SDL_SCANCODE_S: return kATInputCode_KeyS;
	case SDL_SCANCODE_T: return kATInputCode_KeyT;
	case SDL_SCANCODE_U: return kATInputCode_KeyU;
	case SDL_SCANCODE_V: return kATInputCode_KeyV;
	case SDL_SCANCODE_W: return kATInputCode_KeyW;
	case SDL_SCANCODE_X: return kATInputCode_KeyX;
	case SDL_SCANCODE_Y: return kATInputCode_KeyY;
	case SDL_SCANCODE_Z: return kATInputCode_KeyZ;

	// Digits
	case SDL_SCANCODE_0: return kATInputCode_Key0;
	case SDL_SCANCODE_1: return kATInputCode_Key1;
	case SDL_SCANCODE_2: return kATInputCode_Key2;
	case SDL_SCANCODE_3: return kATInputCode_Key3;
	case SDL_SCANCODE_4: return kATInputCode_Key4;
	case SDL_SCANCODE_5: return kATInputCode_Key5;
	case SDL_SCANCODE_6: return kATInputCode_Key6;
	case SDL_SCANCODE_7: return kATInputCode_Key7;
	case SDL_SCANCODE_8: return kATInputCode_Key8;
	case SDL_SCANCODE_9: return kATInputCode_Key9;

	// Navigation
	case SDL_SCANCODE_LEFT:  return kATInputCode_KeyLeft;
	case SDL_SCANCODE_UP:    return kATInputCode_KeyUp;
	case SDL_SCANCODE_RIGHT: return kATInputCode_KeyRight;
	case SDL_SCANCODE_DOWN:  return kATInputCode_KeyDown;
	case SDL_SCANCODE_HOME:  return kATInputCode_KeyHome;
	case SDL_SCANCODE_END:   return kATInputCode_KeyEnd;
	case SDL_SCANCODE_PAGEUP:   return kATInputCode_KeyPrior;
	case SDL_SCANCODE_PAGEDOWN: return kATInputCode_KeyNext;
	case SDL_SCANCODE_INSERT: return kATInputCode_KeyInsert;
	case SDL_SCANCODE_DELETE: return kATInputCode_KeyDelete;

	// Common keys
	case SDL_SCANCODE_RETURN:    return kATInputCode_KeyReturn;
	case SDL_SCANCODE_ESCAPE:    return kATInputCode_KeyEscape;
	case SDL_SCANCODE_BACKSPACE: return kATInputCode_KeyBack;
	case SDL_SCANCODE_TAB:       return kATInputCode_KeyTab;
	case SDL_SCANCODE_SPACE:     return kATInputCode_KeySpace;

	// Function keys
	case SDL_SCANCODE_F1:  return kATInputCode_KeyF1;
	case SDL_SCANCODE_F2:  return kATInputCode_KeyF2;
	case SDL_SCANCODE_F3:  return kATInputCode_KeyF3;
	case SDL_SCANCODE_F4:  return kATInputCode_KeyF4;
	case SDL_SCANCODE_F5:  return kATInputCode_KeyF5;
	case SDL_SCANCODE_F6:  return kATInputCode_KeyF6;
	case SDL_SCANCODE_F7:  return kATInputCode_KeyF7;
	case SDL_SCANCODE_F8:  return kATInputCode_KeyF8;
	case SDL_SCANCODE_F9:  return kATInputCode_KeyF9;
	case SDL_SCANCODE_F10: return kATInputCode_KeyF10;
	case SDL_SCANCODE_F11: return kATInputCode_KeyF11;
	case SDL_SCANCODE_F12: return kATInputCode_KeyF12;

	// Numpad
	case SDL_SCANCODE_KP_0: return kATInputCode_KeyNumpad0;
	case SDL_SCANCODE_KP_1: return kATInputCode_KeyNumpad1;
	case SDL_SCANCODE_KP_2: return kATInputCode_KeyNumpad2;
	case SDL_SCANCODE_KP_3: return kATInputCode_KeyNumpad3;
	case SDL_SCANCODE_KP_4: return kATInputCode_KeyNumpad4;
	case SDL_SCANCODE_KP_5: return kATInputCode_KeyNumpad5;
	case SDL_SCANCODE_KP_6: return kATInputCode_KeyNumpad6;
	case SDL_SCANCODE_KP_7: return kATInputCode_KeyNumpad7;
	case SDL_SCANCODE_KP_8: return kATInputCode_KeyNumpad8;
	case SDL_SCANCODE_KP_9: return kATInputCode_KeyNumpad9;
	case SDL_SCANCODE_KP_MULTIPLY: return kATInputCode_KeyMultiply;
	case SDL_SCANCODE_KP_PLUS:     return kATInputCode_KeyAdd;
	case SDL_SCANCODE_KP_MINUS:    return kATInputCode_KeySubtract;
	case SDL_SCANCODE_KP_PERIOD:   return kATInputCode_KeyDecimal;
	case SDL_SCANCODE_KP_DIVIDE:   return kATInputCode_KeyDivide;
	case SDL_SCANCODE_KP_ENTER:    return kATInputCode_KeyNumpadEnter;

	// Modifiers
	case SDL_SCANCODE_LSHIFT: return kATInputCode_KeyLShift;
	case SDL_SCANCODE_RSHIFT: return kATInputCode_KeyRShift;
	case SDL_SCANCODE_LCTRL:  return kATInputCode_KeyLControl;
	case SDL_SCANCODE_RCTRL:  return kATInputCode_KeyRControl;

	// Punctuation (OEM keys)
	case SDL_SCANCODE_SEMICOLON:    return kATInputCode_KeyOem1;
	case SDL_SCANCODE_EQUALS:       return kATInputCode_KeyOemPlus;
	case SDL_SCANCODE_COMMA:        return kATInputCode_KeyOemComma;
	case SDL_SCANCODE_MINUS:        return kATInputCode_KeyOemMinus;
	case SDL_SCANCODE_PERIOD:       return kATInputCode_KeyOemPeriod;
	case SDL_SCANCODE_SLASH:        return kATInputCode_KeyOem2;
	case SDL_SCANCODE_GRAVE:        return kATInputCode_KeyOem3;
	case SDL_SCANCODE_LEFTBRACKET:  return kATInputCode_KeyOem4;
	case SDL_SCANCODE_BACKSLASH:    return kATInputCode_KeyOem5;
	case SDL_SCANCODE_RIGHTBRACKET: return kATInputCode_KeyOem6;
	case SDL_SCANCODE_APOSTROPHE:   return kATInputCode_KeyOem7;

	default: return kATInputCode_None;
	}
}

// -------------------------------------------------------------------------
// Input state
// -------------------------------------------------------------------------

struct ATInputStateSDL3 {
	ATPokeyEmulator *mpPokey = nullptr;
	ATInputManager *mpInputManager = nullptr;
};

static ATInputStateSDL3 g_inputState;

void ATInputSDL3_Init(ATPokeyEmulator *pokey, ATInputManager *inputMgr) {
	g_inputState.mpPokey = pokey;
	g_inputState.mpInputManager = inputMgr;
}

void ATInputSDL3_HandleKeyDown(const SDL_KeyboardEvent& ev) {
	// Path 1: Route through ATInputManager for input mapping
	// (arrow keys as joystick, etc.)
	if (g_inputState.mpInputManager) {
		uint32 inputCode = SDLScancodeToInputCode(ev.scancode);
		if (inputCode != kATInputCode_None)
			g_inputState.mpInputManager->OnButtonDown(0, inputCode);
	}

	// Path 2: Direct POKEY path for Atari keyboard typing
	if (!g_inputState.mpPokey) return;

	bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
	bool ctrl  = (ev.mod & SDL_KMOD_CTRL) != 0;

	// Handle Break
	if (ev.scancode == SDL_SCANCODE_PAUSE || ev.scancode == SDL_SCANCODE_F8) {
		g_inputState.mpPokey->PushBreak();
		return;
	}

	uint8 atariCode = SDLScancodeToAtari(ev.scancode, shift, ctrl);
	if (atariCode == 0xFF) return;

	if (ctrl)  atariCode |= 0x80;
	if (shift) atariCode |= 0x40;

	g_inputState.mpPokey->PushKey(atariCode, false, true, false, true);
}

void ATInputSDL3_HandleKeyUp(const SDL_KeyboardEvent& ev) {
	// Route key release through ATInputManager
	if (g_inputState.mpInputManager) {
		uint32 inputCode = SDLScancodeToInputCode(ev.scancode);
		if (inputCode != kATInputCode_None)
			g_inputState.mpInputManager->OnButtonUp(0, inputCode);
	}
}
