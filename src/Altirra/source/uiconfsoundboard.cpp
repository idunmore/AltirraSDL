//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <at/atcore/propertyset.h>
#include "uiconfgeneric.h"

bool ATUIConfDevSoundBoard(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"SoundBoard Options",
		[](IATUIConfigView& view) {
			view.AddIntDropDown()
				.SetTag("version")
				.SetLabel(L"Core &version")
				.AddChoice(110, L"1.1 (VBXE-based)")
				.AddChoice(120, L"1.2 (with multiplier)")
				.AddChoice(200, L"2.0 Preview");

			view.AddVerticalSpace();

			view.AddIntDropDown()
				.SetTag("base")
				.SetLabel(L"&Base address")
				.AddChoice(0xD280, L"$D280")
				.AddChoice(0xD2C0, L"$D2C0")
				.AddChoice(0xD600, L"$D600")
				.AddChoice(0xD700, L"$D700");

			view.AddVerticalSpace();

			view.AddMultilineText(true)
				.SetValue(
					L"Note: If Ultimate1MB emulation is enabled, the above "
					L"setting will have no effect for V1.x cores. Instead, "
					L"the U1MB BIOS setting will switch the SoundBoard between "
					L"being disabled and $D2C0-D2FF."
				);
		}
	);
}
