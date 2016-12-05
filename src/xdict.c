
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xdictlib.h"

/* The text file where the dictionary is stored */
#define XDICT_SAVE_TXT "xdict.save.txt"

/*
   Define this macro if the operating system will free our memory on exit;
   otherwise it may take a long time for the program to free individually
   all the little chunks we've allocated.
*/
#define FAST_EXIT 1

#define NELEM(a) ((int)(sizeof a / sizeof *a))
#define PLUR(x) (&"s"[(x)==1])

int printme(const char *s, void *info_dummy);
void engraveme(void);
int display_set(const char *s, void *info);
void do_error(const char *fmt, ...);
void do_help(void);
void do_man(int page_height);
 void page(const char *s);


int main(void)
{
    struct xdict dict;
    int modified = 0;
    char cmd[100];
    int rc;

    xdict_init(&dict);
    puts("Inited successfully");
    switch (rc = xdict_load(&dict, XDICT_SAVE_TXT)) {
        case -1: do_error("Dictionary not found");
        case -2: do_error("Dictionary corrupted");
        case -3: do_error("Out of memory");
        case -4: do_error("Out of memory");
    }
    puts("Loaded successfully. Type HELP for details.");

    while (fgets(cmd, sizeof cmd, stdin) != NULL) {
        if (strchr(cmd, '\n') == NULL) {
            puts("Input line too long. Ignoring and continuing");
            while (getchar() != '\n') ;
            continue;
        }
        if (strncmp(cmd, "ADD ", 4) == 0) {
            int start, end;
            int rc2, two_words = 0;
            for (start=4; isspace(cmd[start]); ++start);
            for (end=start; isalpha(cmd[end]); ++end)
              cmd[end] = tolower(cmd[end]);
            /* Allow "ADD foo/s" to add both words. */
            rc2 = 0;
            if (cmd[end] == '/') {
                if (cmd[end+1] == 's') {
                    two_words = 1;
                    strcpy(cmd+end, "s");
                    rc2 = xdict_addword(&dict, cmd+start, end+1-start);
                }
            }
            cmd[end] = '\0';
            rc = xdict_addword(&dict, cmd+start, end-start);
            if (!rc || !rc2)
              modified++;
            if (!rc && !rc2)
              puts("Added successfully.");
            else if (rc == -3 || rc == -4 || rc2 == -3 || rc2 == -4)
              do_error("Out of memory");
            else if (rc == -1 || rc2 == -1) {
                int both = (rc == -1) && (rc2 == -1);
                printf("Failed to add %sword%s; continuing.\n",
                    (!two_words || both)? "":
                        rc? "the shorter ": "the longer ",
                    both? "s": "");
            }
        }
        else if (strncmp(cmd, "REM ", 4) == 0) {
            int start, end;
            for (start=4; isspace(cmd[start]); ++start);
            for (end=start; !isspace(cmd[end]); ++end)
              cmd[end] = tolower(cmd[end]);
            cmd[end] = '\0';
            rc = xdict_remmatch(&dict, cmd+start, 0);
            if (rc < 0)  puts("Failed to remove word; continuing.");
            else if (rc == 0)  puts("Word not found; continuing.");
            else {
                puts("Removed successfully.");
                modified++;
            }
        }
        else if (strncmp(cmd, "SET ", 4) == 0) {
            int start, end, index = -1;
            for (start=4; isspace(cmd[start]); ++start);
            for (end=start; !isspace(cmd[end]); ++end) {
                if (cmd[end] == '_')
                  cmd[end] = '?', index = end-start;
                else
                  cmd[end] = tolower(cmd[end]);
            }
            cmd[end] = '\0';
            if (index < 0) {
                puts("Set action requires a '_' marker!");
            }
            else {
                rc = xdict_find(&dict, cmd+start, display_set, &index);
                if (rc < 0)  puts("Set action failed; continuing.");
                else if (rc == 0)  puts("No matching words found; continuing.");
                else  display_set(NULL, NULL);
            }
        }
        else if (strcmp(cmd, "SORT\n") == 0) {
            xdict_sort(&dict);
            puts("Done.");
        }
        else if (strcmp(cmd, "STAT\n") == 0) {
            int i, total = 0;
            for (i=0; i < XDICT_MAXLENGTH; ++i)
              total += dict.len[i];
            printf("Max. word length is %d\n", XDICT_MAXLENGTH-1);
            printf("Total word count is %d\n", total);
            printf("%d modification%s; %ssorted\n",
                modified, PLUR(modified), dict.sorted? "": "not ");
        }
        else if (strcmp(cmd, "SAVE\n") == 0) {
            if (!dict.sorted) {
                puts("Sorting dictionary...");
                xdict_sort(&dict);
            }
            if (xdict_save_small(&dict, XDICT_SAVE_TXT))
              puts("Dictionary not saved");
            puts("Saved successfully.");
            modified = 0;
        }
        else if (strcmp(cmd, "SAVEA\n") == 0) {
            if (xdict_save(&dict, XDICT_SAVE_TXT))
              puts("Dictionary not saved");
            puts("Saved successfully");
            modified = 0;
        }
        else if (!strcmp(cmd, "QUIT\n") || !strcmp(cmd, "EXIT\n")) {
            break;
        }
        else if (strcmp(cmd, "HELP\n") == 0) {
            do_help();
        }
        else if (strcmp(cmd, "HELP VERBOSE\n") == 0) {
            do_man(1000);
        }
        else if (strncmp(cmd, "HELP VERBOSE ", 13) == 0) {
            int start, end;
            int oops = 0;
            for (start=13; isspace(cmd[start]); ++start);
            for (end=start; !isspace(cmd[end]); ++end) {
                if (!isdigit(cmd[end]))
                  oops = 1;
            }
            cmd[end] = '\0';
            if (oops || (end > start+3)) {
                do_man(1000);
            }
            else {
                do_man(atoi(&cmd[start]));
            }
        }
        else {
            int start, end;
            for (start=0; isspace(cmd[start]); ++start) ;
            if (cmd[start] == '\0') {
                printf("(Ctrl-D to quit)\n");
                goto next_loop;
            }
            for (end=start; !isspace(cmd[end]); ++end)
              cmd[end] = tolower(cmd[end]);
            cmd[end] = '\0';
            rc = xdict_find(&dict, cmd+start, printme, NULL);
            engraveme(); printf("%d\n", rc);
        next_loop: ;
        }
    }

    puts("Wait...");
    if (modified > 0) {
        printf("%d modification%s\n", modified, PLUR(modified));
        if (!dict.sorted) {
            puts("Sorting dictionary...");
            xdict_sort(&dict);
        }
        if (xdict_save_small(&dict, XDICT_SAVE_TXT))
          do_error("Dictionary not saved");
        puts("Saved successfully");
    }
#if FAST_EXIT
#else
    puts("Freeing memory...");
    xdict_free(&dict);
#endif
    puts("Done.");
    return 0;
}


