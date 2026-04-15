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

bool ATUIConfDevSIDE3(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"SIDE 3 Options",
		[](IATUIConfigView& view) {
			view.AddIntDropDown()
				.SetLabel(L"&Hardware version")
				.SetTag("version")
				.AddChoice(10, L"SIDE 3 (JED 1.1: 2MB RAM)")
				.AddChoice(14, L"SIDE 3.1 (JED 1.4: 8MB RAM, enhanced DMA)");

			view.AddVerticalSpace();

			view.AddCheckbox()
				.SetLabel(L"Additional options")
				.SetTag("led_enable")
				.SetText(L"Show activity &LED")
				.SetDefault(true, true);

			view.AddCheckbox()
				.SetTag("recovery")
				.SetText(L"&Recovery mode (hold button on power-up)");
		}
	);
}
