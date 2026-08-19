/* Minimal regex engine — public domain (Unlicense).
   Based on the tiny-regex-c approach by kokke.  */

#include "re.h"

#include <ctype.h>
#include <string.h>

#define RE_MAX_OBJECTS    64
#define RE_MAX_CLASS_LEN  64

typedef enum {
    RE_UNUSED = 0,
    RE_DOT,           /* .            any character           */
    RE_BEGIN,         /* ^            start-of-string anchor  */
    RE_END,           /* $            end-of-string anchor    */
    RE_QUESTIONMARK,  /* ?  modifier following an atom        */
    RE_STAR,          /* *  modifier following an atom        */
    RE_PLUS,          /* +  modifier following an atom        */
    RE_CHAR,          /* literal character                    */
    RE_CLASS,         /* [...]        character class         */
    RE_INV_CLASS,     /* [^...]       inverted class          */
    RE_DIGIT,         /* \d           [0-9]                   */
    RE_NOT_DIGIT,     /* \D                                   */
    RE_ALPHA,         /* \w           [a-zA-Z0-9_]            */
    RE_NOT_ALPHA,     /* \W                                   */
    RE_SPACE,         /* \s           whitespace              */
    RE_NOT_SPACE      /* \S                                   */
} re_type;

typedef struct {
    re_type       type;
    union {
        unsigned char  ch;
        unsigned char *ccl;
    } u;
} re_obj;

struct re_compiled_s {
    re_obj        obj[RE_MAX_OBJECTS];
    unsigned char ccl[RE_MAX_CLASS_LEN];
};

static struct re_compiled_s re_static;

/* ------------------------------------------------------------------ */
/* Internals                                                           */
/* ------------------------------------------------------------------ */

static int re_matchpattern(const re_obj *p, const char *text, int *ml);

static int re_matchcharclass(unsigned char c, const unsigned char *str)
{
    while (*str) {
        if (str[0] == '\\' && str[1]) {
            switch (str[1]) {
                case 'd': if ( isdigit(c))                return 1; break;
                case 'D': if (!isdigit(c))                return 1; break;
                case 'w': if ( isalnum(c) || c == '_')    return 1; break;
                case 'W': if (!(isalnum(c) || c == '_'))  return 1; break;
                case 's': if ( isspace(c))                return 1; break;
                case 'S': if (!isspace(c))                return 1; break;
                default:  if (c == str[1])                return 1; break;
            }
            str += 2;
        } else if (str[1] == '-' && str[2]) {
            if (c >= str[0] && c <= str[2]) return 1;
            str += 3;
        } else {
            if (c == str[0]) return 1;
            str++;
        }
    }
    return 0;
}

static int re_matchone(const re_obj *p, char c)
{
    switch (p->type) {
        case RE_DOT:       return c != '\0';
        case RE_CHAR:      return (unsigned char)c == p->u.ch;
        case RE_CLASS:     return  re_matchcharclass((unsigned char)c, p->u.ccl);
        case RE_INV_CLASS: return !re_matchcharclass((unsigned char)c, p->u.ccl);
        case RE_DIGIT:     return  isdigit((unsigned char)c) != 0;
        case RE_NOT_DIGIT: return !isdigit((unsigned char)c);
        case RE_ALPHA:     return  (isalnum((unsigned char)c) || c == '_');
        case RE_NOT_ALPHA: return !(isalnum((unsigned char)c) || c == '_');
        case RE_SPACE:     return  isspace((unsigned char)c) != 0;
        case RE_NOT_SPACE: return !isspace((unsigned char)c);
        default:           return 0;
    }
}

static int re_matchstar(re_obj p, const re_obj *rest, const char *text, int *ml)
{
    int orig  = *ml;
    int count = 0;
    while (text[count] && re_matchone(&p, text[count]))
        count++;
    /* Greedy: try longest match first, back off until rest matches. */
    while (count >= 0) {
        *ml = orig + count;
        if (re_matchpattern(rest, text + count, ml)) return 1;
        count--;
    }
    *ml = orig;
    return 0;
}

static int re_matchplus(re_obj p, const re_obj *rest, const char *text, int *ml)
{
    int orig  = *ml;
    int count = 0;
    while (text[count] && re_matchone(&p, text[count]))
        count++;
    while (count >= 1) {
        *ml = orig + count;
        if (re_matchpattern(rest, text + count, ml)) return 1;
        count--;
    }
    *ml = orig;
    return 0;
}