/*
   The two routines |printme| and |engraveme| work in tandem to give the
   user a nice view of the matching words.  The |printme| routine adds
   its argument word to a static list, and the |engraveme| routine prints
   that list in columnar format and clears it in preparation for the
   next |printme| cycle.
*/

static char (*glob_print_buffer)[XDICT_MAXLENGTH] = NULL;
static size_t glob_print_len = 0, glob_print_cap = 0;

int printme(const char *s, void *info_dummy)
{
    (void)info_dummy;  /* This parameter is not used. */

    if (glob_print_len >= glob_print_cap) {
        size_t newcap = 2 * glob_print_cap + 16;
        void *t = realloc(glob_print_buffer, newcap * sizeof *glob_print_buffer);
        if (t == NULL) {
            if (glob_print_len > 0) {
                engraveme(); printme(s, NULL); return 0;
            }
            else  do_error("Out of memory for search results");
        }
        glob_print_buffer = t;
        glob_print_cap = newcap;
    }
    strncpy(glob_print_buffer[glob_print_len++], s, XDICT_MAXLENGTH);
    glob_print_buffer[glob_print_len-1][XDICT_MAXLENGTH-1] = '\0';
    return 0;
}

void engraveme(void)
{
    /*
       We print our results in columns with a total page width of 60 
       chars, leaving at least 4 spaces between columns and keeping
       at least three vertical rows whenever possible.
    */
    int n = glob_print_len;
    int colwidth=3;
    int i, r, cols, rows, x;

    for (i=0; i < n; ++i) {
        int k = strlen(glob_print_buffer[i]);
        if (k > colwidth)  colwidth = k;
    }
    cols = 65/(colwidth+4);
    if (cols > n/3) cols = n/3;
    if (cols < 1) cols = 1;
    rows = (n+cols-1)/cols;
    x = cols*rows - n;

    /* Print using |cols| columns. */
    for (r=0; r < rows; ++r) {
        for (i=r; i < n; i += rows) {
            printf("%s%-*s", (i==r)? "": "    ",
                      colwidth, glob_print_buffer[i]);
            if (r==rows-1 && i >= r+rows*(cols-x-1)) break;
            if (i+1 > (cols-x)*rows) --i;
        }
        printf("\n");
    }

    /* Free the static buffer. */
    free(glob_print_buffer);
    glob_print_buffer = NULL;
    glob_print_len = glob_print_cap = 0;
    return;
}


