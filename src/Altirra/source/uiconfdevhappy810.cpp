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

bool ATUIConfDevHappy810(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"Happy 810 Options",
		[](IATUIConfigView& view) {
			auto& dsel = view.AddIntDropDown()
				.SetLabel(L"&Drive select")
				.SetTag("id");

			VDStringW s;
			for(int i=1; i<=4; ++i) {
				s.sprintf(L"Drive %d (D%d:)", i, i);
				dsel.AddChoice(i - 1, s.c_str());
			}

			view.AddVerticalSpace();

			auto& autospeed = view.AddCheckbox()
				.SetLabel(L"&Autospeed mod")
				.SetText(L"Install autospeed mod (3K pre-V7 firmware only)")
				.SetTag("autospeed");

			view.AddIntSlider()
				.SetTag("autospeedrate")
				.SetRange(200, 400)
				.SetDefault(266, true)
				.SetFormatter([](VDStringW& s, sint32 val) { s.sprintf(L"%d RPM", val); })
				.SetEnableExpr([&] { return autospeed.GetValue(); });
		}
	);
}
