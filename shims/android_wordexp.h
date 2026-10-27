#pragma once
// Minimal wordexp() for Android (bionic has none). Splits on whitespace and honours single /
// double quotes and backslash escapes; no variable, tilde or command expansion.

#include <stdlib.h>
#include <string.h>

#define WRDE_DOOFFS  (1 << 0)
#define WRDE_APPEND  (1 << 1)
#define WRDE_NOCMD   (1 << 2)
#define WRDE_REUSE   (1 << 3)
#define WRDE_SHOWERR (1 << 4)
#define WRDE_UNDEF   (1 << 5)

enum { WRDE_SUCCESS = 0, WRDE_NOSPACE, WRDE_BADCHAR, WRDE_BADVAL, WRDE_CMDSUB, WRDE_SYNTAX };

typedef struct {
    size_t we_wordc;
    char** we_wordv;
    size_t we_offs;
} wordexp_t;

static inline void wordfree(wordexp_t* we)
{
    if (we->we_wordv == NULL)
        return;
    for (size_t i = 0; i < we->we_wordc; ++i)
        free(we->we_wordv[we->we_offs + i]);
    free(we->we_wordv);
    we->we_wordv = NULL;
    we->we_wordc = 0;
}

static inline int wordexp(const char* words, wordexp_t* we, int flags)
{
    (void) flags;
    we->we_wordc = 0;
    we->we_offs  = 0;
    we->we_wordv = (char**) calloc(1, sizeof(char*));
    if (we->we_wordv == NULL)
        return WRDE_NOSPACE;

    const size_t len = strlen(words);
    char* word = (char*) malloc(len + 1);
    if (word == NULL)
        return WRDE_NOSPACE;

    const char* p = words;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            ++p;
        if (!*p)
            break;
        size_t n = 0;
        char quote = 0;
        for (; *p; ++p) {
            if (quote) {
                if (*p == quote)
                    quote = 0;
                else if (*p == '\\' && quote == '"' && p[1])
                    word[n++] = *++p;
                else
                    word[n++] = *p;
            } else if (*p == '\'' || *p == '"') {
                quote = *p;
            } else if (*p == '\\' && p[1]) {
                word[n++] = *++p;
            } else if (*p == ' ' || *p == '\t' || *p == '\n') {
                break;
            } else {
                word[n++] = *p;
            }
        }
        if (quote) {
            free(word);
            return WRDE_SYNTAX;
        }
        word[n] = 0;
        char** grown = (char**) realloc(we->we_wordv, (we->we_wordc + 2) * sizeof(char*));
        if (grown == NULL) {
            free(word);
            return WRDE_NOSPACE;
        }
        we->we_wordv = grown;
        we->we_wordv[we->we_wordc++] = strdup(word);
        we->we_wordv[we->we_wordc] = NULL;
    }
    free(word);
    return WRDE_SUCCESS;
}
