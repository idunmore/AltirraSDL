//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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

bool ATUIConfDevAMDC(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"Amdek AMDC-I/II Options",
		[](IATUIConfigView& view) {
			auto& dsel = view.AddIntDropDown()
				.SetLabel(L"SW5/6: &Starting drive");

			view.AddVerticalSpace();

			VDStringW s;
			for(int i=1; i<=4; ++i) {
				s.sprintf(L"Drive %d (D%d:)", i, i);
				dsel.AddChoice(i - 1, s.c_str());
			}

			view.AddStaticText()
				.SetValue(L"3\", 40 track, double sided")
				.SetLabel(L"Internal drive &1");

			auto& sw1 = view.AddCheckbox()
				.SetText(L"SW1: Default to SD");

			view.AddVerticalSpace();

			view.AddCheckbox()
				.SetLabel(L"Internal drive &2")
				.SetText(L"Enable (AMDC-II)")
				.SetTag("drive2");

			auto& sw2 = view.AddCheckbox()
				.SetText(L"SW2: Default to SD");

			view.AddVerticalSpace();

			// external drives
			const auto addExtDrives = [](IATUIConfigIntDropDownView& combo) {
				combo.AddChoice(0, L"None");
				combo.AddChoice(1, L"3\"/5.25\" (40 track)");
				combo.AddChoice(2, L"3\"/5.25\" (80 track)");
			};

			auto& ext1 = view.AddIntDropDown()
				.SetLabel(L"External drive 1")
				.SetTag("extdrive0");

			addExtDrives(ext1);

			auto& sw3 = view.AddCheckbox()
				.SetText(L"SW3: Default to SD");

			view.AddVerticalSpace();

			auto& ext2 = view.AddIntDropDown()
				.SetLabel(L"External drive 2")
				.SetTag("extdrive1");
			
			addExtDrives(ext2);

			auto& sw4 = view.AddCheckbox()
				.SetText(L"SW4: Default to SD");

			view.AddVerticalSpace();

			auto& sw7 = view.AddCheckbox()
				.SetText(L"SW7: Assign drive IDs to internal drives first")
				.SetLabel(L"Additional options");

			auto& sw8 = view.AddCheckbox()
				.SetText(L"SW8: DOS 3.0 1050 compatibility mode");

			auto& sw9 = view.AddCheckbox()
				.SetText(L"Diagnostic test jumper enabled");

			view.SetCustomRead(
				[&](const ATPropertySet& pset) {
					const uint32 switches = pset.GetUint32("switches", 0x40);

					sw1.SetValue((switches & 0x01) != 0);
					sw2.SetValue((switches & 0x02) != 0);
					sw3.SetValue((switches & 0x04) != 0);
					sw4.SetValue((switches & 0x08) != 0);

					dsel.SetValue((switches >> 4) & 3);

					sw7.SetValue((switches & 0x40) != 0);
					sw8.SetValue((switches & 0x80) != 0);
					sw9.SetValue((switches & 0x100) != 0);
				}
			);

			view.SetCustomWrite(
				[&](ATPropertySet& pset) {
					uint32 switches = 0;

					if (sw1.GetValue()) switches += 0x01;
					if (sw2.GetValue()) switches += 0x02;
					if (sw3.GetValue()) switches += 0x04;
					if (sw4.GetValue()) switches += 0x08;

					switches += (dsel.GetValue() & 3) << 4;

					if (sw7.GetValue()) switches += 0x40;
					if (sw8.GetValue()) switches += 0x80;
					if (sw9.GetValue()) switches += 0x100;

					pset.SetUint32("switches", switches);
				}
			);
		}
	);
}
