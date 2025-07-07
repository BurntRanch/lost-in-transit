#include "engine.h"
#include "scenes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stdio.h>

int main() {
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    if (!LEInitWindow() || !LEInitTTF() || !LEInitSteam())
        return 1;

    LEGameFont = TTF_OpenFont("AdwaitaMono-Regular.ttf", 24);
    if (!LEGameFont) {
        printf("Failed to load game font! (SDL Error Code: %s)\n", SDL_GetError());
        return 1;
    }

    if (!LELoadScene(SCENE_MAINMENU))
        return 1;

    // double frametime;
    while (LEStepRender(NULL)) {
        // printf("frametime: %fms (%ld FPS)\n", frametime * 1000, SDL_lround(1 / frametime));
    }

    TTF_CloseFont(LEGameFont);
    LEGameFont = NULL;
    
    LECleanupScene();
    LEDestroyWindow();
    TTF_Quit();
    SDL_Quit();

    return 0;
}
