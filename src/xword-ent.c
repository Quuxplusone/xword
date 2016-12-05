
/*
   |Xword-ent|, a public-domain program by Arthur O'Dwyer, December 2004.

     This program reads a grid of letters and hashmarks (|#|) from 
   a text file, and then outputs a list of all the words in the 
   crossword, in normal crossword order; first the horizontal clues and 
   then the vertical ones. Characters other than |#| and newlines are 
   treated as letters for our purposes.

     There are several command-line options to this program that work
   in interrelated ways.  The default is to give only the entries,
   with horizontal and vertical in separate lists.  The
   |-F| "frequencies" option alone will give no entries.  The |-H| and
   |-V| entries alone will give only horizontal and only vertical
   entries, respectively; but combined, they can act to override the
   default behavior of the |-F| option.  The |-T| "together" option
   only has an effect when both horizontal and vertical entries are
   being displayed; it shows them together in one combined list.

     The |-i| option uses clue formatting designed to work well with
   automated typesetting systems (in particular, the page templates I
   use with Adobe InDesign).  It doesn't right-justify the clue numbers,
   and uses one tab instead of two spaces between the number and the
   start of the clue.

     The |-S| and |-G| options work independently of the aforementioned
   entry-related options.  |-S| shows the grid solution (with blank
   spaces for black squares), and |-G| shows the unsolved grid (with
   backquotes and hash marks only).  The |-N| option adds placeholders
   for clue numbers in the grid produced by |-G|, and turns |-G| on
   by default; the character '0' is used for that placeholder.
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define steq(x,y) (!strcmp(x,y))
#define stneq(x,y) (!steq(x,y))
#define NELEM(a) ((int)(sizeof a / sizeof *a))

/* Grids can be no bigger than |MAXGRID| squares on a side. */
#define MAXGRID 45

static char *Argv0;

static int ShowHorizontal = 0;
static int ShowVertical = 0;
static int ShowTogether = 0;
static int AdobeClues = 0;
static int ShowSolution = 0;
static int ShowGrid = 0;
static int ShowNumbers = 0;
static int ShowFreqs = 0;
static char *OutputFilename = NULL;

int process(FILE *in, FILE *out);

void do_error(const char *fmat, ...);
void do_help(int);


int main(int argc, char **argv)
{
    int LiteralInputNames = 0;
    int i, j;

    Argv0 = argv[0];

    for (i=1; i < argc; ++i)
    {
        if (argv[i][0] != '-') break;
        if (argv[i][1] == '\0') break;

        if (steq(argv[i]+1, "-")) {
            LiteralInputNames = 1;
            ++i;
            break;
        }
        else if (steq(argv[i]+1, "-help") ||
                 steq(argv[i]+1, "h") || steq(argv[i]+1, "?"))
          do_help(0);
        else if (steq(argv[i]+1, "-man")) do_help(1);
        else if (steq(argv[i], "-o") || steq(argv[i], "-O")) {
            if (i >= argc-1) {
                do_error("Need output filename with -o");
            }
            OutputFilename = argv[++i];
        }
        else {
            for (j=1; argv[i][j]; ++j) {
                switch (argv[i][j]) {
                case 'H': case 'h': ShowHorizontal = 1; break;
                case 'V': case 'v': ShowVertical = 1; break;
                case 'T': case 't': ShowTogether = 1; break;
                case 'I': case 'i': AdobeClues = 1; break;
                case 'S': case 's': ShowSolution = 1; break;
                case 'G': case 'g': ShowGrid = 1; break;
                case 'N': case 'n': ShowNumbers = 1; break;
                case 'F': case 'f': ShowFreqs = 1; break;
                default:
                    do_error("Unrecognized option(s) %s; -h for help",
                        argv[i]);
                }
            }
        }
    }

    /*
       Resolve the dependencies between command-line options which
       were described in the prologue comment block.
    */
    if (ShowNumbers) ShowGrid = 1;
    if (ShowTogether) {
        if (ShowHorizontal != ShowVertical)
          ShowTogether = 0;
        else ShowHorizontal = ShowVertical = 1;
    }
    if (!ShowHorizontal && !ShowVertical && !ShowFreqs)
      ShowHorizontal = ShowVertical = 1;

    if (i >= argc)
      do_help(0);

    for (; i < argc; ++i)
    {
        FILE *infp;
        FILE *outfp;

        if (!LiteralInputNames && steq(argv[i], "--")) {
            LiteralInputNames = 1;
            continue;
        }
        else {
            infp = (!LiteralInputNames && steq(argv[i], "-"))?
                stdin:
                fopen(argv[i], "r");
        }

        outfp = OutputFilename? fopen(OutputFilename, "w"): stdout;

        if (infp == NULL)
          do_error("Error opening file '%s' for input", argv[i]);
        if (outfp == NULL)
          do_error("Error opening file '%s' for output", OutputFilename);

        process(infp, outfp);

        if (infp != stdin)
          fclose(infp);
        if (outfp != stdout)
          fclose(outfp);
    }

    return 0;
}


