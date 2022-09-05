
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xdictlib.h"

#define is_consonant(k) (strchr("bcdfghjklmnpqrstvwxyz",k) != NULL)
#define is_vowel(k) (strchr("aeiouy",k) != NULL)

/*
   For the time being, the |xdict| data is stored to disk as a single
   gigantic text file, containing all the words in the dictionary in
   plain text separated by newlines.
*/
int xdict_load(struct xdict *d, const char *fname)
{
    FILE *in = fopen(fname, "r");
    char buffer[XDICT_MAXLENGTH+25];
    int rc = 0;
    if (in == NULL)  return -1;
    while (fgets(buffer, (sizeof buffer)-5, in) != NULL) {
        char *p = strchr(buffer, '\n');
        int rc;
        if (p == NULL)  { rc = -2; goto done; }
        *p = '\0';
        rc = xdict_addword(d, buffer, p - buffer);
        if (rc < -1) goto done;
    }
  done:
    fclose(in);
    xdict_sort(d);
    return rc;
}


int xdict_save(struct xdict *d, const char *fname)
{
    FILE *out = fopen(fname, "w");
    size_t i;
    int k;
    if (out == NULL)  return -1;
    for (k=0; k < XDICT_MAXLENGTH; ++k) {
        for (i=0; i < d->len[k]; ++i) {
            fprintf(out, "%s\n", d->words[k][i].word);
        }
    }
    fclose(out);
    return 0;
}


void xdict_init(struct xdict *d)
{
    int k;
    d->sorted = 1;
    for (k=0; k < XDICT_MAXLENGTH; ++k) {
        d->words[k] = NULL;
        d->len[k] = d->cap[k] = 0;
    }
}


static int xdict_sortcmp(const void *p, const void *q)
{
    const struct word_entry *wp = p;
    const struct word_entry *wq = q;
    return strcmp(wp->word, wq->word);
}

void xdict_sort(struct xdict *d)
{
    int k;
    for (k=0; k < XDICT_MAXLENGTH; ++k) {
        size_t i, n = d->len[k];
        struct word_entry *w = d->words[k];
        if (n < 2) continue;
        qsort(w, n, sizeof *w, xdict_sortcmp);
        /* Remove duplicates. */
        for (i=n; i > 1; --i) {
            if (strcmp(w[i-1].word, w[i-2].word) == 0) {
                if (i < --n)
                  memcpy(w[i-1].word, w[n].word, k);
            }
        }
        if (n < d->len[k]) {
            d->len[k] = n;
            qsort(w, n, sizeof *w, xdict_sortcmp);
        }
    }
    d->sorted = 1;
}


void xdict_free(struct xdict *d)
{
    size_t i, k;
    for (k=0; k < XDICT_MAXLENGTH; ++k) {
        for (i=0; i < d->len[k]; ++i)
          free(d->words[k][i].word);
        free(d->words[k]);
    }
}


int xdict_addword(struct xdict *d, const char *word, int k)
{
    char *tmp;
    if (k == 0) k = strlen(word);
    if (k >= XDICT_MAXLENGTH) return -1;
    if (k <= 2) return -1;
    if (d->len[k] >= d->cap[k]) {
        size_t newcap = d->cap[k] * 2 + 15;
        void *t = realloc(d->words[k], newcap * sizeof *d->words[k]);
        if (t == NULL) return -3;
        d->words[k] = t;
        d->cap[k] = newcap;
    }
    tmp = malloc(k+1);
    if (tmp == NULL) return -4;
    memcpy(tmp, word, k); tmp[k] = '\0';
    d->words[k][d->len[k]++].word = tmp;
    d->sorted = 0;
    return 0;
}


int xdict_remword(struct xdict *d, const char *word, int k)
{
    int count = 0;
    struct word_entry *w;
    size_t i;

    if (k == 0) k = strlen(word);
    if (k >= XDICT_MAXLENGTH) return -1;
    if (k <= 2) return -1;
    w = d->words[k];
    for (i=0; i < d->len[k]; ++i) {
        if (memcmp(w[i].word, word, k) == 0) {
            d->len[k]--;
            if (i != d->len[k]) {
                strcpy(w[i].word, w[d->len[k]].word);
                d->sorted = 0;
            }
            free(w[d->len[k]].word);
            ++count;
        }
    }
    return count;
}


int xdict_remmatch(struct xdict *d, const char *pat, int k)
{
    if (k != 0 && strchr(pat, '*') == NULL) {
        if (strchr(pat, '?') == NULL)
          return xdict_remword(d, pat, k);
        else {
            int count = 0;
            struct word_entry *w;
            size_t i;
            if (k == 0) k = strlen(pat);
            if (k >= XDICT_MAXLENGTH) return -1;
            if (k <= 2) return -1;
            w = d->words[k];
            for (i=0; i < d->len[k]; ++i) {
                if (xdict_match(w[i].word, pat)) {
                    d->len[k]--;
                    if (i != d->len[k]) {
                        strcpy(w[i].word, w[d->len[k]].word);
                        d->sorted = 0;
                    }
                    free(w[d->len[k]].word);
                    ++count;
                }
            }
            return count;
        }
    }
    else {
        /*
           The pattern does contain a '*' wildcard, and the client
           has not specified a word length.  We must check all possible
           word lengths.
        */
        int count = 0;
        size_t i;
        for (i=k=0; pat[i]; ++i)
          if (pat[i] != '*') ++k;
        for ( ; k < XDICT_MAXLENGTH; ++k)
        {
            struct word_entry *w = d->words[k];
            for (i=0; i < d->len[k]; ++i) {
                if (xdict_match(w[i].word, pat)) {
                    d->len[k]--;
                    if (i != d->len[k]) {
                        strcpy(w[i].word, w[d->len[k]].word);
                        d->sorted = 0;
                    }
                    free(w[d->len[k]].word);
                    ++count;
                }
            }
        }
        return count;
    }
}


