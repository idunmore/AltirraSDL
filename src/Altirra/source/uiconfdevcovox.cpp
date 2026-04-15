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

bool ATUIConfDevCovox(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"Covox Options",
		[](IATUIConfigView& view) {
			auto& ba = view.AddCustomDropDown()
				.SetLabel(L"&Base Address");

			static constexpr uint16 kAddrRanges[][4] = {
				{ 0xD100, 0x100, 0, 0x100 },
				{ 0xD280, 0x80, 0, 0x100, },
				{ 0xD500, 0x100, 0, 0x100 },
				{ 0xD600, 0x40, 0, 0xFF },
				{ 0xD600, 0x100, 0x100, 0x100 },		// also the default
				{ 0xD700, 0x100, 0, 0x100 },
			};

			VDStringW s;
			for(const auto& [base, size, minSize, maxSize] : kAddrRanges) {
				s.sprintf(L"$%04X-%04X", base, base + size - 1);

				ba.AddChoice(
					s.c_str(),
					[=](const ATPropertySet& pset) -> bool {
						const uint32 baseProp = pset.GetUint32("base", 0xD600);
						uint32 sizeProp = pset.GetUint32("size", 0);

						if (!sizeProp)
							sizeProp = 0x100;

						if (sizeProp > 0x100)
							sizeProp = 0x100;

						return baseProp == base && sizeProp >= minSize && sizeProp <= maxSize;
					},
					[=](ATPropertySet& pset) {
						pset.SetUint32("base", base);
						pset.SetUint32("size", size);

					}
				);
			}

			view.AddVerticalSpace();

			view.AddIntDropDown()
				.SetLabel(L"&Channels")
				.SetTag("channels")
				.AddChoice(1, L"1 channel (mono)")
				.AddChoice(4, L"4 channels (stereo)")
				.SetValue(4);
		}
	);
}
