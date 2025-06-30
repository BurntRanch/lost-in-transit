#include "main_menu.h"
#include "renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>

static TTF_Text *game_text = NULL;

bool MainMenuInit(TTF_TextEngine *pTextEngine) {
    if (!pLEGameFont) {
        fprintf(stderr, "Trying to load text without loading the font!");
        return false;
    }

    if (!(game_text = TTF_CreateText(pTextEngine, pLEGameFont, "Hii!", 0))) {
        fprintf(stderr, "Failed to create text for main menu! (SDL Error Code: %s)\n", SDL_GetError());
        return false;
    }

    return true;
}

bool MainMenuRender(void) {
    int text_width, text_height;
    if (!TTF_GetTextSize(game_text, &text_width, &text_height)) {
        fprintf(stderr, "Failed to get text width! (SDL Error Code : %s)\n", SDL_GetError());

        return false;
    }

    if (!TTF_DrawRendererText(game_text, 400 - (text_width/2), 300 - (text_height/2))) {
        fprintf(stderr, "Failed to draw text for main menu! (SDL Error Code: %s)\n", SDL_GetError());

        return false;
    }

    return true;
}

void MainMenuCleanup(void) {
    TTF_DestroyText(game_text);
}
