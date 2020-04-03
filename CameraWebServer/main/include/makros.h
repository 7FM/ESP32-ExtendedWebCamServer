#pragma once

#define BOOL_TO_STR(x) ((x) ? "true" : "false")
#define CONST_STR_LEN(x) (sizeof(x) / sizeof((x)[0]) - 1)
#define MAXEQ(x, y) ((x) >= (y) ? (x) : (y))
#define NUMELEMS(x) (sizeof(x) / sizeof(x[0]))

// Makro to create a 32 bit int from FOURCC char array such that it can be written in little endian without problems
#define CONVERT_TO_FCC(cstr) (cstr[0] | cstr[1] << 8 | cstr[2] << 16 | cstr[3] << 24)