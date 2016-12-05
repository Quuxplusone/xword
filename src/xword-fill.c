
/*
   This program is copyright Arthur O'Dwyer, May 2009.
   It is free for all non-commercial use. Please keep it free.

   This program uses Donald Knuth's "dancing links" algorithm
   to fill a user-specified crossword grid with words from a
   user-specified dictionary.
*/

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dancing.h"
#include "xdictlib.h"

#define MAX_WORDLEN 15   /* fixes some array bounds at compile time */

#define steq(s,t) (!strcmp(s,t))
#define stneq(s,t) (!steq(s,t))
#define ch2idx(ch) (isalpha(ch)? tolower(ch)-'a': 'x'-'a')
#define idx2ch(idx) (idx+'a')


struct xword_info {
    int w, h;
    const char *grid;
    struct dance_matrix *mat;
    FILE *out;
};


static char *DictFilename = "xdict.save.txt";
static char *OutputFilename = NULL;
static FILE *DebugFile = NULL;
static int NumSolutions = -1; /* print all solutions by default */
static int RejectDuplicateWords = 1;
/* Pass "--naive" if you want to see the simple method in which
 * the matrix always has exactly 54*w*h columns.  In the default method, we
 * compress the matrix by getting rid of all the slices that correspond to
 * known cells (black squares or squares forced to contain a given letter).
 * This saves memory, but it doesn't really speed up Dancing Links any,
 * because we're removing only the columns that are easy for Dancing Links
 * to satisfy anyway. */
static int UseNaiveMethod = 0;
static int PrintEveryNthSolution = 1;

int load_grid(FILE *fp, char **grid, int *w, int *h);
 void strip_space(char *line);
void strip_dict(const char *grid, int w, int h, struct xdict *dict);

int xword_solve(const char *grid, int w, int h, struct xdict *dict,
    FILE *out);
 size_t dict_len(struct xdict *dict);
 int add_rows_for_word(const char *word, void *info);
  int entry_fits_across(const char *grid, int w, int h,
      int i, int j, const char *word, int wlen);
  int entry_fits_down(const char *grid, int w, int h,
      int i, int j, const char *word, int wlen);
   int matches(int a, int b);
  int add_row_across(struct xword_info *info,
      int i, int j, const char *word, int wlen);
  int add_row_down(struct xword_info *info,
      int i, int j, const char *word, int wlen);
 int add_row_black(struct dance_matrix *mat, int w, int h, int idx);
 int add_row_forced_across(struct dance_matrix *mat, int w, int h,
     int i, int j, const char *grid);
 int add_row_forced_down(struct dance_matrix *mat, int w, int h,
     int i, int j, const char *grid);

 int print_crossword_result(size_t n, struct data_object **sol, void *info);
  int grid_contains_duplicates(const char *grid, int w, int h);

int is_fixed_value(int ch);

void debug(const char *fmt, ...);
void do_help(int man);
void do_error(const char *fmt, ...);



