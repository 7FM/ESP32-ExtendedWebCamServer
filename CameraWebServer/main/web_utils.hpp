#pragma once

static inline unsigned char h2int(char c) {
    if (c >= '0' && c <= '9') {
        return ((unsigned char)c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return ((unsigned char)c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return ((unsigned char)c - 'A' + 10);
    }
    return '\0';
}

inline bool urldecode(char *str, size_t length) {
    size_t resIdx = 0;
    for (size_t i = 0; i < length; i++) {
        char c = str[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= length) {
                // Invalid encoding abort!
                str[resIdx] = '\0';
                return false;
            }
            char code0 = str[++i];
            char code1 = str[++i];
            c = (h2int(code0) << 4) | h2int(code1);
        }
        str[resIdx++] = c;
    }
    str[resIdx] = '\0';
    return true;
}