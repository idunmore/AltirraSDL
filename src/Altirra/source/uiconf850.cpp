//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <at/atcore/propertyset.h>
#include "uiconfgeneric.h"

bool ATUIConfDev850(VDGUIHandle h, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(h,
		props,
		L"850 Interface Module (full emulation) Options",
		[](IATUIConfigView& view) {
			view.AddCheckbox()
				.SetLabel(L"Baud rate")
				.SetTag("baudex")
				.SetDefault(false)
				.SetText(L"Enable e&xtended baud rate support (57600, 115K, 230K baud)");

			view.AddCheckbox()
				.SetTag("unthrottled")
				.SetText(L"Disable serial port t&hrottling")
				.SetDefault(false);

			view.AddVerticalSpace();

			view.AddIntDropDown()
				.SetLabel(L"SIO emulation")
				.SetTag("emulevel")
				.AddChoice(0, L"None - Emulated R: handler only")
				.AddChoice(1, L"Minimal - Emulated R: handler + stub loader only")
				.AddChoice(2, L"Full - SIO protocol and 6502 R: handler")
				.SetValue(0);
		}
	);
}

////////////////////////////////////////////////////////////////////////////////

bool ATUIConfDev850Full(VDGUIHandle h, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(h,
		props,
		L"850 Interface Module (full emulation) Options",
		[](IATUIConfigView& view) {
			const auto setBaudRates = [](IATUIConfigIntDropDownView& dropDownView) -> IATUIConfigIntDropDownView& {
				return dropDownView
					.AddChoice(0, L"Auto")
					.AddChoice(2, L"45.5 baud")
					.AddChoice(3, L"50 baud")
					.AddChoice(4, L"56.875 baud")
					.AddChoice(5, L"75 baud")
					.AddChoice(6, L"110 baud")
					.AddChoice(7, L"134.5 baud")
					.AddChoice(8, L"150 baud")
					.AddChoice(1, L"300 baud")
					.AddChoice(10, L"600 baud")
					.AddChoice(11, L"1200 baud")
					.AddChoice(12, L"1800 baud")
					.AddChoice(13, L"2400 baud")
					.AddChoice(14, L"4800 baud")
					.AddChoice(15, L"9600 baud");
			};

			setBaudRates(view.AddIntDropDown())
				.SetTag("serbaud1")
				.SetLabel(L"Port 1 baud rate");

			setBaudRates(view.AddIntDropDown())
				.SetTag("serbaud2")
				.SetLabel(L"Port 2 baud rate");

			setBaudRates(view.AddIntDropDown())
				.SetTag("serbaud3")
				.SetLabel(L"Port 3 baud rate");

			setBaudRates(view.AddIntDropDown())
				.SetTag("serbaud4")
				.SetLabel(L"Port 4 baud rate");
		}
	);
}
