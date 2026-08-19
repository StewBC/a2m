/* Minimal regex engine — public domain (Unlicense).
   Based on the tiny-regex-c approach by kokke.
   Supports: . ^ $ * + ? [abc] [^abc] \d \D \w \W \s \S   */
#pragma once

typedef struct re_compiled_s *re_t;

/* Compile a regex pattern into a reusable compiled form.
   Returns a pointer to a static internal buffer — not thread-safe,
   and invalidated by the next re_compile call.
   Returns NULL only if pattern is NULL. */
re_t re_compile(const char *pattern);

/* Find the first match of compiled pattern in text.
   Returns the start index (>= 0) of the match, or -1 if none.
   Sets *matchlength to the number of characters matched. */
int re_matchp(re_t pattern, const char *text, int *matchlength);

/* Convenience: compile pattern then match text in one call. */
int re_match(const char *pattern, const char *text, int *matchlength);