int main(int argc, char **argv)
{
    int LiteralInputNames = 0;
    FILE *gridfp;
    FILE *outfp = stdout;
    char *grid;
    int gridw, gridh;
    struct xdict dict;
    int i;

    xdict_init(&dict);

    for (i=1; i < argc; ++i) {
        if (argv[i][0] != '-') break;
        if (argv[i][1] == '\0') break;

        if (steq(argv[i]+1, "-")) {
            LiteralInputNames = 1;
            ++i;
            break;
        } else if (steq(argv[i]+1, "-help") ||
                 steq(argv[i]+1, "h") || steq(argv[i]+1, "?")) {
            do_help(0);
        } else if (steq(argv[i]+1, "-man")) {
            do_help(1);
        } else if (steq(argv[i], "-o")) {
            if (i >= argc-1)
              do_error("Need output filename with -o");
            OutputFilename = argv[++i];
        } else if (steq(argv[i], "-d")) {
            if (i >= argc-1)
              do_error("Need dictionary filename with -d");
            DictFilename = argv[++i];
        } else if (steq(argv[i], "-n") || steq(argv[i], "-N")) {
            if (i >= argc-1)
              do_error("Need a number (of solutions) with -n");
            NumSolutions = atoi(argv[++i]);
            if (NumSolutions <= 0)
              do_error("Option -n expects a positive integer!");
        } else if (steq(argv[i], "--every")) {
            if (i >= argc-1)
              do_error("Need a number (of solutions) with --every");
            PrintEveryNthSolution = atoi(argv[++i]);
            if (PrintEveryNthSolution <= 0)
              do_error("Option --every expects a positive integer!");
        } else if (steq(argv[i], "--allow_duplicate_words")) {
            RejectDuplicateWords = 0;
        } else if (steq(argv[i], "--debug")) {
            DebugFile = stderr;
        } else if (steq(argv[i], "--naive")) {
            UseNaiveMethod = 1;
        } else {
            do_error("Unrecognized option(s) '%s'; -h for help", argv[i]);
        }
    }

    if (argc-i > 1) {
        do_error("You seem to have provided %d input files.\n"
                 "I can only read one at a time.", argc-i);
    }

    if (i < argc && (LiteralInputNames || stneq(argv[i], "-"))) {
        char *fname = argv[i];
        if ((gridfp = fopen(fname, "r")) == NULL)
          do_error("I couldn't open grid file '%s'!", fname);
    } else {
        gridfp = stdin;
    }


    switch (load_grid(gridfp, &grid, &gridw, &gridh)) {
        case 0: break;
        case -2: do_error("I couldn't parse the grid%s!", 
                     (gridfp==stdin)? "": " file");
        case -3: do_error("Out of memory loading grid!");
        default: do_error("Error loading grid file!");
    }

    if (gridfp != stdin)
      fclose(gridfp);

    if (RejectDuplicateWords && grid_contains_duplicates(grid, gridw, gridh)) {
        do_error("The input grid contains duplicate words!\n"
                 "Use option --allow_duplicate_words, or amend your input file.");
    }
    debug("Done checking for duplicate words in input grid.");
    
    if (xdict_load(&dict, DictFilename) < 0)
      do_error("Error loading dictionary file '%s'!", DictFilename);
    debug("Done loading dictionary file '%s'.", DictFilename);

    if (OutputFilename && stneq(OutputFilename, "-")) {
        outfp = fopen(OutputFilename, "w");
        if (outfp == NULL) {
            do_error("I couldn't open file '%s' for output!", 
                OutputFilename);
        }
    }
    else outfp = stdout;

    strip_dict(grid, gridw, gridh, &dict);

    xword_solve(grid, gridw, gridh, &dict, outfp);

    xdict_free(&dict);

    return 0;
}


int load_grid(FILE *fp, char **ggrid, int *gw, int *gh)
{
    size_t i, j;
    size_t w, h;
    char *grid = NULL;
    size_t gridcap = 0;
    char buf[100];

    /* Find something like a grid. */
    while (fgets(buf, sizeof buf, fp) != NULL)
    {
        strip_space(buf);
        if (buf[0] != '\0') break;
    }
    if (feof(fp)) return -2;

    w = strlen(buf);
    h = 0;
    while (strlen(buf) == w) {
        if (w*h >= gridcap) {
            gridcap += 2*w;
            grid = realloc(grid, gridcap);
            if (grid == NULL) return -3;
        }
        for (i=0; i < w; ++i) {
            if (strchr("`.?", buf[i]) != NULL)
              grid[w*h+i] = '.';
            else grid[w*h+i] = tolower(buf[i]);
        }
        h += 1;
        if (fgets(buf, sizeof buf, fp) == NULL)
          break;
        strip_space(buf);
    }

    /* Print the grid, so the user can see whether we got it right. */
    printf("Grid (%ldx%ld):\n", (long)w, (long)h);
    for (j=0; j < h; ++j) {
        for (i=0; i < w; ++i)
          printf("%c", grid[j*w+i]);
        printf("\n");
    }

    *ggrid = grid;
    *gw = w;
    *gh = h;
    return 0;
}


/* Return the total number of unknown cells in this grid. */
static int NUMBER_OF_SLICES(struct xword_info *info)
{
    int n = info->w * info->h;
    const char *grid = info->grid;
    int i;
    int unknown_cells = 0;
    if (UseNaiveMethod)
     return n;    
    for (i=0; i < n; ++i) {
        if (is_fixed_value(grid[i])) continue;
        ++unknown_cells;
    }
    return unknown_cells;
}

/* Find the slice'th unknown cell in this grid. */
static int SLICE_TO_CELL(int slice, struct xword_info *info)
{
    int n = info->w * info->h;
    const char *grid = info->grid;
    int i;
    int unknown_cells = 0;
    assert(slice < n);
    if (UseNaiveMethod)
     return slice;
    for (i=0; i < n; ++i) {
        if (is_fixed_value(grid[i])) continue;
        if (unknown_cells == slice)
          return i;
        ++unknown_cells;
    }
    assert(0);
    return 0;
}

/* This cell is the which'th unknown cell in this grid? */
static int CELL_TO_SLICE(int cell, struct xword_info *info)
{
    int n = info->w * info->h;
    const char *grid = info->grid;
    int i;
    int unknown_cells = 0;
    if (UseNaiveMethod)
     return cell;
    assert(cell < n);
    assert(!is_fixed_value(grid[cell]));
    for (i=0; i < n; ++i) {
        if (i == cell)
          return unknown_cells;
        if (is_fixed_value(grid[i])) continue;
        ++unknown_cells;
    }
    assert(0);
    return 0;
}


