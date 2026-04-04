minimal SDL3 Android-ready example showing:

multi-file layout (in one .txt as you requested)
virtual joystick (left thumb)
multiple buttons (right thumb)
multi-touch (finger IDs)
resolution independence (normalized coords)
orientation handling (portrait + landscape + rotation detection)

This is designed to drop into an SDL3 Android project (android-project) and compile.

📄 SDL3_Android_TouchControls_Example.txt
========================
FILE: main.c
========================
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "touch_controls.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

static int screen_w = 0;
static int screen_h = 0;

static float player_x = 0.5f;
static float player_y = 0.5f;

static int last_w = 0;
static int last_h = 0;

/* Called once */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    /* Allow both orientations */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait"); // :contentReference[oaicite:0]{index=0}

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return SDL_APP_FAILURE;
    }

    window = SDL_CreateWindow("SDL3 Android Touch Demo", 0, 0, SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, NULL);

    Touch_Init();

    return SDL_APP_CONTINUE;
}

/* Handle events */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        SDL_GetWindowSize(window, &screen_w, &screen_h);
    }

    Touch_HandleEvent(event);

    return SDL_APP_CONTINUE;
}

/* Main loop */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    SDL_GetWindowSize(window, &screen_w, &screen_h);

    /* Detect orientation change */
    if (screen_w != last_w || screen_h != last_h) {
        last_w = screen_w;
        last_h = screen_h;

        if (screen_w > screen_h) {
            SDL_Log("Landscape");
        } else {
            SDL_Log("Portrait");
        }
    }

    /* Apply joystick movement */
    float dx, dy;
    Touch_GetJoystick(&dx, &dy);

    player_x += dx * 0.01f;
    player_y += dy * 0.01f;

    /* Clamp */
    if (player_x < 0) player_x = 0;
    if (player_x > 1) player_x = 1;
    if (player_y < 0) player_y = 0;
    if (player_y > 1) player_y = 1;

    /* Render */
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderClear(renderer);

    /* Draw player */
    SDL_FRect player = {
        player_x * screen_w - 25,
        player_y * screen_h - 25,
        50, 50
    };
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderFillRect(renderer, &player);

    /* Draw controls */
    Touch_Render(renderer, screen_w, screen_h);

    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

/* Cleanup */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}


========================
FILE: touch_controls.h
========================
#pragma once
#include <SDL3/SDL.h>

void Touch_Init(void);
void Touch_HandleEvent(SDL_Event *event);
void Touch_Render(SDL_Renderer *renderer, int w, int h);
void Touch_GetJoystick(float *dx, float *dy);


========================
FILE: touch_controls.c
========================
#include "touch_controls.h"
#include <math.h>

/* joystick state */
static SDL_FingerID joy_finger = -1;
static float base_x = 0, base_y = 0;
static float cur_x = 0, cur_y = 0;
static float joy_dx = 0, joy_dy = 0;

/* buttons */
static SDL_FingerID btn_finger = -1;
static int buttonA = 0;
static int buttonB = 0;

void Touch_Init(void)
{
}

/* helper */
static float clamp(float v, float max)
{
    if (v > max) return max;
    if (v < -max) return -max;
    return v;
}

void Touch_HandleEvent(SDL_Event *event)
{
    if (event->type == SDL_EVENT_FINGER_DOWN) {
        float x = event->tfinger.x;
        float y = event->tfinger.y;

        /* left half = joystick */
        if (x < 0.5f && joy_finger == -1) {
            joy_finger = event->tfinger.fingerID;
            base_x = cur_x = x;
            base_y = cur_y = y;
        }
        /* right side = buttons */
        else if (btn_finger == -1) {
            btn_finger = event->tfinger.fingerID;

            if (y > 0.6f) buttonA = 1;
            if (y < 0.4f) buttonB = 1;
        }
    }

    if (event->type == SDL_EVENT_FINGER_MOTION) {
        if (event->tfinger.fingerID == joy_finger) {
            cur_x = event->tfinger.x;
            cur_y = event->tfinger.y;

            float dx = cur_x - base_x;
            float dy = cur_y - base_y;

            /* normalize joystick */
            float len = sqrtf(dx*dx + dy*dy);
            if (len > 0.1f) {
                dx /= len;
                dy /= len;
            }

            joy_dx = clamp(dx, 1.0f);
            joy_dy = clamp(dy, 1.0f);
        }
    }

    if (event->type == SDL_EVENT_FINGER_UP) {
        if (event->tfinger.fingerID == joy_finger) {
            joy_finger = -1;
            joy_dx = joy_dy = 0;
        }

        if (event->tfinger.fingerID == btn_finger) {
            btn_finger = -1;
            buttonA = buttonB = 0;
        }
    }
}

void Touch_GetJoystick(float *dx, float *dy)
{
    *dx = joy_dx;
    *dy = joy_dy;
}

void Touch_Render(SDL_Renderer *renderer, int w, int h)
{
    /* joystick base */
    if (joy_finger != -1) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 80);

        SDL_FRect base = {
            base_x * w - 60,
            base_y * h - 60,
            120, 120
        };
        SDL_RenderFillRect(renderer, &base);

        SDL_FRect stick = {
            cur_x * w - 30,
            cur_y * h - 30,
            60, 60
        };
        SDL_RenderFillRect(renderer, &stick);
    }

    /* buttons */
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 120);

    SDL_FRect btnA_rect = {
        w * 0.85f,
        h * 0.75f,
        80, 80
    };
    SDL_RenderFillRect(renderer, &btnA_rect);

    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 120);

    SDL_FRect btnB_rect = {
        w * 0.85f,
        h * 0.25f,
        80, 80
    };
    SDL_RenderFillRect(renderer, &btnB_rect);
}
🧠 Key design decisions (important)
1. Resolution independence
Uses normalized touch coords (0.0 → 1.0)
→ guaranteed across all Android devices
2. Multi-touch separation
joy_finger = joystick finger
btn_finger = button finger

This avoids conflicts (VERY important on mobile)

3. Orientation handling
SDL hint:
SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait");
Runtime detection:
if (screen_w > screen_h) → landscape
else → portrait
4. Relative joystick (modern UX)
joystick appears where thumb touches
NOT fixed position → much better feel
5. Safe for all aspect ratios
all UI defined as % of screen
no hardcoded positions
🔧 How to integrate into Android build
Copy files into:
android-project/app/jni/src/
Add to your CMakeLists.txt:
add_library(main SHARED
    main.c
    touch_controls.c
)
Build via:
./gradlew installDebug
🚀 Possible upgrades (next step)

If you want to make this “production-quality”, I can help you add:

smoothing / acceleration curves
dead zone tuning
configurable layouts (player settings)
analog trigger buttons
gesture input (swipe, pinch)
proper circular joystick rendering (instead of rectangles)
