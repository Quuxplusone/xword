
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xdictlib.h"

enum {
    POS_NORMAL, POS_PLURAL, POS_VERB, POS_VERBE, POS_VERBB,
    POS_COVERED
};

#define is_consonant(k) (strchr("bcdfghjklmnpqrstvwxyz",k) != NULL)
#define is_vowel(k) (strchr("aeiouy",k) != NULL)


/*
   For the time being, the |xdict| data is stored to disk as a single 
   gigantic text file, containing all the words in the dictionary in
   plain text separated by newlines.

   Update, 16 March 2005: Related words may be stored in the file as
   "foo/s", indicating that both "foo" and "foos" are words; as
   "foo/v", indicating that "foo", "foos", "fooed", and "fooing" are
   words; as "foo/w", indicating that "fooe", "fooes", "fooed", and
   "fooing" are words; or as "fop/x", indicating that "fop", "fops",
   "fopped", and "fopping" are words. It is allowed for "foo/v", for
   example, to indicate the presence of the shorter word forms, even
   when "fooing" has more than |XDICT_MAXLENGTH| letters.
*/
int xdict_load(struct xdict *d, const char *fname)
{
    FILE *in = fopen(fname, "r");
    char buffer[XDICT_MAXLENGTH+25];
    int rc = 0;
    if (in == NULL)  return -1;
    while (fgets(buffer, (sizeof buffer)-5, in) != NULL) {
        char *p = strchr(buffer, '\n');
        int buflen, rc;
        if (p == NULL)  { rc = -2; goto done; }
        *p = '\0';
        buflen = (p - buffer);
        p = strchr(buffer, '/');
        if (p != NULL) {
            switch (p[1]) {
                case 's': case 'S':
                    rc = xdict_addword(d, buffer, p - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "s");
                    rc = xdict_addword(d, buffer, (p+1) - buffer);
                    if (rc < -1) goto done;
                    break;
                case 'v': case 'V':
                    rc = xdict_addword(d, buffer, p - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "s");
                    rc = xdict_addword(d, buffer, (p+1) - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "ed");
                    rc = xdict_addword(d, buffer, (p+2) - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "ing");
                    rc = xdict_addword(d, buffer, (p+3) - buffer);
                    if (rc < -1) goto done;
                    break;
                case 'w': case 'W':
                    strcpy(p, "e");
                    rc = xdict_addword(d, buffer, (p+1) - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "es");
                    rc = xdict_addword(d, buffer, (p+2) - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "ed");
                    rc = xdict_addword(d, buffer, (p+2) - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "ing");
                    rc = xdict_addword(d, buffer, (p+3) - buffer);
                    if (rc < -1) goto done;
                    break;
                case 'x': case 'X':
                    rc = xdict_addword(d, buffer, p - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "s");
                    rc = xdict_addword(d, buffer, (p+1) - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "xed");
                    p[0] = p[-1];
                    rc = xdict_addword(d, buffer, (p+3) - buffer);
                    if (rc < -1) goto done;
                    strcpy(p, "xing");
                    p[0] = p[-1];
                    rc = xdict_addword(d, buffer, (p+4) - buffer);
                    if (rc < -1) goto done;
                    break;
                default:
                    rc = xdict_addword(d, buffer, buflen);
                    if (rc < -1) goto done;
                    break;
            }
        }
        else {
            rc = xdict_addword(d, buffer, buflen);
            if (rc < -1) goto done;
        }
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


/*
   Given an entry in the dictionary, determine whether it is a "root"
   of a verb or plural construction ("bake"), or a normal word with no
   derivative words in the dictionary ("whoever"), or a word that will
   be covered by some other root word ("baking"). Some words may be
   covered twice; for example, "pol/v" and "pol/w" both cover "poling"
   in a dictionary containing "pol" and "pols" as well as "pole",
   "poles", and "poled". Some words may appear at first glance to
   be covered when they're not; for example, "princess" is not covered
   by "princes" if the dictionary also contains "prince". To deal with
   the "princes/princess" problem, we allow |pos_categorize| to call
   itself recursively, but only on /shorter/ words, never longer ones!
   Then consider the entry "fling/v", which is a root word even though
   it ends in "-ing". This shows that we must check each word for rootness
   as well as coveredness.

   Todo: Given "car, cars, care, cares, cared, caring", we incorrectly
   store both "car/v" and "car/w", leading to duplicate entries for
   "cared" and "caring". This is incorrect behavior.
*/
static int pos_categorize(struct xdict *d, const char *word, int k)
{
    char buffer[XDICT_MAXLENGTH+4];
    int ends_with_ing = (k >= 6 && !memcmp(word+k-3, "ing", 3));
    int ends_with_ed = (k >= 5 && !memcmp(word+k-2, "ed", 2));
    int ends_with_es = (k >= 4 && !memcmp(word+k-2, "es", 2));
    int ends_with_s = (k >= 4 && (word[k-1] == 's'));
    int ends_with_e = (k >= 3 && (word[k-1] == 'e'));
    int rc;

    if (ends_with_s || ends_with_es) {
        sprintf(buffer, "%.*s", k-1, word);
        if (xdict_find(d, buffer, NULL, NULL) > 0) {
            rc = pos_categorize(d, buffer, k-1);
            if (rc == POS_VERB || rc == POS_VERBE ||
                rc == POS_VERBB || rc == POS_PLURAL)
              return POS_COVERED;
        }
    }
    else if (ends_with_ed) {
        sprintf(buffer, "%.*s", k-2, word);
        if (xdict_find(d, buffer, NULL, NULL) > 0) {
            rc = pos_categorize(d, buffer, k-2);
            if (rc == POS_VERB) return POS_COVERED;
        }
        sprintf(buffer, "%.*se", k-2, word);
        if (xdict_find(d, buffer, NULL, NULL) > 0) {
            rc = pos_categorize(d, buffer, k-1);
            if (rc == POS_VERBE) return POS_COVERED;
        }
        if (word[k-4] == word[k-3]) {
            sprintf(buffer, "%.*s", k-3, word);
            if (xdict_find(d, buffer, NULL, NULL) > 0) {
                rc = pos_categorize(d, buffer, k-3);
                if (rc == POS_VERBB) return POS_COVERED;
            }
        }
    }
    else if (ends_with_ing) {
        sprintf(buffer, "%.*s", k-3, word);
        if (xdict_find(d, buffer, NULL, NULL) > 0) {
            rc = pos_categorize(d, buffer, k-3);
            if (rc == POS_VERB) return POS_COVERED;
        }
        sprintf(buffer, "%.*se", k-3, word);
        if (xdict_find(d, buffer, NULL, NULL) > 0) {
            rc = pos_categorize(d, buffer, k-2);
            if (rc == POS_VERBE) return POS_COVERED;
        }
        if (word[k-5] == word[k-4]) {
            sprintf(buffer, "%.*s", k-4, word);
            if (xdict_find(d, buffer, NULL, NULL) > 0) {
                rc = pos_categorize(d, buffer, k-4);
                if (rc == POS_VERBB) return POS_COVERED;
            }
        }
    }

    /*
       Okay, this word isn't covered by any shorter root word.
       Is it a root word itself?
    */
    if (ends_with_e) {
        sprintf(buffer, "%.*ses", k-1, word);
        if (xdict_find(d, buffer, NULL, NULL) <= 0) return POS_NORMAL;
        sprintf(buffer, "%.*sing", k-1, word);
        if (xdict_find(d, buffer, NULL, NULL) == 0) return POS_PLURAL;
        sprintf(buffer, "%.*sed", k-1, word);
        if (xdict_find(d, buffer, NULL, NULL) == 0) return POS_PLURAL;
        return POS_VERBE;
    }
    else {
        int has_taping, has_taped;
        sprintf(buffer, "%ss", word);
        if (xdict_find(d, buffer, NULL, NULL) <= 0) return POS_NORMAL;

        /* Look for "taping", if the root word is "tap". */
        sprintf(buffer, "%sing", word);
        has_taping = xdict_find(d, buffer, NULL, NULL);
        if (has_taping == 0) goto look_for_tapping;

        /* The entry "taping" was found (or too long). Look for "taped". */
        sprintf(buffer, "%sed", word);
        has_taped = xdict_find(d, buffer, NULL, NULL);
        if (has_taped == 0) goto look_for_tapping;
        if (has_taped > 0 || has_taping > 0) return POS_VERB;
        return POS_PLURAL;

      look_for_tapping:        
        sprintf(buffer, "%s%cing", word, word[k-1]);
        if (xdict_find(d, buffer, NULL, NULL) == 0) return POS_PLURAL;
        sprintf(buffer, "%s%ced", word, word[k-1]);
        if (xdict_find(d, buffer, NULL, NULL) <= 0) return POS_PLURAL;
        return POS_VERBB;
    }
}

/*
   This routine saves out the dictionary using the "updated" compression
   scheme: we look for plurals and verb tenses while saving the
   dictionary. This will run very slowly if the last operation on the
   dictionary didn't leave it sorted!
*/
int xdict_save_small(struct xdict *d, const char *fname)
{
    FILE *out = fopen(fname, "w");
    size_t i;
    int k;

    if (out == NULL)  return -1;
    for (k=0; k < XDICT_MAXLENGTH; ++k) {
        for (i=0; i < d->len[k]; ++i) {
            char *word = d->words[k][i].word;
            switch (pos_categorize(d, word, k)) {
                case POS_NORMAL:
                    fprintf(out, "%s\n", word);
                    break;
                case POS_VERB:
                    fprintf(out, "%s/v\n", word);
                    break;
                case POS_VERBE:
                    fprintf(out, "%.*s/w\n", k-1, word);
                    break;
                case POS_VERBB:
                    fprintf(out, "%s/x\n", word);
                    break;
                case POS_PLURAL:
                    fprintf(out, "%s/s\n", word);
                    break;
                case POS_COVERED: break;
            }
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