/* (This comment describes the naive method.)
 * Consider the following trivial example: The initial grid is
 *     .AS     012
 *     .R.     345
 *     ETA     678
 * For reference, "cell 2" is the cell containing "S" above,
 * and so on. The dictionary consists of the seven words
 * { art, eta, has, hie, hit, ire, sea }.
 *
 * We turn this grid-filling problem into a matrix with eight rows
 * and (26+2 * w*h) = 252 columns. For demonstration purposes,
 * however, we don't need to show all 252 columns.
 *
 * Each row of the matrix corresponds to a possible word placement.
 * For example, we have one row that corresponds to placing ART at
 * 4-Across, and another row that corresponds to placing IRE there.
 * We also have a row for placing IRE at 1-Down, and so on. We have
 * no rows for placing HIT, since it doesn't fit anywhere.
 * The pattern of 1s and 0s in rows corresponding to "Across" words
 * is done differently from the rows corresponding to "Down" words.
 *
 * Our matrix is really made up of "column pairs", not just columns.
 * The values in a "column pair" may be (0 1), (1 0), or (0 0).
 * Notice that in the exact-cover solution, each column pair will
 * contain exactly one (0 1), exactly one (1 0), and arbitrarily
 * many (0 0).
 *
 * The matrix column-pairs are also organized into slices, where
 * each slice relates to a particular grid cell. A matrix row will
 * contain (0 0) everywhere except in slices related to its grid
 * position. There will be a total of w*h slices.
 *
 * The slice for a grid cell (i,j) contains 27 column-pairs. Twenty-six
 * of them are labeled "A" through "Z", and the last one is labeled
 * "Across or Down".  In our example, the row "IRE at 4-Across"
 * will have (1 0) in the column-pair labeled "I" in the slice for
 * cell 3, because it wants to put "I" in cell 3. It will have (0 1)
 * in every other column-pair "A" through "Z" in that slice.
 * It will have (1 0) in the "Across or Down" column-pair in slice 3.
 * Likewise, it will have (1 0) in the column-pairs for "R" and
 * "Across or Down" in cell 4, and (0 1) in the other column-pairs
 * for cell 4; and likewise again for cell 5. It will have (0 0) in
 * the rest of the column-pairs.
 *
 * The "Down" rows are constructed the same way, but where an "Across"
 * row would have (1 0) they have (0 1), and vice versa. This means
 * that the column-pair for "I across in cell 3" interlocks pleasingly
 * with the column-pair for "I down in cell 3", which is exactly what
 * we want to happen.
 *
 * The interesting columns of the matrix for the above problem
 * look like this:
 *
 *                    cell 0.....  cell 3........  cell 5.....  cell 7
 *                    H  I  X  ad  A  I  R  X  ad  E  T  X  ad  T  ad
 *     1-Across HAS   1- -1 -1 1-  -- -- -- -- --  -- -- -- --  -- --
 *     4-Across ART   -- -- -- --  1- -1 -1 -1 1-  -1 1- -1 1-  -- --
 *     4-Across IRE   -- -- -- --  -1 1- -1 -1 1-  1- -1 -1 1-  -- --
 *     5-Across ETA   -- -- -- --  -- -- -- -- --  -- -- -- --  1- 1-
 *     1-Down HIE     -1 1- 1- -1  1- -1 1- 1- -1  -- -- -- --  -- --
 *     1-Down IRE     1- -1 1- -1  1- 1- -1 1- -1  -- -- -- --  -- --
 *     2-Down ART     -- -- -- --  -- -- -- -- --  -- -- -- --  -1 -1
 *     3-Down SEA     -- -- -- --  -- -- -- -- --  -1 1- 1- -1  -- --
 *
 * I've shown the rows for 5-Across and 2-Down, for completeness,
 * even though they contain non-(0 0) values only in uninteresting
 * places. The point is that the matrix really does have only 8 rows,
 * even though it has hundreds of columns that I didn't bother to show.
 * And the solution to the exact cover problem is this set of rows:
 *
 *     1-Across HAS   1- -1 -1 1-  -- -- -- -- --  -- -- -- --  -- --
 *     4-Across IRE   -- -- -- --  -1 1- -1 -1 1-  1- -1 -1 1-  -- --
 *     5-Across ETA   -- -- -- --  -- -- -- -- --  -- -- -- --  1- 1-
 *     1-Down HIE     -1 1- 1- -1  1- -1 1- 1- -1  -- -- -- --  -- --
 *     2-Down ART     -- -- -- --  -- -- -- -- --  -- -- -- --  -1 -1
 *     3-Down SEA     -- -- -- --  -- -- -- -- --  -1 1- 1- -1  -- --
 *
 * There are some extra complications, of course:
 * (1) The grid contains black squares. These grid cells won't ever
 * be affected by any "Across" or "Down" words, so we'll just add
 * one extra "black-square" row to the matrix that contains (1 1)
 * in every column-pair of every slice that corresponds to a black
 * square. This row will always be picked as part of the solution set,
 * since it's the only way to get 1s into those columns.
 *
 * (2) If the given grid is only one corner of a larger puzzle, it may
 * contain partial words that aren't in the dictionary. This is okay;
 * we'll add a row for each "forced placement" of a made-up word.
 * If the above example had contained "T" in cell 6 instead of "E",
 * we'd have added a row "5-Across TTA" anyway.
 */
