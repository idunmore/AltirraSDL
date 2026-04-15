//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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

bool ATUIConfDevKMKJZIDE2(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"KMK/JZ IDE Options",
		[](IATUIConfigView& view) {
			view.AddCustomDropDown()
				.SetLabel(L"&Revision")
				.SetDefault(4)
				.AddChoice(
					L"Rev. C",
					[](const ATPropertySet& pset) -> bool {
						return VDStringSpanW(pset.GetString("revision", L"")) == L"c";
					},
					[](ATPropertySet& pset) {
						pset.SetString("revision", L"c");
					}
				)
				.AddChoice(
					L"Rev. D",
					[](const ATPropertySet& pset) -> bool {
						return VDStringSpanW(pset.GetString("revision", L"")) == L"d";
					},
					[](ATPropertySet& pset) {
						pset.SetString("revision", L"d");
					}
				)
				.AddChoice(
					L"Rev. Ds/S (rev.D with Covox)",
					[](const ATPropertySet& pset) -> bool {
						return VDStringSpanW(pset.GetString("revision", L"")) == L"s";
					},
					[](ATPropertySet& pset) {
						pset.SetString("revision", L"s");
					}
				)
				.AddChoice(
					L"Rev. E",
					[](const ATPropertySet& pset) -> bool {
						return VDStringSpanW(pset.GetString("revision", L"")) == L"e";
					},
					[](ATPropertySet& pset) {
						pset.SetString("revision", L"e");
					}
				);

			view.AddIntDropDown()
				.SetTag("id")
				.SetLabel(L"&PBI device ID")
				.AddChoice(0, L"ID 0")
				.AddChoice(1, L"ID 1")
				.AddChoice(2, L"ID 2")
				.AddChoice(3, L"ID 3")
				.AddChoice(4, L"ID 4")
				.AddChoice(5, L"ID 5")
				.AddChoice(6, L"ID 6")
				.AddChoice(7, L"ID 7");

			view.AddCheckbox()
				.SetTag("enablesdx")
				.SetText(L"&Enable SpartaDOS X module on boot")
				.SetDefault(true, true);

			view.AddCheckbox()
				.SetTag("writeprotect")
				.SetText(L"&Write protect hard disks");

			view.AddCheckbox()
				.SetTag("nvramguard")
				.SetText(L"Protect &NVRAM contents against reset during clock read")
				.SetDefault(true, true);
		}
	);
}
