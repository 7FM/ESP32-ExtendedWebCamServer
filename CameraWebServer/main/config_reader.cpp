#include "config_reader.hpp"

static inline void removeStringChars(char **strPtr) {
    char *const str = *strPtr;
    const int length = strlen(str);
    const int lastIndex = length - 1;
    if (str[lastIndex] == '\'' || str[lastIndex] == '"') {
        str[lastIndex] = '\0';
    }

    if (str[0] == '\'' || str[0] == '"') {
        *strPtr = str + 1;
    }
}

/*
    maxReadSize is the buffer size - 1 so that \0 can still be appended
*/
static inline bool getLine(FILE *file, String &buffer, char *buf, int maxReadSize, String &line) {
    size_t pos;
    int n;

    while ((pos = buffer.indexOf('\n')) == -1) {
        n = fread(buf, 1, maxReadSize, file);

        if (n <= 0) {
            return false;
        }

        buf[n] = '\0';
        buffer += buf;

        if (n < maxReadSize) {
            if ((pos = buffer.indexOf('\n')) != -1) {
                break;
            }
            line = buffer;
            buffer = "";
            return false;
        }
    }

    // Split the buffer around '\n' found and return first part.
    line = buffer.substring(0, pos);
    buffer = buffer.substring(pos + 1);

    return true;
}

static inline bool matchesOption(const char *fieldName, const char *const *optionNames, int length) {
    for (int i = 0; i < length; ++i) {
        if (!strcmp(optionNames[i], fieldName)) {
            return true;
        }
    }
    return false;
}

void readConfig(FILE *file, const char *const *const options, int optionsCount, const int *optionNameLengths, String *const *parameters) {
    String buffer;
    char readBuf[128];

    bool continueRead;

    do {

        String line = "";

        continueRead = getLine(file, buffer, readBuf, 127, line);

        const int lineLength = line.length();

        if (lineLength > 0) {

            char fieldBuf[lineLength];
            char valueBuf[lineLength];

            char *field_name = fieldBuf;
            char *value = valueBuf;

            int parseRes = sscanf(line.c_str(), " %s = %s ", field_name, value);

            //Check if both %s where filled with a value... else skip
            //Also ignore comments... this check should be fine
            //because sscanf always '\0' terminates strings so there should at least be one char
            if (parseRes == 2 && field_name[0] != '#') {
                // Remove ' and " from fields
                removeStringChars(&field_name);
                removeStringChars(&value);

                const char *const *optionsOffset = options;

                for (int i = 0; i < optionsCount; ++i) {
                    const int numOptionNames = optionNameLengths[i];

                    if (matchesOption(field_name, optionsOffset, numOptionNames)) {
                        *(parameters[i]) = value;
                        break;
                    }

                    optionsOffset += numOptionNames;
                }
            }
        }
    } while (continueRead);
}