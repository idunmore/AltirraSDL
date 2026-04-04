package org.altirra.app;

import org.libsdl.app.SDLActivity;

/**
 * Altirra Android activity.
 *
 * Extends SDL3's SDLActivity which handles:
 * - GL surface creation and lifecycle
 * - Native library loading (libmain.so via libSDL3.so)
 * - Touch event routing to SDL
 * - Gamepad/keyboard input
 * - Audio device management
 *
 * We override only what's needed for Altirra-specific behavior.
 */
public class AltirraActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[]{
            "SDL3",
            "main"  // Our native library (AltirraSDL compiled as libmain.so)
        };
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }
}
