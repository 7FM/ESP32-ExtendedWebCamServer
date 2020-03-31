#pragma once

#include <dirent.h>
#include <sys/stat.h>

inline bool isDir(struct dirent *entry) {
    return entry->d_type == DT_DIR;
}

inline bool isDir(struct stat *file_stat) {
    return S_ISDIR(file_stat->st_mode);
}

#define DIR_TYPE 2
#define FILE_TYPE 1
#define NONE_TYPE 0

inline int getType(char *absolutePath) {
    struct stat file_stat;

    if (stat(absolutePath, &file_stat)) {
        return NONE_TYPE;
    }

    return isDir(&file_stat) ? DIR_TYPE : FILE_TYPE;
}

inline bool listDirectory(const char *absolutePath, bool (*callback)(const char * /*entryName*/, bool /*isDirectory*/, void * /*arg*/), void *arg) {
    DIR *dp = opendir(absolutePath);

    if (dp == NULL) {
        return false;
    }

    struct dirent *entry;
    bool res = true;
    while ((entry = readdir(dp)) && (res = callback(entry->d_name, isDir(entry), arg))) {
    }

    closedir(dp);

    return res;
}