int xword_solve(const char *grid, int w, int h, struct xdict *dict, FILE *out)
{
    struct dance_matrix mat;
    int ns;
    int i,j;
    /* Set up the info for our callback grid-printing function. */
    struct xword_info info = { w, h, grid, &mat, out };
    int cols = 27*2*NUMBER_OF_SLICES(&info);
    int rc = dance_init(&mat, 0, cols, NULL);
    if (rc != 0)
      return rc;

    xdict_find(dict, "*", add_rows_for_word, &info);

    if (UseNaiveMethod) {
        /* Complication 1: Add a row for each black cell. */
        debug("Looking for black squares...");
        for (i=0; i < w*h; ++i) {
            if (grid[i] == '#') {
                add_row_black(&mat, w, h, i);
                debug("Added row black(%d,%d)", i/w, i%w);
            }
        }
    }

    if (UseNaiveMethod) {
        /* Complication 2: Add a row for each forced placement. */
        debug("Looking for forced placements Across...");
        for (j=0; j < h; ++j) {
            /* Look for forced-placement Across words. */
            int word_starts_here = 0;
            for (i=0; i <= w; ++i) {
                if ((i == w || grid[j*w+i] == '#') && word_starts_here < i) {
                    assert(i - word_starts_here <= MAX_WORDLEN);
                    add_row_forced_across(&mat, w, h, word_starts_here, j, grid);
                    debug("Added row forced_across(%d,%d)", word_starts_here, j);
                    word_starts_here = i+1;
                } else if (i == w) {
                    break;
                } else if (!isalpha(grid[j*w+i])) {
                    /* Skip this entry; it's not being forced. */
                    while (i < w && grid[j*w+i] != '#')
                      ++i;
                    word_starts_here = i+1;
                } else {
                    /* do nothing special */
                }
            }
        }
        debug("Looking for forced placements Down...");
        for (i=0; i < w; ++i) {
            /* Look for forced-placement Down words. */
            int word_starts_here = 0;
            for (j=0; j <= h; ++j) {
                if ((j == h || grid[j*w+i] == '#') && word_starts_here < j) {
                    assert(j - word_starts_here <= MAX_WORDLEN);
                    add_row_forced_down(&mat, w, h, i, word_starts_here, grid);
                    debug("Added row forced_down(%d,%d)", i, word_starts_here);
                    word_starts_here = j+1;
                } else if (j == h) {
                    break;
                } else if (!isalpha(grid[j*w+i])) {
                    /* Skip this entry; it's not being forced. */
                    while (j < h && grid[j*w+i] != '#')
                      ++j;
                    word_starts_here = j+1;
                } else {
                    /* do nothing special */
                }
            }
        }
    }

    printf("The completed matrix has %ld columns and %ld rows.\n", 
        (long)mat.ncolumns, (long)mat.nrows);
    printf("Solving...\n");

    ns = dance_solve(&mat, print_crossword_result, &info);
    if (ns == -99) {
        /* We generated NumSolutions grids and then bailed out. */
    } else if (ns < 0) {
        /* There was some kind of internal error. */
        debug("dance_solve() returned %d", ns);
        do_error("There was an error in dance_solve(). Probably out of memory.");
    } else {
        printf("There w%s %d solution%s found.\n", (ns==1)? "as": "ere", 
            ns, (ns==1)? "": "s");
    }

    dance_free(&mat);
    return 0;
}


/* Callback invoked from 'xdict_find' on every word in the dictionary.
 * This routine looks for all the possible placements of this word,
 * and adds a row to the matrix for each one it finds.
 */