int display_set(const char *s, void *info)
{
    static char buf[CHAR_MAX-CHAR_MIN] = {0};
    if (s == NULL) {
        /* Display the accumulated letters and reset the buffer. */
        int i, found_some = 0;
        for (i=0; i < NELEM(buf); ++i) {
            if (buf[i]) printf("%c", i+CHAR_MIN), found_some = 1;
            buf[i] = 0;
        }
        if (found_some)  printf("\n");
    }
    else {
        /* Add a new word to the buffer. */
        const int *p = info;
        buf[s[*p]-CHAR_MIN] = 1;
    }
    return 0;
}


void do_error(const char *fmat, ...)
{
    va_list ap;
    printf("xdict: ");
    va_start(ap, fmat);
    vprintf(fmat, ap);
    printf("\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}


void do_help(void)
{
    puts("All meta-commands must be entered in upper case.");
    puts("HELP          This message");
    puts("HELP VERBOSE  Complete man pages for xdict");
    puts("QUIT, EXIT    (Save and) exit, the same as Ctrl-D");
    puts("SAVE          Save compressed word list into " XDICT_SAVE_TXT);
    puts("SAVEA         Save uncompressed word list");
    puts("SORT          Sort the dictionary");
    puts("STAT          Display some statistical details");
    puts("ch0rtl*       Display matching word(s)");
    puts("SET ch_rtl*   Display set of crossing letters");
    puts("ADD chortle   Add a word to the dictionary");
    puts("REM ch0rtl*   Remove word(s) from the dictionary");
    puts("");
    puts("set           Matches the word \"set\" only");
    puts("b0g           Vowel: matches bag, beg, big,... but not bfg");
    puts("do1           Consonant: matches doc, dog, don,... but not doe");
    puts("do?           Single letter: matches doc, doe, dog,...");
    puts("do*t          Any string: matches dot, doubt, donut,...");
}


static int glob_pageheight;
static int glob_paralines = 0;

void do_man(int page_height)
{
    glob_pageheight = page_height-1;
    page(NULL);
    glob_paralines = 4;
    page("xdict: Crossword dictionary utility.\n");
    page("  The 'xdict' utility is a crossword dictionary. It supports");
    page("various kinds of wildcard searches, including restricting");
    page("the wildcards to vowels or consonants.");
    glob_paralines = 5;
    page("  The word list for the dictionary is stored in the text file");
    page("'" XDICT_SAVE_TXT "'. That file in its simplest form is just");
    page("a list of words: one word per line. Words must be completely");
    page("alphabetic, and can't have any embedded spaces; capitalization");
    page("is irrelevant.");
    glob_paralines = 11;
    page("  By default, the word list is saved in a slightly more complex");
    page("format, to save disk space. In the compressed format, the pair");
    page("of words \"bed\" and \"beds\" would be stored as \"bed/s\" (on a");
    page("single line). The regular verb \"add, adds, added, adding\" is");
    page("stored as \"add/v\". There are also two other regular verb");
    page("constructions, exemplified by \"tap/w\" (for the verb \"tape\")");
    page("and \"tap/x\" (for the verb \"tap\"). This is a purely lossless");
    page("and unambiguous form of compression, and very human-friendly,");
    page("but it does make the file format rather idiosyncratic. Therefore,");
    page("'xdict' provides the user meta-command SAVEA, which stores the");
    page("dictionary word list in the \"simplest form\" detailed above.");
    glob_paralines = 10;
    page("  The user meta-command SAVE saves the word list in compressed");
    page("form. It will sort the dictionary first, if needed.");
    page("  When the program exits normally --- upon encountering the");
    page("end-of-file marker or one of the user meta-commands QUIT and EXIT");
    page("--- it will check to see whether the dictionary has been modified");
    page("by any ADD or REM commands since the last time it was saved. If");
    page("the word list has been modified, then it will sort the list and");
    page("save it in the compressed format. If the word list is unmodified,");
    page("the program will free its resources and exit without performing");
    page("the redundant save operation.");
    glob_paralines = 8;
    page("  The user meta-command STAT can be used to see whether the");
    page("dictionary has been modified, and whether it is currently sorted.");
    page("  The meta-command SET is used to find out quickly which letters");
    page("can be used in a given position. For example, searching on the");
    page("pattern 'be???f' yields the results \"behalf, behoof, belief\";");
    page("therefore the meta-command 'SET be??_f' yields the three letters");
    page("\"elo\", and 'SET be_??f' yields \"hl\". All the normal wildcards");
    page("can be used in SET commands.");
    glob_paralines = 5;
    page("  All the normal wildcards can be used in REM commands, also;");
    page("the command 'REM foo*' will remove \"food\" and \"footstool\".");
    page("Wildcards cannot be used with ADD, for obvious reasons; you must");
    page("enter 'ADD draft' and 'ADD drafted' individually, for example.");
    page("However, 'ADD draft/s' will add both \"draft\" and \"drafts\".");
    glob_paralines = 4;
    page("  Type 'HELP' for a brief summary of commands and wildcards, or");
    page("'HELP VERBOSE' for this message again. Type 'HELP VERBOSE k' to");
    page("make this message pause after every k lines; for example, 'HELP");
    page("VERBOSE 20'.");
}


/*
   This is a very icky attempt at a "nice" pager, that will avoid
   creating orphaned or widowed lines, as best it can, on the fly.
   It's overkill for this program, but since we have in-program help
   text instead of a verbose help option at the command line, it seems
   like we ought to be helping the user as much as possible.
*/
void page(const char *s)
{
    static int lines_in_this_para;
    static int lines_dumped_this_page = 0;
    static int para_line;
    static int dump_count;

    if (s == NULL) {
        lines_dumped_this_page = 0;
        return;
    }

    if (glob_paralines > 0) {
        lines_in_this_para = glob_paralines;
        para_line = 0;
        glob_paralines = 0;
        if (lines_dumped_this_page + lines_in_this_para <= glob_pageheight)
          dump_count = lines_in_this_para;
        else {
            if (lines_dumped_this_page + 2 > glob_pageheight) {
                /* We may need an orphaned line. */
                if (lines_dumped_this_page > 2*glob_pageheight/3)
                  goto pause;
                /* Yep, make this an orphan. */
                dump_count = lines_in_this_para;
            }
            else if ((lines_dumped_this_page + lines_in_this_para) % glob_pageheight == 1) {
                /* We may need a widowed line. */
                if (lines_dumped_this_page > 2*glob_pageheight/3)
                  goto pause;
                /* Yep, make this a widow. */
                dump_count = lines_in_this_para;
            }
            else dump_count = lines_in_this_para;
        }
    }

    if (dump_count > 0) {
        --dump_count;
        goto print;
    }
    else goto pause;

  print:
    puts(s);
    ++lines_dumped_this_page;
    --glob_paralines;
    if (lines_dumped_this_page >= glob_pageheight) {
        puts("--more--");
        while (getchar() != '\n') continue;
        lines_dumped_this_page = 0;
        dump_count = glob_pageheight;
    }
    return;

  pause:
    puts("--more--");
    while (getchar() != '\n') continue;
    puts(s);
    lines_dumped_this_page = 1;
    --glob_paralines;
    dump_count = glob_pageheight-1;
    return;
}
