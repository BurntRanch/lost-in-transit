#include "options.h"
#include <stdlib.h>
#include <unistd.h>
#include "tomlc17.h"

struct Options_t options;

const char *const PATH = "config.toml";

static void error(const char *msg) {
  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
}

void OverWriteConfigFile() {
    FILE *f = fopen(PATH, "w");
    if (!f)
        return;

    fprintf(f, "[config]\n");
    fprintf(f, "vsync = %s\n", options.vsync ? "true" : "false");
    fclose(f);
}

void InitOptions() {
    if (access(PATH, F_OK) != 0) {
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
}
