#include "renderer.h"
#include "scenes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

int main(int argc, char *argv[]) {
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

    const struct option long_opts[] = {
        { "vsync", required_argument, 0, 'v' },
        { NULL, 0, 0, 0 }
    };

    int optind = 0;

    while (true) {
        char c = getopt_long(argc, argv, "v:", long_opts, &optind);
        if (c == -1)
            break;

        switch (c) {
            case 'v':
                if (optarg && strncmp(optarg, "true", 5) == 0)
                    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
                else if (optarg && strncmp(optarg, "false", 6) == 0) {
                    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
                } else {
                    fprintf(stderr, "--vsync/-v requires an argument that's either 'true' or 'false'");
                    return 1;
                }
        }
    }

    if (!LEInitWindow() || !LEInitTTF())
        return 1;

    pLEGameFont = TTF_OpenFont("AdwaitaMono-Regular.ttf", 24);
    if (!pLEGameFont) {
        printf("Failed to load game font! (SDL Error Code: %s)\n", SDL_GetError());
        return 1;
    }

    if (!LELoadScene(SCENE_MAINMENU))
        return 1;

    double frametime;
    while (LEStepRender(&frametime)) {
        printf("frametime: %fms (%ld FPS)\n", frametime, SDL_lround(1 / (frametime / 1000)));
    }

    TTF_CloseFont(pLEGameFont);
    pLEGameFont = NULL;
    
    LECleanupScene();
    LEDestroyWindow();
    TTF_Quit();
    SDL_Quit();

    return 0;
}
