#ifndef _OPTIONS_H_
#define _OPTIONS_H_

#include <stdbool.h>

extern struct Options_t {
    bool vsync;
} options;

void InitOptions();

#endif // !_OPTIONS_H_