int xdict_match(const char *w, const char *p)
{
    int i, j;
    for (i=0; p[i]; ++i) {
        if (p[i] == '*') {
            for (j=i; w[j]; ++j)
              if (xdict_match(w+j, p+i+1)) return 1;
            if (xdict_match(w+j, p+i+1)) return 1;
            return 0;
        }
        else if (w[i] == '\0') return 0;
        else if (p[i] == '1') { if (!is_consonant(w[i])) return 0; }
        else if (p[i] == '0') { if (!is_vowel(w[i])) return 0; }
        else if (p[i] != '?') { if (p[i] != w[i]) return 0; }
    }
    return (w[i] == '\0');
}


int xdict_match_simple(const char *w, const char *p)
{
    int i;
    for (i=0; p[i]; ++i) {
        if (w[i] == '\0') return 0;
        else if (p[i] == '1') { if (!is_consonant(w[i]))  return 0; }
        else if (p[i] == '0') { if (!is_vowel(w[i]))  return 0; }
        else if (p[i] != '?') { if (p[i] != w[i])  return 0; }
    }
    return (w[i] == '\0');
}


static int is_purely_alphabetic(const char *pattern)
{
    size_t i;
    for (i=0; isalpha(pattern[i]); ++i)
      continue;
    return (pattern[i] == '\0');
}

int xdict_find(struct xdict *d, const char *pattern,
               int (*f)(const char *, void *), void *info)
{
    int count = 0;

    if (strchr(pattern, '*') == NULL) {
        size_t len = strlen(pattern);
        struct word_entry *w;

        if (len < 2 || len >= XDICT_MAXLENGTH) return -1;
        w = d->words[len];

        if (d->sorted && is_purely_alphabetic(pattern)) {
            /*
               If the dictionary is sorted and the pattern is
               simply a word, we can perform a speedy binary search
               instead of our usual slow linear search.
            */
            size_t low = 0;
            size_t high = d->len[len];
            while (low < high) {
                size_t i = low + (high-low)/2;
                int rc = strcmp(w[i].word, pattern);
                if (rc == 0) {
                    if (f != NULL)
                      f(w[i].word, info);
                    return 1;
                }
                else if (rc > 0) {
                    high = i;
                }
                else {
                    low = i+1;
                }
            }
            return 0;
        }
        else {
            size_t i;
            for (i=0; i < d->len[len]; ++i) {
                if (xdict_match_simple(w[i].word, pattern)) {
                    ++count;
                    if (f && f(w[i].word, info)) return count;
                }
            }
            return count;
        }
    }
    else {
        size_t k, len=0;
        for (k=0; pattern[k]; ++k)
          if (pattern[k] != '*') ++len;

        for (k=len; k < XDICT_MAXLENGTH; ++k) {
            struct word_entry *w = d->words[k];
            size_t i;
            for (i=0; i < d->len[k]; ++i) {
                if (xdict_match(w[i].word, pattern)) {
                    ++count;
                    if (f && f(w[i].word, info)) return count;
                }
            }
        }
        return count;
    }
}

static int xdict_match_scrabble(const char *w, const int *mincounts, const int *maxcounts)
{
    int counts[256] = {0};
    for (int i=0; w[i] != '\0'; ++i) {
        int ch = (unsigned char)w[i];
        if (counts[ch] < maxcounts[ch]) {
            counts[ch] += 1;
        } else if (is_vowel(w[i]) && counts['0'] < maxcounts['0']) {
            counts['0'] += 1;
        } else if (is_consonant(w[i]) && counts['1'] < maxcounts['1']) {
            counts['1'] += 1;
        } else if (counts['?'] < maxcounts['?']) {
            counts['?'] += 1;
        } else {
            return 0;
        }
    }
    for (int ch = 0; ch < 256; ++ch) {
        if (counts[ch] < mincounts[ch]) {
            return 0;
        }
    }
    return 1;
}

int xdict_find_scrabble(struct xdict *d, const char *rack, const char *mustuse,
                        int (*f)(const char *, void *), void *info)
{
    int count = 0;
    int mincounts[256] = {0};
    int maxcounts[256] = {0};
    for (int i = 0; rack[i] != '\0'; ++i) {
        int ch = (unsigned char)rack[i];
        maxcounts[ch] += 1;
    }
    for (int i = 0; mustuse[i] != '\0'; ++i) {
        int ch = (unsigned char)mustuse[i];
        mincounts[ch] += 1;
    }
    size_t minlen = strlen(mustuse) > 2 ? strlen(mustuse) : 2;
    size_t maxlen = strlen(rack)+1 < XDICT_MAXLENGTH ? strlen(rack)+1 : XDICT_MAXLENGTH;
    for (size_t len = minlen; len < maxlen; ++len) {
        struct word_entry *w = d->words[len];
        for (size_t i=0; i < d->len[len]; ++i) {
            if (xdict_match_scrabble(w[i].word, mincounts, maxcounts)) {
                ++count;
                if (f && f(w[i].word, info)) return count;
            }
        }
    }
    return count;
}