int add_rows_for_word(const char *word, void *vinfo)
{
    struct xword_info *info = vinfo;
    int i, j;
    int wlen = strlen(word);
    int w = info->w, h = info->h;
    const char *grid = info->grid;
    debug("add_rows_for_word(%s)", word);

    for (j=0; j < h; ++j) {
        for (i=0; i < w; ++i) {
            if (entry_fits_across(grid, w, h, i, j, word, wlen) == 1) {
                add_row_across(info, i, j, word, wlen);
                debug("Added row across(%d,%d, %s)", i,j, word);
            }
            if (entry_fits_down(grid, w, h, i, j, word, wlen) == 1) {
                add_row_down(info, i, j, word, wlen);
                debug("Added row down(%d,%d, %s)", i,j, word);
            }
        }
    }
    return 0;
}


int entry_fits_across(const char *grid, int w, int h,
    int i, int j, const char *word, int wlen) 
{
    int exact_match = 1;
    int k;
    if (i+wlen > w) return 0;
    if (i > 0 && grid[j*w+(i-1)] != '#') return 0;
    if (i+wlen < w && grid[j*w+(i+wlen)] != '#') return 0;
    for (k=0; k < wlen; ++k) {
        int rc = matches(grid[j*w+(i+k)], word[k]);
        if (rc == 0) return 0;
        else if (rc == 1) exact_match = 0;
    }
    return exact_match? 2: 1;
}

int entry_fits_down(const char *grid, int w, int h,
    int i, int j, const char *word, int wlen)
{
    int exact_match = 1;
    int k;
    if (j+wlen > h) return 0;
    if (j > 0 && grid[(j-1)*w+i] != '#') return 0;
    if (j+wlen < h && grid[(j+wlen)*w+i] != '#') return 0;
    for (k=0; k < wlen; ++k) {
        int rc = matches(grid[(j+k)*w+i], word[k]);
        if (rc == 0) return 0;
        else if (rc == 1) exact_match = 0;
    }
    return exact_match? 2: 1;
}

int matches(int a, int b)
{
    if (a == '#' || b == '#') return 0;
    if (a == '.' || b == '.') return 1;
    if (strchr("aeiouy", a) && b == '0') return 1;
    if (strchr("aeiouy", b) && a == '0') return 1;
    if (strchr("bcdfghjklmnpqrstvwxyz", a) && b == '1') return 1;
    if (strchr("bcdfghjklmnpqrstvwxyz", b) && a == '1') return 1;
    return (tolower(a) == tolower(b))? 2: 0;
}

int is_fixed_value(int ch)
{
    if (ch == '#') return 1;
    if (isalpha(ch)) return 1;
    assert(ch == '.' || ch == '0' || ch == '1');
    return 0;
}


int add_row_across(struct xword_info *info,
    int i, int j, const char *word, int wlen)
{
    struct dance_matrix *mat = info->mat;
    int w = info->w;
    size_t constraint[MAX_WORDLEN*27];
    int idx = 0;
    int k, m;
    for (k=0; k < wlen; ++k) {
        int cell = j*w+(i+k);
        /* When using the non-naive method, we only add slices for cells
         * whose values are actually unknown. */
        if (UseNaiveMethod || !is_fixed_value(info->grid[cell])) {
            int slice = 27*2*CELL_TO_SLICE(cell, info);
            int relevant_index = ch2idx(word[k]);
        
            /* Put (1 0) in one column-pair; put (0 1) in the other 25. */
            for (m=0; m < 26; ++m) {
                constraint[idx++] = slice + 2*m + (relevant_index != m);
            }
            /* Put (1 0) in this slice's "Across or Down" column-pair. */
            constraint[idx++] = slice + 2*26 + 0;
        }
    }
    if (UseNaiveMethod)
      assert(idx == wlen*27);
    return dance_addrow(mat, idx, constraint);
}

int add_row_down(struct xword_info *info,
    int i, int j, const char *word, int wlen)
{
    struct dance_matrix *mat = info->mat;
    int w = info->w;
    size_t constraint[MAX_WORDLEN*27];
    int idx = 0;
    int k, m;
    for (k=0; k < wlen; ++k) {
        int cell = (j+k)*w+i;
        /* When using the non-naive method, we only add slices for cells
         * whose values are actually unknown. */
        if (UseNaiveMethod || !is_fixed_value(info->grid[cell])) {
            int slice = 27*2*CELL_TO_SLICE(cell, info);
            int relevant_index = ch2idx(word[k]);
        
            /* Put (0 1) in one column-pair; put (1 0) in the other 25. */
            for (m=0; m < 26; ++m) {
                constraint[idx++] = slice + 2*m + (relevant_index == m);
            }
            /* Put (0 1) in this slice's "Across or Down" column-pair. */
            constraint[idx++] = slice + 2*26 + 1;
        }
    }
    if (UseNaiveMethod)
      assert(idx == wlen*27);
    return dance_addrow(mat, idx, constraint);
}

