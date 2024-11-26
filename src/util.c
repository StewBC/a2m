// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

int util_dir_change(const char *path) {
#ifdef _WIN32
    if(_chdir(path) == 0) {
        return A2_OK;
    }
#else
    if(chdir(path) == 0) {
        return A2_OK;
    }
#endif
    return A2_ERR;
}

int util_dir_get_current(char *buffer, size_t buffer_size) {
#ifdef _WIN32
    if(_getcwd(buffer, (int) buffer_size) != NULL) {
        return A2_OK;
    }
#else
    if(getcwd(buffer, buffer_size) != NULL) {
        return A2_OK;
    }
#endif
    return A2_ERR;
}

int util_dir_load_contents(DYNARRAY *array) {

#ifdef _WIN32
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    char dirSpec[PATH_MAX];

    // Prepare string for use with FindFile functions. First, copy the current directory path
    if(_getcwd(dirSpec, PATH_MAX) == NULL) {
        return A2_ERR;
    }
    // Append the wildcard to search for all files
    strncat(dirSpec, "\\*", PATH_MAX - strlen(dirSpec) - 1);

    hFind = FindFirstFileA(dirSpec, &findFileData);

    if(hFind == INVALID_HANDLE_VALUE) {
        return A2_ERR;
    }

    do {
        FILE_INFO info;
        strncpy(info.name, findFileData.cFileName, PATH_MAX);
        info.is_directory = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

        LARGE_INTEGER fileSize;
        fileSize.LowPart = findFileData.nFileSizeLow;
        fileSize.HighPart = findFileData.nFileSizeHigh;
        info.size = (size_t) fileSize.QuadPart;

        ARRAY_ADD(array, info);
    } while(FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);

    // Check for errors in FindNextFile
    DWORD dwError = GetLastError();
    if(dwError != ERROR_NO_MORE_FILES) {
        return A2_ERR;
    }
#else
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;

    dp = opendir(".");
    if(dp == NULL) {
        return A2_ERR;
    }

    while((entry = readdir(dp)) != NULL) {
        FILE_INFO info;
        strncpy(info.name, entry->d_name, PATH_MAX);

        if(stat(entry->d_name, &statbuf) == 0) {
            info.size = (size_t) statbuf.st_size;
            info.is_directory = S_ISDIR(statbuf.st_mode);
        } else {
            info.size = 0;
            info.is_directory = 0;
        }
        ARRAY_ADD(array, info);
    }
    closedir(dp);
#endif
    return A2_OK;
}

void util_file_close(UTIL_FILE *f) {
    // Close the file handle if it's open, and set the file to not-open
    if(f->is_used && f->is_file_open) {
        fclose(f->fp);
        f->fp = 0;
        f->is_file_open = 0;
    }
}

int util_file_load(UTIL_FILE *f, const char *file_name, const char *file_mode) {
    // Open the file
    if(A2_OK != util_file_open(f, file_name, file_mode)) {
        return A2_ERR;
    }
    // Get a buffer to hold the contents
    int64_t total_size = f->file_size + f->load_padding;
    f->file_data = (char *) malloc(total_size);
    if(!f->file_data) {
        return A2_ERR;
    }
    // Read the file into the buffer
    f->file_size = fread(f->file_data, 1, f->file_size, f->fp);
    // Fill the padding with 0's (in text files f->file_size might now be less
    // than the seek size indicated - fill remainder with 0's).
    memset(&f->file_data[f->file_size], 0, total_size - f->file_size);
    f->is_file_loaded = 1;
    fclose(f->fp);
    f->fp = 0;
    f->is_file_open = 0;

    return A2_OK;
}