int process(FILE *in, FILE *out)
{
    char grid[MAXGRID][MAXGRID];
    int xmax, ymax;
    int x, y, k;
    int (*clues)[3], clue_idx, clue_max;
    int clue_width;
    enum {HORIZ=1, VERT=2};

    /* Prevent runaway words on bad input */
    for (y=0; y < MAXGRID; ++y)
      for (x=0; x < MAXGRID; ++x)
        grid[y][x] = '#';

    /*
       Read in the letter grid. The grid is filled in row-major order,
       so that |grid[0]| contains the first row of the file, |grid[1]|
       contains the second row, and so on.  A blank line is considered
       to end the grid.
    */
    x = y = xmax = ymax = 0;
    while ((k=getc(in)) != EOF)
    {
        if (k == '\n' || x >= MAXGRID) {
            if (k == '\n') {
                /*
                   A completely blank line means either that we have
                   not started reading input yet (if $y=0$), or else
                   that the user is done with input and we ought to
                   break out of this input loop.
                */
                for (k=0; k < x; ++k)
                  if (!isspace(grid[y][k])) break;
                if (k == x) {
                    if (y==0) { x=0; continue; }
                    else { break; }
                }
            }
            ++y; x = 0;
            if (y > ymax) ymax = y;
        }
        else {
            if (y >= MAXGRID)
              do_error("Max grid size is %dx%d!", MAXGRID, MAXGRID);
            grid[y][x++] = k;
            if (x > xmax) xmax = x;
        }
    }

    /*
       If the file doesn't end with a newline, we must explicitly
       handle this last line of the grid.
    */
    if (x > 0) ymax = y+1;

    /*
       Now we tag grid squares with clue numbers, just as in a normal
       crossword. A square gets a unique clue number if and only if it
       is preceded horizontally or vertically by a hashmark (black square)
       or by the edge of the grid.  Clue number coordinates are stored in
       the array |cluecoord|, which we conservatively assume to have a
       maximum of $x_max*y_max$ entries.
    */
    clues = malloc(xmax * ymax * sizeof *clues);
    clue_idx = 0;
    for (y=0; y < ymax; ++y) {
        for (x=0; x < xmax; ++x) {
            if (grid[y][x] != '#' && (x==0 || y==0 ||
                    grid[y-1][x]=='#' || grid[y][x-1]=='#')
               ) {
                clues[clue_idx][0] = x;
                clues[clue_idx][1] = y;
                clues[clue_idx][2] = 0;
                if (y==0 || grid[y-1][x]=='#') clues[clue_idx][2] |= VERT;
                if (x==0 || grid[y][x-1]=='#') clues[clue_idx][2] |= HORIZ;
                ++clue_idx;
            }
        }
    }
    clue_max = clue_idx;
    clue_width = (clue_max < 10)? 1: (clue_max < 100)? 2: 3;

    /*
       Now we begin our output routines. First, if the user has requested
       either the solution or the empty crossword grid, we output those.
       Next, if the user has requested statistics, we output those.
       Then, we output the clues---either in two lists (by default) or
       in one big list, in which case we have to tag each word with a
       "horizontal" or "vertical" marker as well as the clue number.
    */
    if (ShowGrid || ShowSolution) {
        int clue_idx = 0;
        for (y=0; y < ymax; ++y)
        {
            if (ShowGrid) {
                if (ShowNumbers) {
                    for (x=0; x < xmax; ++x) {
                        if (grid[y][x] == '#')
                          putc('#', out);
                        else if (x == clues[clue_idx][0]
                              && y == clues[clue_idx][1])
                          putc('0', out), ++clue_idx;
                        else  putc('`', out);
                    }
                }
                else {
                    for (x=0; x < xmax; ++x)
                      putc("`#"[grid[y][x]=='#'], out);
                }
                if (ShowSolution) fputs("          ", out);
            }
            if (ShowSolution) {
                for (x=0; x < xmax; ++x)
                  putc((grid[y][x]=='#')? ' ': grid[y][x], out);
            }
            putc('\n', out);
        }
        fputs("\n\n", out);
    }

    /*
       Tally the statistics on word length and square count.
    */
    if (ShowFreqs) {
        int hlen[MAXGRID+1]={0}, vlen[MAXGRID+1]={0};
        int hcount=0, vcount=0;
        int blackcount=0, cheatercount=0;
        int asymmetric=0;
        int ltrcount[26]={0}, ltrtotal=0;
        int wordlen;
        for (clue_idx = 0; clue_idx < clue_max; ++clue_idx) {
            int dir = clues[clue_idx][2];
            if (dir & HORIZ) {
                x = clues[clue_idx][0]; y = clues[clue_idx][1];
                wordlen = 0;
                do {
                    ++x; ++wordlen;
                } while (x < xmax && grid[y][x] != '#');
                hcount += 1; hlen[wordlen] += 1;
            }
            if (dir & VERT) {
                x = clues[clue_idx][0]; y = clues[clue_idx][1];
                wordlen = 0;
                do {
                    ++y; ++wordlen;
                } while (y < ymax && grid[y][x] != '#');
                vcount += 1; vlen[wordlen] += 1;
            }
        }
        for (y=0; y < ymax; ++y) {
            for (x=0; x < xmax; ++x) {
                if ((grid[y][x]=='#') != (grid[ymax-y-1][xmax-x-1]=='#')) {
                    asymmetric = 1;
                }
                if (grid[y][x] == '#') {
                    int blocked[4] = {0};
                    ++blackcount;
                    blocked[0] = (y==0 || grid[y-1][x] == '#');
                    blocked[1] = (y==ymax-1 || grid[y+1][x] == '#');
                    blocked[2] = (x==0 || grid[y][x-1] == '#');
                    blocked[3] = (x==xmax-1 || grid[y][x+1] == '#');
                    if ((blocked[0] && blocked[1]) || (blocked[2] && blocked[3]))
                      /* do nothing */ ;
                    else if (blocked[0]+blocked[1]+blocked[2]+blocked[3] == 2)
                      ++cheatercount;
                }
                if (isalpha(grid[y][x])) {
                    ltrtotal += 1;
                    ltrcount[toupper(grid[y][x])-'A'] += 1;
                }
            }
        }

        fprintf(out, "STATISTICS\n");
        fprintf(out, "----------\n\n");
        fprintf(out, "Dimensions: %dx%d\n", xmax, ymax);
        fprintf(out, "Word count: %d\n", hcount+vcount);
        fprintf(out, "Black squares: %d (%.2g%%)", blackcount,
            100.*blackcount/((double)xmax*ymax));
        if (xmax==15 && ymax==15 && blackcount != 36)
          fprintf(out, " (%+d)", blackcount-36);
        fprintf(out, "\n");
        if (cheatercount)
          fprintf(out, "Cheaters: %d\n", cheatercount);
        fprintf(out, "Avg. word length: %0.2g\n",
            (2.*((double)xmax*ymax - blackcount) - (hlen[1]+vlen[1]))
                / (hcount+vcount));
        fprintf(out, "\n");
        fprintf(out, "Long words:\n");
        for (y=0, k=9; k <= MAXGRID; ++k)
          if (hlen[k] || vlen[k])
            { y=1; fprintf(out, " %d(%d)", k, hlen[k]+vlen[k]); }
        fprintf(out, "%s\n\n", (y? "": " none"));
        if (hlen[1] || vlen[1])
          fprintf(out, "Contains %d unchecked letter%s.\n", hlen[1]+vlen[1],
            &"s"[hlen[1]+vlen[1] == 1]);
        if (hlen[2] || vlen[2])
          fprintf(out, "Contains %d two-letter word%s.\n", hlen[2]+vlen[2],
            &"s"[hlen[2]+vlen[2] == 1]);
        if (asymmetric)
          fprintf(out, "This grid is not symmetric!\n");

        fprintf(out, "Horizontal word count: %d\n", hcount);
        fprintf(out, "Vertical word count: %d\n\n", vcount);

        if (ltrtotal > 0) {
            int nused=0, nunused=0;
            char used[26], unused[26];
            for (k=0; k < 26; ++k) {
                if (ltrcount[k] > 0)  used[nused++] = 'A'+k;
                else  unused[nunused++] = 'A'+k;
            }
            if (nunused > 0)
              fprintf(out, "Letters unused: %.*s\n", nunused, unused);
            else  fprintf(out, "Pangrammatic.\n");
        }
        if (ShowHorizontal || ShowVertical)
          fprintf(out, "\n\n");
    }

    /*
       Now that the grids and charts have been printed, we turn to
       printing the clues.  If the user has requested that we print the
       clues intermixed, then we have an easy time of it. Otherwise, we
       will need to do two passes over the clue list: one for the 
       horizontal clues and one for the verticals.
    */
    if (ShowTogether)
    {
            const char *clue_format =
                (AdobeClues? "%*d. %-8s\t": "%0.0d%d. %-8s  ");
            fputs("HORIZONTAL AND VERTICAL\n", out);
            fputs("--------------------------\n", out);
            for (clue_idx = 0; clue_idx < clue_max; ++clue_idx)
            {
                int dir = clues[clue_idx][2];
                if (dir & HORIZ) {
                    fprintf(out, clue_format, clue_width, clue_idx+1,
                       (dir & VERT)? "(Horiz.)": "");
                    x = clues[clue_idx][0]; y = clues[clue_idx][1];
                    do {
                        putc(grid[y][x++], out);
                    } while (x < xmax && grid[y][x] != '#');
                    putc('\n', out);
                }
                if (dir & VERT) {
                    fprintf(out, clue_format, clue_width, clue_idx+1,
                       (dir & HORIZ)? "(Vert.)": "");
                    x = clues[clue_idx][0]; y = clues[clue_idx][1];
                    do {
                        putc(grid[y++][x], out);
                    } while (y < ymax && grid[y][x] != '#');
                    putc('\n', out);
                }
            }
    }
    else {
        const char *clue_format =
            (AdobeClues? "%0.0d%d.\t": "%*d.  ");
        if (ShowHorizontal) {
            fputs("HORIZONTAL\n", out);
            fputs("---------------------\n", out);
            for (clue_idx = 0; clue_idx < clue_max; ++clue_idx)
            {
                if (!(clues[clue_idx][2] & HORIZ))
                  continue;
                fprintf(out, clue_format, clue_width, clue_idx+1);
                x = clues[clue_idx][0]; y = clues[clue_idx][1];
                do {
                    putc(grid[y][x++], out);
                } while (x < xmax && grid[y][x] != '#');
                putc('\n', out);
            }
            if (ShowVertical) fputs("\n\n", out);
        }

        if (ShowVertical) {
            fputs("VERTICAL\n", out);
            fputs("---------------------\n", out);
            for (clue_idx = 0; clue_idx < clue_max; ++clue_idx)
            {
                if (!(clues[clue_idx][2] & VERT))
                  continue;
                fprintf(out, clue_format, clue_width, clue_idx+1);
                x = clues[clue_idx][0]; y = clues[clue_idx][1];
                do {
                    putc(grid[y++][x], out);
                } while (y < ymax && grid[y][x] != '#');
                putc('\n', out);
            }
        }
    }

    return 0;
}


void do_error(const char *fmat, ...)
{
    va_list ap;
    printf("%s: ", Argv0);
    va_start(ap, fmat);
    vprintf(fmat, ap);
    printf("\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}


void do_help(int man)
{
    if (man)
      goto man;
    puts("xword-ent [-?h] [-fghnstv] [-o outfile] filename");
    puts("Lists words in a crossword template.");
    puts("  -H: show (only) horizontal clues");
    puts("  -V: show (only) vertical clues");
    puts("  -T: show H and V clues together, instead of in two lists");
    puts("  -S: show solution (letters only)");
    puts("  -G: show grid (hashes and ticks only)");
    puts("  -N:   ...with clue-number placeholders");
    puts("  -F: show frequency charts and statistics");
    puts("  -o filename: send output to specified file");
    puts("  --help: show this message");
    puts("  --man: show complete help text");
    exit(0);
  man:
    puts("xword-ent: Crossword cluing tool.\n");
    puts(" This is what this program does, in present tense, using");
    puts("   a new paragraph to describe each option or parameter.");
    exit(0);
}
