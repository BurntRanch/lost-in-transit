#include "options.h"
#include "tomlc17.h"

#include <stdlib.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_filesystem.h>

struct Options_t options;

const char *const PATH = "options.toml";

static void error(const char *msg) {
  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
}

void OverWriteConfigFile(void) {
    SDL_IOStream *stream = SDL_IOFromFile(PATH, "w");
    if (!stream)
        return;

    SDL_IOprintf(stream, "[config]\n");
    SDL_IOprintf(stream, "vsync = %s\n", options.vsync ? "true" : "false");
    SDL_IOprintf(stream, "cam_sens = %f\n", options.cam_sens);
    
    SDL_CloseIO(stream);
}

void InitOptions(void) {
    options.vsync = true;
    options.cam_sens = 0.5f;

    if (!SDL_GetPathInfo(PATH, NULL)) {
        OverWriteConfigFile();
    }

    toml_result_t result = toml_parse_file_ex(PATH);
    if (!result.ok) {
        error(result.errmsg);
    }

    toml_datum_t vsync = toml_seek(result.toptab, "config.vsync");
    if (vsync.type != TOML_BOOLEAN) {
        error("config option \"config.vsync\" is not a BOOLEAN");
    }
    options.vsync = vsync.u.boolean;

    toml_datum_t cam_sens = toml_seek(result.toptab, "config.cam_sens");
    if (cam_sens.type != TOML_FP64) {
        error("config option \"config.cam_sens\" is not FP64");
    }
    options.cam_sens = cam_sens.u.fp64;
}