static int re_matchquestion(re_obj p, const re_obj *rest, const char *text, int *ml)
{
    int orig = *ml;
    if (*text && re_matchone(&p, *text)) {
        (*ml)++;
        if (re_matchpattern(rest, text + 1, ml)) return 1;
        *ml = orig;
    }
    return re_matchpattern(rest, text, ml);
}

static int re_matchpattern(const re_obj *p, const char *text, int *ml)
{
    for (;;) {
        if (p[0].type == RE_UNUSED)
            return 1;
        if (p[0].type == RE_END)
            return (*text == '\0');
        if (p[0].type == RE_BEGIN) {
            p++;
            continue;
        }
        if (p[1].type == RE_STAR)
            return re_matchstar(p[0], p + 2, text, ml);
        if (p[1].type == RE_PLUS)
            return re_matchplus(p[0], p + 2, text, ml);
        if (p[1].type == RE_QUESTIONMARK)
            return re_matchquestion(p[0], p + 2, text, ml);
        if (!*text || !re_matchone(p, *text))
            return 0;
        (*ml)++;
        text++;
        p++;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

re_t re_compile(const char *pattern)
{
    re_t re = &re_static;
    int  i  = 0;
    int  k  = 0;
    int  j  = 0;

    if (!pattern) return NULL;

    memset(re, 0, sizeof(*re));

    while (pattern[i] != '\0' && k < RE_MAX_OBJECTS - 1) {
        switch (pattern[i]) {
        case '^': re->obj[k].type = RE_BEGIN;        i++; k++; break;
        case '$': re->obj[k].type = RE_END;          i++; k++; break;
        case '.': re->obj[k].type = RE_DOT;          i++; k++; break;
        case '*': re->obj[k].type = RE_STAR;         i++; k++; break;
        case '+': re->obj[k].type = RE_PLUS;         i++; k++; break;
        case '?': re->obj[k].type = RE_QUESTIONMARK; i++; k++; break;

        case '\\':
            i++;
            if (pattern[i] == '\0') goto done;
            switch (pattern[i]) {
            case 'd': re->obj[k].type = RE_DIGIT;     break;
            case 'D': re->obj[k].type = RE_NOT_DIGIT; break;
            case 'w': re->obj[k].type = RE_ALPHA;     break;
            case 'W': re->obj[k].type = RE_NOT_ALPHA; break;
            case 's': re->obj[k].type = RE_SPACE;     break;
            case 'S': re->obj[k].type = RE_NOT_SPACE; break;
            default:
                re->obj[k].type  = RE_CHAR;
                re->obj[k].u.ch  = (unsigned char)pattern[i];
                break;
            }
            i++; k++;
            break;

        case '[': {
            i++;
            if (pattern[i] == '^') {
                re->obj[k].type = RE_INV_CLASS;
                i++;
            } else {
                re->obj[k].type = RE_CLASS;
            }
            re->obj[k].u.ccl = &re->ccl[j];
            while (pattern[i] != '\0' && pattern[i] != ']' &&
                   j < RE_MAX_CLASS_LEN - 2) {
                if (pattern[i] == '\\' && pattern[i + 1] != '\0') {
                    re->ccl[j++] = '\\';
                    re->ccl[j++] = (unsigned char)pattern[++i];
                } else {
                    re->ccl[j++] = (unsigned char)pattern[i];
                }
                i++;
            }
            re->ccl[j++] = '\0';
            if (pattern[i] == ']') i++;
            k++;
            break;
        }

        default:
            re->obj[k].type = RE_CHAR;
            re->obj[k].u.ch = (unsigned char)pattern[i];
            i++; k++;
            break;
        }
    }
done:
    re->obj[k].type = RE_UNUSED;
    return re;
}

int re_matchp(re_t re, const char *text, int *matchlength)
{
    const char *t;
    *matchlength = 0;
    if (!re || !text) return -1;

    if (re->obj[0].type == RE_BEGIN) {
        return re_matchpattern(&re->obj[1], text, matchlength) ? 0 : -1;
    }

    t = text;
    for (;;) {
        *matchlength = 0;
        if (re_matchpattern(re->obj, t, matchlength))
            return (int)(t - text);
        if (*t == '\0') break;
        t++;
    }
    return -1;
}

int re_match(const char *pattern, const char *text, int *matchlength)
{
    return re_matchp(re_compile(pattern), text, matchlength);
}
