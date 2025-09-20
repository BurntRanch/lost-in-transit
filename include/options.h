#ifndef _OPTIONS_H_
#define _OPTIONS_H_

#include <stdbool.h>

extern struct Options_t {
    bool vsync;
    float cam_sens;
} options;

void InitOptions();
void OverWriteConfigFile();

#endif // !_OPTIONS_H_
