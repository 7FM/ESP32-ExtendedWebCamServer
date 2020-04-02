#pragma once

#include "WString.hpp"
#include "config.h"
#include <stdio.h>

void readConfig(FILE *file, const char *const *const options, int optionsCount, const int *optionNameLengths, String *const *parameters);

inline void readConfig(const char *const *const options, int optionsCount, const int *optionNameLengths, String *const *parameters) {

    FILE *configFile = fopen(CONFIG_FILE_PATH, "r");

    // Read config file if it exists
    if (configFile) {
        readConfig(configFile, options, optionsCount, optionNameLengths, parameters);
        fclose(configFile);
    }
}