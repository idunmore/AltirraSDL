//	AltirraSDL - Firmware UI internal header
//	Shared between ui_firmware.cpp (manager dialog) and
//	ui_firmware_category.cpp (System Config firmware category).

#ifndef f_AT_UI_FIRMWARE_INTERNAL_H
#define f_AT_UI_FIRMWARE_INTERNAL_H

class ATSimulator;

// Defined in ui_firmware.cpp. Called by RenderFirmwareCategory() in
// ui_firmware_category.cpp when the user opens the Firmware Manager dialog
// from inside the System Configuration page.
void RenderFirmwareManager(ATSimulator &sim, bool &show);

#endif
