//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

bool ATUIConfDev815(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"815 Disk Drive Options",
		[](IATUIConfigView& view) {
			view.AddIntDropDown()
				.AddChoice(0, L"Drives 1-2 (D1-D2:)")
				.AddChoice(2, L"Drives 3-4 (D3-D4:)")
				.AddChoice(4, L"Drives 5-6 (D5-D6:)")
				.AddChoice(6, L"Drives 7-8 (D7-D8:)")
				.SetDefault(0)
				.SetLabel(L"&Drive select")
				.SetTag("id");

			view.AddVerticalSpace();

			view.AddBoolChoice()
				.SetLabel(L"Data &inversion")
				.SetTag("accurate_invert")
				.SetFirstChoiceValue(true)
				.SetFirstChoiceText(L"Store data inverted from other drives (accurate)")
				.SetSecondChoiceText(L"Automatically un-invert data for 815 (compatible)")
				.SetDefault(false);
		}
	);
}
