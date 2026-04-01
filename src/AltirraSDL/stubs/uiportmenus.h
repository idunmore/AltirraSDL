//	Altirra SDL3 frontend - uiportmenus.h stub
//	Replaces the Win32 version which #includes <windows.h> and uses HMENU.

#ifndef f_AT_UIPORTMENUS_H
#define f_AT_UIPORTMENUS_H

class ATInputManager;

void ATInitPortMenus(ATInputManager *im);
void ATUpdatePortMenus();
void ATShutdownPortMenus();
void ATReloadPortMenus();
bool ATUIHandlePortMenuCommand(uint32 id);

#endif // f_AT_UIPORTMENUS_H
