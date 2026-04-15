//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2017 Avery Lee
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

bool ATUIConfDevDiskDriveFull(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"Disk Drive Options",
		[](IATUIConfigView& view) {
			view.AddIntDropDown()
				.SetTag("id")
				.SetLabel(L"&Drive select")
				.AddChoice(0, L"Drive 1 (D1:)")
				.AddChoice(1, L"Drive 2 (D2:)")
				.AddChoice(2, L"Drive 3 (D3:)")
				.AddChoice(3, L"Drive 4 (D4:)");
		}
	);
}