int add_row_black(struct dance_matrix *mat, int w, int h, int cell)
{
    size_t constraint[27*2];
    int idx = 0;
    int m;
    assert(UseNaiveMethod);
    for (m=0; m < 27; ++m) {
        int slice = 27*2*cell;
        constraint[idx++] = slice + 2*m + 0;
        constraint[idx++] = slice + 2*m + 1;
    }
    assert(idx == 27*2);
    return dance_addrow(mat, idx, constraint);
}

int add_row_forced_across(struct dance_matrix *mat, int w, int h,
    int i, int j, const char *grid)
{
    size_t constraint[MAX_WORDLEN*27];
    int idx = 0;
    int k, m;
    assert(UseNaiveMethod);
    for (k=0; i+k < w; ++k) {
        int cell = j*w+(i+k);
        int slice = 27*2*cell;
        int relevant_index;
        if (grid[cell] == '#')
          break;
        assert(isalpha(grid[cell]));
        relevant_index = ch2idx(grid[cell]);
        
        /* Put (1 0) in one column-pair; put (0 1) in the other 25. */
        for (m=0; m < 26; ++m) {
            constraint[idx++] = slice + 2*m + (relevant_index != m);
        }
        /* Put (1 0) in this slice's "Across or Down" column-pair. */
        constraint[idx++] = slice + 2*26 + 0;
    }
    assert(k < MAX_WORDLEN);
    assert(idx == k*27);
    return dance_addrow(mat, idx, constraint);
}

int add_row_forced_down(struct dance_matrix *mat, int w, int h,
    int i, int j, const char *grid)
{
    size_t constraint[MAX_WORDLEN*27];
    int idx = 0;
    int k, m;
    assert(UseNaiveMethod);
    for (k=0; j+k < h; ++k) {
        int cell = (j+k)*w+i;
        int slice = 27*2*cell;
        int relevant_index;
        if (grid[cell] == '#')
          break;
        assert(isalpha(grid[cell]));
        relevant_index = ch2idx(grid[cell]);
        
        /* Put (0 1) in one column-pair; put (1 0) in the other 25. */
        for (m=0; m < 26; ++m) {
            constraint[idx++] = slice + 2*m + (relevant_index == m);
        }
        /* Put (0 1) in this slice's "Across or Down" column-pair. */
        constraint[idx++] = slice + 2*26 + 1;
    }
    assert(k < MAX_WORDLEN);
    assert(idx == k*27);
    return dance_addrow(mat, idx, constraint);
}

int print_crossword_result(size_t n, struct data_object **sol, void *vinfo)
{
    struct xword_info *info = vinfo;
    int w = info->w;
    int h = info->h;
    char *grid;
    int i, j;
    size_t k;
    static int printed_so_far = 0;
    static int skipped_so_far = 0;

    if (PrintEveryNthSolution != 1) {
        ++skipped_so_far;
        if (skipped_so_far < PrintEveryNthSolution) {
            return 0;
        } else {
            skipped_so_far = 0;
        }
    }

    assert(NumSolutions == -1 || printed_so_far < NumSolutions);

    grid = malloc(w*h * sizeof *grid);
    if (grid == NULL) return -3;
    memcpy(grid, info->grid, w*h);

    for (k=0; k < n; ++k) {
        struct data_object *o = sol[k];
        int this_is_an_across_word = 0;
        int this_is_a_down_word = 0;
        /* o is an arbitrary 1 entry in the kth row of the solution.
         * Scan to the right in the circular linked list that is o->right,
         * looking for a 1 in an "Across or Down" column-pair; that will
         * tell us whether this row represents an Across word, a Down word,
         * or a set of black squares. We really only care about the
         * Across words, since the letters Down are by definition the
         * same as the letters Across. */
        do {
            int colx = atoi(o->column->name);
            if (colx % 54 == 52) {
                this_is_an_across_word = 1;
            } else if (colx % 54 == 53) {
                this_is_a_down_word = 1;
            }
            o = o->right;
        } while (o != sol[k]);
        assert(this_is_an_across_word || this_is_a_down_word);
        if (this_is_an_across_word && this_is_a_down_word) {
            /* Actually, this is a set of black squares. */
            assert(UseNaiveMethod);
            continue;
        } else if (this_is_a_down_word) {
            /* Indeed it is. */
            continue;
        }
        
        /* This is an Across word. Extract its letters. */
        o = sol[k];
        do {
            int colx = atoi(o->column->name);
            int cell = SLICE_TO_CELL(colx / 54, info);
            assert(0 <= cell && cell < w*h);
            if (colx % 2 == 0) {
                /* This is a column-pair containing (1 0). */
                int letter_idx = (colx % 54) / 2;
                if (letter_idx == 26) {
                    /* This is just the "Across or Down" column-pair again. */
                } else {
                    assert(0 <= letter_idx && letter_idx < 26);
                    grid[cell] = idx2ch(letter_idx);
                }
            }
            o = o->right;
        } while (o != sol[k]);
    }

    if (RejectDuplicateWords) {
        int rc = grid_contains_duplicates(grid, w, h);
        if (rc < 0) {
            debug("grid_contains_duplicates() returned %d", rc);
            free(grid);
            /* Return an error code to bail out immediately. */
            return -1;
        } else if (rc == 1) {
            debug("Grid %d contains duplicate words", printed_so_far);
            free(grid);
            /* This one doesn't count toward our total number of grids,
             * so return 0 as the value to accumulate instead of 1. */
            return 0;
        }
        assert(rc == 0);
    }

    for (j=0; j < h; ++j) {
        for (i=0; i < w; ++i)
          fprintf(info->out, "%c", grid[j*w+i]);
        fprintf(info->out, "\n");
    }
    fprintf(info->out, "\n");
    printed_so_far += 1;

    free(grid);
    /* Return -99 to cause dance_solve() to bail out if we've hit our
     * maximum number of solutions. Otherwise, return 1, which will
     * be accumulated into the return value of dance_solve(). */
    return (printed_so_far == NumSolutions ? -99 : 1);
}


