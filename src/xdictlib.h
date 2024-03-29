
#ifndef H_XDICTLIB
 #define H_XDICTLIB

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
   The |xdict| interface.
*/

#define XDICT_MAXLENGTH 16  /* 0..15 characters */

struct xdict {
    struct word_entry *words[XDICT_MAXLENGTH];
    size_t cap[XDICT_MAXLENGTH];
    size_t len[XDICT_MAXLENGTH];
    int sorted;
};

struct word_entry {
    char *word;
};


void xdict_init(struct xdict *d);
int xdict_load(struct xdict *d, const char *fname);
  int xdict_addword(struct xdict *d, const char *word, int len);
  int xdict_remword(struct xdict *d, const char *word, int len);
  int xdict_remmatch(struct xdict *d, const char *pat, int len);
void xdict_sort(struct xdict *d);
int xdict_save(struct xdict *d, const char *fname);
void xdict_free(struct xdict *d);
int xdict_find(struct xdict *d, const char *pattern,
               int (*f)(const char *, void *), void *info);
  int xdict_match_simple(const char *w, const char *p);
  int xdict_match(const char *w, const char *p);
int xdict_find_scrabble(struct xdict *d, const char *rack, const char *mustuse,
                        int (*f)(const char *, void *), void *info);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
