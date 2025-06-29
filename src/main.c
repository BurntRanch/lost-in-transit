#include "renderer.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <stdbool.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

int main(int argc, char *argv[]) {
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
                else if (!optarg || strncmp(optarg, "false", 6) != 0) {
                    fprintf(stderr, "-vsync requires an argument that's either 'true' or 'false'");
                    return 1;
                }
        }
    }

    if (!LEInitWindow()) {
        return 1;
    }

    double frametime;
    while (LEStepRender(&frametime)) {
        fprintf(stdout, "frametime: %fms (%ld FPS)\n", frametime, SDL_lround(1 / (frametime / 1000)));
    }

    return 0;
}
