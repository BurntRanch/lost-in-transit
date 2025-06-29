#include "renderer.h"

#include <SDL3/SDL_init.h>
#include <stdbool.h>
#include <stdio.h>

int main(void) {
    if (!LEInitWindow()) {
        return 1;
    }

    double frametime;
    while (LEStepRender(&frametime)) {
        fprintf(stdout, "frametime: %fms (%ld FPS)\n", frametime, SDL_lround(1 / (frametime / 1000)));
    }

    return 0;
}
