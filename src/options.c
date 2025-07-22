#include "options.h"
#include <stdlib.h>
#include "tomlc17.h"

struct Options_t options;

static void error(const char *msg) {
  fprintf(stderr, "ERROR: %s\n", msg);
  exit(1);
}

void InitOptions() {
    toml_result_t result = toml_parse_file_ex("config.toml");
    if (!result.ok) {
        error(result.errmsg);
    }

    toml_datum_t vsync = toml_seek(result.toptab, "config.vsync");

    if (vsync.type != TOML_BOOLEAN) {
        error("config option \"config.vsync\" is not a BOOLEAN");
    }

    options.vsync = vsync.u.boolean;
}
