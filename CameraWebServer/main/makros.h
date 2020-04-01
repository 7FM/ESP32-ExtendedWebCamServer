#pragma once

#define BOOL_TO_STR(x) ((x) ? "true" : "false")
#define CONST_STR_LEN(x) (sizeof(x) / sizeof((x)[0]) - 1)
#define MAXEQ(x, y) ((x) >= (y) ? (x) : (y))
#define NUMELEMS(x) (sizeof(x) / sizeof(x[0]))