/*
   This routine strips whitespace from the beginning and end of
   the given line.
*/
void strip_space(char *buf)
{
    int i;
    int n = strlen(buf);

    for (i=0; isspace(buf[i]); ++i)
      continue;
    if (i > 0) {
        n -= i;
        memmove(buf, buf+i, n+1);
    }

    for (i=n; (i > 0) && isspace(buf[i-1]); --i)
      continue;
    buf[i] = '\0';
    return;
}


/* Compare two strings lexicographically. */
int ptr_str_cmp(const void *vp, const void *vq)
{
    char * const *a = vp;
    char * const *b = vq;
    int k = strcmp(*a, *b);
    return (k < 0)? -1: (k > 0);
}



/*
    This routine strips out all the "useless" words from the dictionary 
    --- words that can't possibly fit anywhere in the grid. This speeds
    up the solution of the matrix by a large factor, for grids in which
    we only care about one or two problem corners.

    This routine also strips out any words that appear in the grid 
    already, so that we don't duplicate any words.
*/
void strip_dict(const char *grid, int w, int h, struct xdict *dict)
{
    int k;
    size_t widx;
    int i, j;
    int removed_count = 0;

    for (k=0; k < XDICT_MAXLENGTH; ++k) {
        for (widx=0; widx < dict->len[k]; ++widx) {
            char *word = dict->words[k][widx].word;
            int fits_in_grid = 0;

            /* Does the current |word| fit in the grid? */
            for (j=0; j < h; ++j) {
                for (i=0; i < w-k+1; ++i) {
                    switch (entry_fits_across(grid, w, h, i, j, word, k)) {
                        case 2: if (RejectDuplicateWords) goto remove_it;
                        case 1: fits_in_grid = 1;
                          if (!RejectDuplicateWords) goto next;
                    }
                }
            }
            for (j=0; j < h-k+1; ++j) {
                for (i=0; i < w; ++i) {
                    switch (entry_fits_down(grid, w, h, i, j, word, k)) {
                        case 2: if (RejectDuplicateWords) goto remove_it;
                        case 1: fits_in_grid = 1;
                          if (!RejectDuplicateWords) goto next;
                    }
                }
            }

            /* It doesn't fit, or is duplicated. Remove it. */
            if (!fits_in_grid) {
              remove_it:
                free(word);
                dict->words[k][widx] = dict->words[k][--dict->len[k]];
                removed_count += 1;
            }
          next:
            /* Go get the next word in the dictionary. */
            ;
        }
    }

    debug("Preemptively removed %d already-used or useless words\n"
          " from the dictionary, leaving %d.", removed_count, dict_len(dict));
    return;
}


/* Returns 1 if the grid contains duplicates; 0 if it doesn't;
 * or -3 if we run out of memory. */
