#pragma once
#include <SDL3/SDL.h>
#include "joystick.h"

// SDL3-specific extension to IATJoystickManager.
// Only used by main_sdl3.cpp for gamepad removal events.
class ATJoystickManagerSDL3 : public IATJoystickManager {
public:
	virtual void CloseGamepad(SDL_JoystickID id) = 0;
};
