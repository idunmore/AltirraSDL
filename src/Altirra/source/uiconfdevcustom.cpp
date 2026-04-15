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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include "oshelper.h"
#include "uiconfgeneric.h"
#include "uiaccessors.h"

bool ATUIConfDevCustom(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"Custom Device Options",
		[](IATUIConfigView& view) {
			view.AddPath()
				.SetTag("path")
				.SetLabel(L"&Config path")
				.SetBrowseKey("CustomDevice"_vdtypeid)
				.SetBrowseCaption(L"Select Custom Device Description")
				.SetType(L"Altirra custom device desc. (*.atdevice)\0*.atdevice\0", L"atdevice");

			view.AddCheckbox()
				.SetTag("hotreload")
				.SetText(L"Enable &hot reload on file changes");

			view.AddCheckbox()
				.SetTag("allowunsafe")
				.SetText(L"All &full access to potentially unsafe operations");

			view.AddMultilineLinkableText(false)
				.SetValue(L"<a>Help: How to create a custom device</a>")
				.SetOnLinkSelected(
					[] {
						ATShowHelp(ATUIGetNewPopupOwner(), L"customdev-intro.html");
					}
				);
		}
	);
}