int grid_contains_duplicates(const char *grid, int w, int h)
{
    char **dict = malloc(2*w*h * sizeof *dict);
    int len = 0;
    int rc = 0;
    int i, j, k, end;

    if (dict == NULL) return -3;

    /* Insert the "Across" entries. */
    for (j=0; j < h; ++j) {
        for (i=0; i < w; i = end) {
            int invalid = 0;
            end = i+1;
            if (grid[j*w+i]=='#') continue;
            for (end=i; end < w; ++end) {
                if (grid[j*w+end]=='#') break;
                if (strchr(".01", grid[j*w+end])) invalid = 1;
            }
            if (invalid) continue;
            if ((dict[len] = malloc(end-i+1)) == NULL) {
                rc = -3;
                break;
            }
            for (k=0; k < end-i; ++k)
              dict[len][k] = tolower(grid[j*w+(i+k)]);
            dict[len][k] = '\0';
            len += 1;
        }
    }

    /* Insert the "Down" entries. */
    for (i=0; i < w; ++i) {
        for (j=0; j < h; j = end) {
            int invalid = 0;
            end = j+1;
            if (grid[j*w+i]=='#') continue;
            for (end=j; end < h; ++end) {
                if (grid[end*w+i]=='#') break;
                if (strchr(".01", grid[end*w+i])) invalid = 1;
            }
            if (invalid) continue;
            if ((dict[len] = malloc(end-j+1)) == NULL) {
                rc = -3;
                break;
            }
            for (k=0; k < end-j; ++k)
              dict[len][k] = tolower(grid[(j+k)*w+i]);
            dict[len][k] = '\0';
            len += 1;
        }
    }

    qsort(dict, len, sizeof *dict, ptr_str_cmp);
    for (k=0; k < len-1; ++k) {
        if (strcmp(dict[k], dict[k+1]) == 0) {
            debug("The duplicate word is '%s'.", dict[k]);
            rc = 1;
            break;
        }
    }
    for (k=0; k < len; ++k)
      free(dict[k]);
    free(dict);
    return rc;
}


size_t dict_len(struct xdict *dict)
{
    size_t i;
    size_t count = 0;
    for (i=0; i < XDICT_MAXLENGTH; ++i)
      count += dict->len[i];
    return count;
}


void debug(const char *fmt, ...)
{
    if (DebugFile == NULL)
      return;
    else {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(DebugFile, fmt, ap);
        fputc('\n', DebugFile);
        va_end(ap);
    }
}


void do_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
    exit(EXIT_FAILURE);
}


void do_help(int man)
{
    if (man)
      goto man;
    puts("xword-fill [-?h] [-options] gridfile");
    puts("Fills a crossword grid by constraint satisfaction.");
    puts("  --allow_duplicate_words: allow duplicate words in output grid");
    puts("  -n int: limit output to first 'n' valid grids");
    puts("  -d filename: load dictionary from specified file");
    puts("  -o filename: send output to specified file");
    puts("  --debug: dump debugging output to stderr");
    puts("  --help: show this message");
    puts("  --man: show complete help text");
    exit(0);
  man:
    puts("xword-fill: Crossword filling tool.\n");
    puts(" This program attempts to fill in a crossword grid using");
    puts("   words from a dictionary file. The input grid must be in");
    puts("   the standard form output by 'xword-ent' and 'xword-manip',");
    puts("   using the hash mark ('#') to stand for black squares and");
    puts("   the dot or backtick ('.', '`') for empty squares. The grid");
    puts("   may also contain letters, which behave normally; the");
    puts("   numerals 0 and 1, which stand for \"any vowel\" and \"any");
    puts("   consonant,\" respectively. Any other characters are treated"); 
    puts("   as the letter X when it comes to grid-filling.");
    puts(" The program transforms the input grid and dictionary into");
    puts("   a very large matrix of ones and zeros, and then looks for");
    puts("   an \"exact cover\" of this matrix: a set of rows such that");
    puts("   the number 1 appears exactly once per column in that set");
    puts("   of rows. It uses the \"dancing links\" algorithm, due to");
    puts("   D.E. Knuth, to find this cover. Once a cover is found, the");
    puts("   program translates those rows back into a solution to the");
    puts("   crossword puzzle and prints out the solved grid.");
    puts(" Finding an exact cover can take a long time if the matrix is");
    puts("   large. If you find the program too slow, try giving it");
    puts("   only one corner to fill at a time.");
    puts(" Also remember that if your grid has two independent open");
    puts("   corners, with N and M distinct solutions respectively,");
    puts("   then passing the two-corner problem to 'xword-fill' will");
    puts("   yield NxM distinct solutions, whereas breaking it down into");
    puts("   two one-corner problems will yield only N+M.");
    puts(" When the exact-cover solver produces a solution grid, it may");
    puts("   contain duplicate entries, which of course is unacceptable");
    puts("   in a crossword grid. The program will silently ignore these");
    puts("   bad solutions (and they won't count toward the -n total),");
    puts("   unless you pass --allow_duplicate_words.");
    exit(0);
}
