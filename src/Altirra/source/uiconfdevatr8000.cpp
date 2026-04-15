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
#include <at/atcore/enumparseimpl.h>
#include "uiconfgeneric.h"

enum class ATATR8000Signal1Type : uint8 {
	Rts,
	Dtr
};

enum class ATATR8000Signal2Type : uint8 {
	Cts,
	Dsr,
	Cd,
	Srts
};

AT_DEFINE_ENUM_TABLE_BEGIN(ATATR8000Signal1Type)
	{ ATATR8000Signal1Type::Rts, "rts" },
	{ ATATR8000Signal1Type::Dtr, "dtr" },
AT_DEFINE_ENUM_TABLE_END(ATATR8000Signal1Type, ATATR8000Signal1Type::Rts)

AT_DEFINE_ENUM_TABLE_BEGIN(ATATR8000Signal2Type)
	{ ATATR8000Signal2Type::Cts, "cts" },
	{ ATATR8000Signal2Type::Dsr, "dsr" },
	{ ATATR8000Signal2Type::Cd, "cd" },
	{ ATATR8000Signal2Type::Srts, "srts" },
AT_DEFINE_ENUM_TABLE_END(ATATR8000Signal2Type, ATATR8000Signal2Type::Cts)

bool ATUIConfDevATR8000(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"ATR8000 Options",
		[](IATUIConfigView& view) {
			VDStringW s;
			VDStringA tag;

			for(int i=0; i<4; ++i) {
				s.sprintf(L"Drive &%d", i + 1);
				tag.sprintf("drivetype%d", i);

				view.AddIntDropDown()
					.SetLabel(s.c_str())
					.SetTag(tag.c_str())
					.SetDefault(i ? 0 : 1)
					.AddChoice(0, L"None")
					.AddChoice(1, L"5.25\"")
					.AddChoice(2, L"8\"");
			}

			view.AddVerticalSpace();

			view.AddDropDown<ATATR8000Signal1Type>()
				.SetLabel(L"&Serial port signals")
				.SetTag("signal1")
				.AddChoice(ATATR8000Signal1Type::Rts, L"Request To Send (RTS)")
				.AddChoice(ATATR8000Signal1Type::Dtr, L"Data Terminal Ready (DTR)");

			view.AddDropDown<ATATR8000Signal2Type>()
				.SetTag("signal2")
				.AddChoice(ATATR8000Signal2Type::Cts, L"Clear To Send (CTS)")
				.AddChoice(ATATR8000Signal2Type::Dsr, L"Data Set Ready (DSR)")
				.AddChoice(ATATR8000Signal2Type::Cd, L"Carrier Detect (CD)")
				.AddChoice(ATATR8000Signal2Type::Srts, L"Secondary Request To Send (SRTS)");
		}
	);
}
