
#ifndef H_XDICTLIB
 #define H_XDICTLIB
 #ifdef __cplusplus
   extern "C" {
 #endif

#include <stdlib.h>

/*
   The |xdict| interface.
*/

#define XDICT_MAXLENGTH 10  /* 0..9 characters */

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
int xdict_save_small(struct xdict *d, const char *fname);
void xdict_free(struct xdict *d);
int xdict_find(struct xdict *d, const char *pattern,
               int (*f)(const char *, void *), void *info);
  int xdict_match_simple(const char *w, const char *p);
  int xdict_match(const char *w, const char *p);

 #ifdef __cplusplus
   }
 #endif
#endif
