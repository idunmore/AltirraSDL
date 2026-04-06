//	AltirraSDL - Keyboard Customize dialog internal header
//	Data tables and formatting helpers shared between
//	ui_keyboard_customize.cpp (dialog + import/export) and
//	ui_keyboard_customize_tables.cpp (name tables + VKey data).

#ifndef f_AT_UI_KEYBOARD_CUSTOMIZE_INTERNAL_H
#define f_AT_UI_KEYBOARD_CUSTOMIZE_INTERNAL_H

#include <vd2/system/vdtypes.h>
#include <imgui.h>
#include <cstddef>

struct ImGuiKeyToVK {
	ImGuiKey key;
	uint32 vk;
};

// Atari-side scan code → human-readable name (lookup on 8-bit key code).
const char *GetNameForKeyCode(uint32 c);

// Host-side VKey → human-readable name ("A", "Return", "F1", ...).
const char *GetVKName(uint32 vk);

// Format the host-side key portion of a key mapping entry ("Ctrl+Shift+X").
void FormatMappingHostKey(uint32 mapping, char *buf, size_t bufSize);

// True if `c` is a valid Atari-side scan code (name table lookup).
bool ATIsValidScanCodeLocal(uint32 c);

// True if `vk` is one of the extended virtual keys that should be
// flagged as "Ext" when formatting a key mapping string.
bool IsExtendedVK(uint32 vk);

// Tables exposed from ui_keyboard_customize_tables.cpp.
extern const uint32 kScanCodeTable[];
extern const int kScanCodeTableSize;
extern const ImGuiKeyToVK kImGuiKeyMap[];
extern const int kImGuiKeyMapSize;

#endif