int util_file_open(UTIL_FILE *f, const char *file_name, const char *file_mode) {
    // Make sure this file "handle" is clean
    util_file_discard(f);

    // Remember the file name and mode
    f->file_path = strdup(file_name);
    f->file_display_name = util_strrtok(f->file_path, "\\/");
    if(!f->file_display_name) {
        f->file_display_name = f->file_path;
    } else {
        f->file_display_name++;
    }
    f->file_mode = strdup(file_mode);

    // Open and size the file
    if(!(f->fp = fopen(file_name, file_mode))) {
        return A2_ERR;
    }
    f->is_used = 1;
    f->is_file_open = 1;

    if(fseek(f->fp, 0, SEEK_END) < 0) {
        return A2_ERR;
    }
    f->file_size = ftell(f->fp);
    if(f->file_size < 0) {
        return A2_ERR;
    }
    if(fseek(f->fp, 0, SEEK_SET) < 0) {
        return A2_ERR;
    }

    return A2_OK;
}

void util_file_discard(UTIL_FILE *f) {
    // If this handle is being reused, clean it up so it is "fresh"
    if(f->is_used) {
        if(f->is_file_open) {
            fclose(f->fp);
            f->fp = 0;
            f->is_file_open = 0;
        }
        if(f->is_file_loaded) {
            free(f->file_data);
            f->is_file_loaded = 0;
        }
        f->file_size = 0;
        f->file_display_name = 0;
        free(f->file_path);
        f->file_path = 0;
        free(f->file_mode);
        f->file_mode = 0;
        f->is_used = 0;
    }
}

char *util_ini_find_character(char **start, char character) {
    char *old_start = *start;
    while(**start && **start != character && **start != ';') {
        (*start)++;
    }
    if(**start) {
        **start = '\0';
        char *tail = *start - 1;
        while(tail > old_start && isspace(*tail)) {
            *tail-- = '\0';
        }
        (*start)++;
    }
    return old_start;
}

char *util_ini_get_line(char **start, char *end) {
    char *old_start = *start;
    while(*start < end && **start != '\n' && **start != '\r') {
        (*start)++;
    }
    *(*start)++ = '\0';                                     // padding makes sure this will be safe
    while(*start < end && (**start == '\n' || **start == '\r')) {
        (*start)++;
    }
    return old_start;
}

char *util_ini_next_token(char **start) {
    while(**start && isspace(**start)) {
        (*start)++;
    }
    return *start;
}

int util_ini_load_file(char *filename, INI_PAIR_CALLBACK callback, void *user_data) {
    UTIL_FILE ini_file;
    memset(&ini_file, 0, sizeof(UTIL_FILE));
    ini_file.load_padding = 1;
    char *section = 0;
    char *key = 0;
    char *value = 0;
    if(A2_OK != util_file_load(&ini_file, filename, "r")) {
        return A2_ERR;
    }
    char *current = ini_file.file_data;
    char *end = current + ini_file.file_size;
    while(current < end) {
        char *line = util_ini_get_line(&current, end);
        line = util_ini_next_token(&line);
        switch (*line) {
        case ';':
            continue;
        case '[':
            line++;
            util_ini_next_token(&line);
            if(*line) {
                section = util_ini_find_character(&line, ']');
            }
            break;
        default:
            if(*line) {
                key = util_ini_find_character(&line, '=');
                if(!key) {
                    continue;
                }
                util_ini_next_token(&line);
                value = util_ini_find_character(&line, '\0');
                callback(user_data, section, key, value);
            }
            break;
        }
    }
    util_file_discard(&ini_file);
    return A2_OK;
}

int util_qsort_cmp(const void *p1, const void *p2) {
    FILE_INFO *fip1 = (FILE_INFO *) p1;
    FILE_INFO *fip2 = (FILE_INFO *) p2;
    if(fip1->is_directory != fip2->is_directory) {
        return fip1->is_directory - fip2->is_directory;
    }
    return stricmp(fip1->name, fip2->name);
}

char *util_strrtok(char *str, const char *delim) {
    char *s = str + strlen(str);
    while(s > str) {
        const char c = *--s;
        const char *d = delim;
        while(*d && c != *d) {
            d++;
        }
        if(*d) {
            return s;
        }
    }
    return NULL;
}
