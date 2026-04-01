//	Altirra SDL3 frontend - ATUIManager stub
//	Minimal stub replacing the Win32 ATUIManager class.
//	Only the methods used by settings.cpp are declared here.

#ifndef f_AT2_UIMANAGER_H
#define f_AT2_UIMANAGER_H

#include <vd2/system/VDString.h>

class ATUIManager {
public:
	const wchar_t *GetCustomEffectPath() const;
	void SetCustomEffectPath(const wchar_t *s, bool forceReload);
};

#endif // f_AT2_UIMANAGER_H
