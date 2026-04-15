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

bool ATUIConfDevDongle(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"Joystick dongle options",
		[](IATUIConfigView& view) {
			view.AddIntDropDown()
				.SetTag("port")
				.SetLabel(L"&Controller port")
				.AddChoice(0, L"Port 1")
				.AddChoice(1, L"Port 2")
				.AddChoice(2, L"Port 3 (400/800 only)")
				.AddChoice(3, L"Port 4 (400/800 only)");

			view.AddVerticalSpace();

			auto& mappingView = view.AddStringEdit()
				.SetTag("mapping")
				.SetLabel(L"&Mapping")
				.SetDefault(L"FFFFFFFFFFFFFFFF", true);

			view.AddVerticalSpace();

			view.AddMultilineText(true)
				.SetValue(
					L"The mapping determines the dongle's 4-bit response to each possible "
					L"4-bit output from the joystick port."
				);

			view.SetCustomWrite(
				[&](ATPropertySet& pset) {
					VDStringW mappingStr(mappingView.GetValue());

					// trim the string
					size_t len1 = 0;
					size_t len2 = mappingStr.size();

					while(len2 > len1 && iswspace(mappingStr[len2 - 1]))
						--len2;

					while(len1 < len2 && iswspace(mappingStr[len1]))
						++len1;

					mappingStr.erase(mappingStr.begin() + len2, mappingStr.end());
					mappingStr.erase(mappingStr.begin(), mappingStr.begin() + len1);

					// convert to uppercase
					std::transform(mappingStr.begin(), mappingStr.end(), mappingStr.begin(), ::towupper);

					// check if it's the wrong length or has non-hex digits
					if (len2 - len1 != 16 || std::find_if_not(mappingStr.begin(), mappingStr.end(), iswxdigit) != mappingStr.end()) {
						throw ATUIGenericConfigValidationException(
							&mappingView,
							L"The mapping string must be a set of 16 hexadecimal digits."
						);
					}

					pset.SetString("mapping", mappingStr.c_str());
				}
			);
		}
	);
}
