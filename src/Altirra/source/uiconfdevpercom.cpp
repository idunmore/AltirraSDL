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

bool ATUIConfDevPercom(VDGUIHandle hParent, ATPropertySet& props, const wchar_t *caption, bool atmode, bool atspdmode) {
	return ATUIShowDialogGenericConfig(hParent,
		props,
		caption,
		[&](IATUIConfigView& view) {
			if (atspdmode) {
				view.AddBoolDropDown()
					.SetLabel(L"&FDC type")
					.SetTag("use1795")
					.AddChoice(false, L"1791 (side compare optional)")
					.AddChoice(true, L"1795 (side compare always on)")
					.SetValue(false);
			} else if (atmode) {
				view.AddCustomDropDown()
					.SetLabel(L"&FDC type")
					.AddChoice(
						L"1771+1791 (double density, side compare optional)",
						[](const ATPropertySet& pset) {
							return pset.GetBool("ddcapable", true) && !pset.GetBool("use1795", false);
						},
						[](ATPropertySet& pset) {
							pset.SetBool("ddcapable", true);
							pset.SetBool("use1795", false);
						}
					)
					.AddChoice(
						L"1771+1795 (double density, side compare always on)",
						[](const ATPropertySet& pset) {
							return pset.GetBool("ddcapable", true) && pset.GetBool("use1795", false);
						},
						[](ATPropertySet& pset) {
							pset.SetBool("ddcapable", true);
							pset.SetBool("use1795", true);
						}
					)
					.AddChoice(
						L"1771 (single density only)",
						[](const ATPropertySet& pset) {
							return !pset.GetBool("ddcapable", true);
						},
						[](ATPropertySet& pset) {
							pset.SetBool("ddcapable", false);
							pset.SetBool("use1795", false);
						}
					)
					.SetValue(0);
			} else {
				auto& driveId = view.AddIntDropDown()
					.SetLabel(L"&Starting drive ID")
					.SetTag("id");

				VDStringW s;
				for(int i=1; i<=8; ++i) {
					s.sprintf(L"Drive %d (D%d:)", i, i);
					driveId.AddChoice(i - 1, s.c_str());
				}
			}

			view.AddVerticalSpace();

			VDStringW s;
			VDStringA tag;
			for(size_t i=0; i<4; ++i) {
				s.sprintf(L"Drive &%d", i+1);
				tag.sprintf("drivetype%d", i);

				view.AddIntDropDown()
					.SetLabel(s.c_str())
					.SetTag(tag.c_str())
					.SetDefault(i ? 0 : 1)
					.AddChoice(0, L"None")
					.AddChoice(1, L"5.25\" (40 track)")
					.AddChoice(2, L"5.25\" (80 track)");
			}
		}
	);
}

bool ATUIConfDevPercomRFD(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIConfDevPercom(hParent, props, L"Percom RFD-40S1 Options", false, false);
}

bool ATUIConfDevPercomAT(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIConfDevPercom(hParent, props, L"Percom AT-88S1 Options", true, false);
}

bool ATUIConfDevPercomATSPD(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIConfDevPercom(hParent, props, L"Percom AT-88SPD Options", true, true);
}
