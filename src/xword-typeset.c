
/*
   |Xword-typeset|, a public-domain program by Arthur O'Dwyer,
   July 2005. For use with Gerd Neugebauer's |cwpuzzle| package,
   version 1.4, dated November 1996.
   Modifications by Arthur O'Dwyer, 2022.

     This program reads a grid of letters and hashmarks (|#|) from
   a text file, and then outputs a LaTeX file containing instructions
   for typesetting the grid using Gerd Neugebauer's |cwpuzzle| package,
   and placeholders for adding the clues.

     If the file begins with a quoted string, then that string is
   turned into a title for the typeset page.
     If there appear to be crossword clues following the grid, then
   those clues are typeset instead of clue placeholders.
*/

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define steq(x,y) (!strcmp(x,y))
#define stneq(x,y) (!steq(x,y))
#define NELEM(a) ((int)(sizeof a / sizeof *a))

/* Grids can be no bigger than |MAXGRID| squares on a side. */
#define MAXGRID 45

/* The number of typographer's points in one inch */
#define INCH_IN_POINTS 72

static char *Argv0;

/* These two values are square sizes in tenths of typographer's points */
static int MinUnitlength = 100;
static int DefaultUnitlength = 200;

static int UseCwpuzzleSty = 0;
static int UseMulticol = 0;
static int PrintTitle = 1;
static int PrintPuzzleGrid = 1;
static int PrintSolutionGrid = 0;
static int PrintClues = 1;
static char *OutputFilename = NULL;
static char *PuzzleTitle = NULL;
enum {HORIZ=1, VERT=2};

int process(FILE *in, FILE *out);
  static int read_xword(FILE *in, char grid[MAXGRID][MAXGRID],
                        int *pxmax, int *pymax);
  static int trim_xword(char grid[MAXGRID][MAXGRID],
                        int *pxmax, int *pymax);
  static int compute_clue_positions(char grid[MAXGRID][MAXGRID],
                                    int xmax, int ymax,
                                    int (**pclues)[3], int *pclue_max);
  static int read_clues(FILE *in, int clue_max,
                        char ***phcluetext, char ***pvcluetext);
    static int is_blank(const char *line);
    static int is_adorned(const char *text, const char *pattern);
    static int extract_clue(int what, const char *line, int clue_max,
                            char **hcluetext, char **vcluetext);
      static void trim_end(char *s);
  static int dump_boilerplate(FILE *out, int xmax, int ymax);
  static int dump_HWEB_to_TeX(FILE *out, const char *hweb);

void do_error(const char *fmat, ...);
void do_help(int);


int main(int argc, char **argv)
{
    int LiteralInputNames = 0;
    int i, j;

    Argv0 = argv[0];

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
        } else if (steq(argv[i]+1, "-solution-only")) {
            PrintTitle = 0;
            PrintPuzzleGrid = 0;
            PrintSolutionGrid = 1;
            PrintClues = 0;
        } else if (steq(argv[i], "-o") || steq(argv[i], "-O")) {
            if (i >= argc-1) {
                do_error("Need output filename with -o");
            }
            OutputFilename = argv[++i];
        } else {
            for (j=1; argv[i][j]; ++j) {
                switch (argv[i][j]) {
                case 'H': case 'h': do_help(0); break;
                case 'P': UseCwpuzzleSty = 1; break;
                case 'p': UseCwpuzzleSty = 0; break;
                case '1': UseMulticol = 1; break;
                default:
                    do_error("Unrecognized option(s) %s; -h for help",
                        argv[i]);
                }
            }
        }
    }

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
    int x, y;
    int (*clues)[3], clue_idx, clue_max;
    char **hcluetext = NULL, **vcluetext = NULL;

    read_xword(in, grid, &xmax, &ymax);
    if (ymax == 1) {
        if (grid[0][0] == '"')
        {
            /* This is just the title of the crossword. */
            if (PuzzleTitle == NULL) {
                PuzzleTitle = malloc(xmax);
                for (x=1; x < xmax && grid[0][x] != '"'; ++x)
                  PuzzleTitle[x-1] = grid[0][x];
                PuzzleTitle[x-1] = '\0';
            }
            /* Read the crossword grid properly this time. */
            read_xword(in, grid, &xmax, &ymax);
        }
    }

    trim_xword(grid, &xmax, &ymax);

    compute_clue_positions(grid, xmax, ymax, &clues, &clue_max);

    read_clues(in, clue_max, &hcluetext, &vcluetext);

    /*
       Now we begin our output routine.
    */
    const char *dcarg = UseCwpuzzleSty ? "" :
                        UseMulticol ? "" : "[twocolumn]";
    fprintf(out, "\\documentclass%s{article}\n", dcarg);
    fprintf(out, "\\usepackage[left=1cm, right=1cm, top=2cm, bottom=1cm]{geometry}\n");
    fprintf(out, "\\usepackage[utf8]{inputenc}\n");
    fprintf(out, "\\usepackage[T1]{fontenc}\n");
    fprintf(out, "\\usepackage{pict2e}\n");
    if (UseMulticol) {
        fprintf(out, "\\usepackage{multicol}\n");
    }
    if (UseCwpuzzleSty) {
        fprintf(out, "\\usepackage{cwpuzzle}\n\n");
        fprintf(out, "\\newenvironment{AcrossClues}{\\begin{Clues}{\\textbf{Across}}}{\\end{Clues}}\n");
        fprintf(out, "\\newenvironment{DownClues}{\\begin{Clues}{\\textbf{Down}}}{\\end{Clues}}\n");
    } else {
        dump_boilerplate(out, xmax, ymax);
    }
    fprintf(out, "\\begin{document}\n");
    fprintf(out, "\\pagestyle{empty}\\raggedright\n");

    if (PrintTitle && PuzzleTitle != NULL) {
        fputs("\\section*{", out);
        dump_HWEB_to_TeX(out, PuzzleTitle);
        fputs("}\n", out);
    }

    if (PrintPuzzleGrid) {
        fprintf(out, "\\begin{Puzzle}{%d}{%d}%%\n", xmax, ymax);
        clue_idx = 0;
        for (y=0; y < ymax; ++y) {
            fprintf(out, "  ");
            for (x=0; x < xmax; ++x) {
                char cell = grid[y][x];
                if (cell == '.') {
                    cell = 'X';  // '.' is magic to cwpuzzle.sty
                }
                if (cell == '#') {
                    fprintf(out, "|* ");
                } else if ((clues[clue_idx][0]==x) && (clues[clue_idx][1]==y)) {
                    fprintf(out, "|[%d]%c ", clue_idx+1, cell);
                    ++clue_idx;
                } else {
                    fprintf(out, "|%c ", cell);
                }
            }
            fprintf(out, "|.\n");
        }
        fputs("\\end{Puzzle}\n\n", out);
    }

    if (PrintSolutionGrid) {
        fprintf(out, "\\begin{Puzzle}{%d}{%d}%%\n", xmax, ymax);
        for (y=0; y < ymax; ++y) {
            fprintf(out, "  ");
            for (x=0; x < xmax; ++x) {
                char cell = grid[y][x];
                if (cell == '#') {
                    fprintf(out, "|*    ");
                } else {
                    fprintf(out, "|[%c]X ", toupper(cell));
                }
            }
            fprintf(out, "|.\n");
        }
        fputs("\\end{Puzzle}\n\n", out);
    }

    if (PrintClues) {
        if (UseMulticol) {
            fputs("\\begin{multicols}{2}\n", out);
        }
        fputs("\\begin{AcrossClues}%\n", out);
        for (clue_idx=0; clue_idx < clue_max; ++clue_idx) {
            if ((clues[clue_idx][2] & HORIZ) == 0) continue;
            fprintf(out, "  \\Clue{%d}{", clue_idx+1);
            y = clues[clue_idx][1];
            for (x = clues[clue_idx][0]; x < xmax && grid[y][x] != '#'; ++x)
              fprintf(out, "%c", grid[y][x]);
            fprintf(out, "}{");
            if (hcluetext && hcluetext[clue_idx])
              dump_HWEB_to_TeX(out, hcluetext[clue_idx]);
            else fprintf(out, "clue");
            fprintf(out, "}\n");
        }
        fputs("\\end{AcrossClues}%\n", out);

        fputs("\\begin{DownClues}%\n", out);
        for (clue_idx=0; clue_idx < clue_max; ++clue_idx) {
            if ((clues[clue_idx][2] & VERT) == 0) continue;
            fprintf(out, "  \\Clue{%d}{", clue_idx+1);
            x = clues[clue_idx][0];
            for (y = clues[clue_idx][1]; y < ymax && grid[y][x] != '#'; ++y)
              fprintf(out, "%c", grid[y][x]);
            fprintf(out, "}{");
            if (vcluetext && vcluetext[clue_idx])
              dump_HWEB_to_TeX(out, vcluetext[clue_idx]);
            else fprintf(out, "clue");
            fprintf(out, "}\n");
        }
        fputs("\\end{DownClues}\n", out);
        if (UseMulticol) {
            fputs("\\end{multicols}\n", out);
        }
    }

    fputs("\n\\end{document}\n", out);


    free(PuzzleTitle);
    if (hcluetext) {
        for (clue_idx=0; clue_idx < clue_max; ++clue_idx)
          free(hcluetext[clue_idx]);
        free(hcluetext);
    }
    if (vcluetext) {
        for (clue_idx=0; clue_idx < clue_max; ++clue_idx)
          free(vcluetext[clue_idx]);
        free(vcluetext);
    }

    return 0;
}


static int read_xword(FILE *in, char grid[MAXGRID][MAXGRID],
                      int *pxmax, int *pymax)
{
    int xmax = 0;
    int ymax = 0;
    int x, y, k;
    int left_padding;

    /* Initialize to a blank grid. */
    for (y=0; y < MAXGRID; ++y)
      for (x=0; x < MAXGRID; ++x)
        grid[y][x] = ' ';

    /*
       Read in the letter grid. The grid is filled in row-major order,
       so that |grid[0]| contains the first row of the file, |grid[1]|
       contains the second row, and so on.  A blank line is considered
       to end the grid.
    */
    x = y = 0;
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
       Remove whitespace "padding" from the grid's left side.
    */
    left_padding = xmax;
    for (y=0; y < ymax; ++y) {
        for (x=0; x < xmax && isspace(grid[y][x]); ++x) continue;
        if (x < left_padding) left_padding = x;
    }
    if (left_padding > 0) {
        xmax -= left_padding;
        for (y=0; y < ymax; ++y)
          memmove(grid[y], grid[y]+left_padding, xmax);
    }

    *pxmax = xmax;
    *pymax = ymax;
    return 0;
}


static int compute_clue_positions(char grid[MAXGRID][MAXGRID],
                                  int xmax, int ymax,
                                  int (**pclues)[3], int *pclue_max)
{
    int x, y;
    int clue_idx, clue_max;
    int (*clues)[3];

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
    clues[clue_max][0] = 0;
    clues[clue_max][1] = 0;
    clues[clue_max][2] = 0;

    *pclue_max = clue_max;
    *pclues = clues;
    return 0;
}


/*
   If the provided grid looks a lot like /two/ grids set side by side,
   look for the one with some letters in it, and use only that one.
   Use the leftmost one if neither one has letters. If the resulting
   grid has no hashes, then replace all non-letters by hashes.
*/
static int trim_xword(char grid[MAXGRID][MAXGRID], int *pxmax, int *pymax)
{
    int xmax = *pxmax, ymax = *pymax;
    int x, y;
    int grid1end = 0;
    int grid2start = 0;
    int grid1hasletters = 0;
    int grid2hasletters = 0;
    int hashashes;

    for (x=0; x < xmax; ++x) {
        int hasletters = 0;
        for (y=0; y < ymax; ++y) {
            if (isalpha(grid[y][x])) hasletters = 1;
            if (!isspace(grid[y][x])) break;
        }
        if (grid1end == 0) {
            if (y == ymax)
              grid1end = x;
            else if (hasletters)
              grid1hasletters = 1;
        }
        else if (grid1end != 0 && y < ymax) {
            if (grid2start == 0)
              grid2start = x;
            if (hasletters)
              grid2hasletters = 1;
        }
    }

    if (grid2start == 0) return 0;

    if (grid2hasletters && !grid1hasletters) {
        xmax -= grid2start;
        for (y=0; y < ymax; ++y)
          memmove(grid[y], grid[y]+grid2start, xmax);
    }
    else {
        xmax = grid1end;
    }

    hashashes = 0;
    for (y=0; !hashashes && y < ymax; ++y)
      for (x=0; x < xmax; ++x)
        if (grid[y][x] == '#') { hashashes = 1; break; }
    if (!hashashes)
    {
        for (y=0; y < ymax; ++y)
          for (x=0; x < xmax; ++x)
            if (isspace(grid[y][x])) grid[y][x] = '#';
    }

    *pxmax = xmax;
    *pymax = ymax;
    return 0;
}


static int read_clues(FILE *in, int clue_max,
                      char ***phcluetext, char ***pvcluetext)
{
    char **hcluetext, **vcluetext;
    int clue_idx;
    char line[100];
    int seen_Across, seen_Down;

    *phcluetext = *pvcluetext = NULL;

    seen_Across = 0;
    do {
    top_of_Across_loop:
        if (fgets(line, sizeof line, in) == NULL) return -1;
        if (is_adorned(line, "Across") || is_adorned(line, "Horizontal"))
        {
            if (seen_Across) return -1;
            seen_Across = 1;
            goto top_of_Across_loop;
        }
    } while (is_blank(line));

    if (!seen_Across) return 0;

    /*
       At this point, we initialize the clue buffers.
    */
    hcluetext = malloc(clue_max * sizeof *hcluetext);
    vcluetext = malloc(clue_max * sizeof *vcluetext);
    if (!hcluetext || !vcluetext) return -3;

    for (clue_idx = 0; clue_idx < clue_max; ++clue_idx)
      hcluetext[clue_idx] = vcluetext[clue_idx] = NULL;

    *phcluetext = hcluetext;
    *pvcluetext = vcluetext;

    while (!extract_clue(HORIZ, line, clue_max, hcluetext, vcluetext)) {
        if (fgets(line, sizeof line, in) == NULL) return -1;
    }

    seen_Down = 0;
    do {
    top_of_Down_loop:
        if (fgets(line, sizeof line, in) == NULL) return -1;
        if (is_adorned(line, "Down") || is_adorned(line, "Vertical"))
        {
            if (seen_Down) return -1;
            seen_Down = 1;
            goto top_of_Down_loop;
        }
    } while (is_blank(line));

    if (!seen_Down) return 0;

    while (!extract_clue(VERT, line, clue_max, hcluetext, vcluetext)) {
        if (fgets(line, sizeof line, in) == NULL) return -1;
    }

    return 0;
}


static int is_blank(const char *line)
{
    int i;
    for (i=0; line[i]; ++i)
      if (!isspace(line[i]) && !ispunct(line[i])) return 0;
    return 1;
}


static int is_adorned(const char *line, const char *pattern)
{
    int i, j;
    for (i=j=0; line[i]; ++i) {
        if (isalnum(line[i])) {
            if (tolower(line[i]) == tolower(pattern[j]))
              ++j;
            else return 0;
        }
    }
    return (pattern[j] == '\0');
}


static int extract_clue(int what, const char *line, int clue_max,
                        char **hcluetext, char **vcluetext)
{
    int i, clue_idx;
    char *this_clue;

    if ((what & (HORIZ | VERT)) == 0 || line == NULL) return -2;
    for (i=0; line[i] && strchr(" \t#.-=", line[i]); ++i)
      continue;
    if (!isdigit(line[i])) return -1;
    for (clue_idx = 0; isdigit(line[i]); ++i) {
        if (clue_idx >= (INT_MAX-9)/10) return -4;
        clue_idx = clue_idx*10 + (line[i]-'0');
    }
    if (clue_idx < 1 || clue_idx > clue_max) return -4;

    /*
       At this point the program believes it has found a valid
       clue number. The number might end with some punctuation,
       and then some whitespace.
       Everything after that is part of the clue text.
    */
    while (ispunct(line[i])) ++i;
    while (isspace(line[i])) ++i;
    this_clue = malloc(strlen(line+i) + 1);
    if (this_clue == NULL) return -3;
    strcpy(this_clue, line+i);
    trim_end(this_clue);

    if (what & HORIZ) {
        hcluetext[clue_idx-1] = this_clue;
    }
    else if (what & VERT) {
        vcluetext[clue_idx-1] = this_clue;
    }
    return 0;
}


/*
   This routine strips any excess whitespace from the end of the
   given string.
*/
static void trim_end(char *s)
{
    char *p = strchr(s, '\0');
    while (p > s && isspace(p[-1])) --p;
    *p = '\0';
    return;
}


static int dump_boilerplate(FILE *out, int xmax, int ymax)
{
    /*
       We calculate the size of the puzzle's squares based
       on how many we need to fit on the page. We never go
       bigger than 20-point squares, but we may go down to
       as small as 10-point squares if we need to.
    */
    int PuzzleUnitlength = DefaultUnitlength;
    if (UseCwpuzzleSty) {
        if (PuzzleUnitlength*xmax > 75*INCH_IN_POINTS)
          PuzzleUnitlength = (75*INCH_IN_POINTS) / xmax;
    } else {
        /* Two-column layout. */
        if (PuzzleUnitlength*xmax > 37*INCH_IN_POINTS)
          PuzzleUnitlength = (37*INCH_IN_POINTS) / xmax;
    }
    if (PuzzleUnitlength*ymax > 100*INCH_IN_POINTS)
      PuzzleUnitlength = (100*INCH_IN_POINTS) / ymax;
    if (PuzzleUnitlength < MinUnitlength)
      PuzzleUnitlength = MinUnitlength;

    fprintf(out, "\\newlength\\PuzzleUnitlength\n");
    fprintf(out, "\\PuzzleUnitlength=%.1fpt\n", PuzzleUnitlength/10.0);
    fprintf(out, "\\newcommand\\PuzzleNumberFont{\\sf\\scriptsize}\n");
    fprintf(out, "\\newcommand\\PuzzleSolutionFont{\\sf\\bfseries\\LARGE}\n");
    fprintf(out, "\\newcount\\PuzzleX\n");
    fprintf(out, "\\newcount\\PuzzleY\n");
    fprintf(out, "\\newcommand\\PuzzleBlackBox{\\rule{"
                         "\\PuzzleUnitlength}{\\PuzzleUnitlength}}\n");
    fprintf(out, "\\newcommand\\PuzzleBox[2][]{%%\n");
    fprintf(out, "  \\def\\Puzzletmp{#2}%%\n");
    fprintf(out, "  \\if\\Puzzletmp.\n");
    fprintf(out, "    \\PuzzleX=0\\relax \\advance\\PuzzleY-1\n");
    fprintf(out, "  \\else\n");
    fprintf(out, "    \\ifx\\empty\\Puzzletmp\n");
    fprintf(out, "    \\else\\if\\Puzzletmp *\n");
    fprintf(out, "      \\put(\\PuzzleX,\\PuzzleY){\\framebox(1,1){\\PuzzleBlackBox}}\n");
    fprintf(out, "    \\else\n");
    fprintf(out, "      \\put(\\PuzzleX,\\PuzzleY){\\framebox(1,1){}}\n");
    fprintf(out, "    \\fi\\fi\n");
    fprintf(out, "    \\def\\Puzzletmp{#1}%%\n");
    fprintf(out, "    \\ifx\\empty\\Puzzletmp\n");
    fprintf(out, "    \\else\n");
    if (PrintSolutionGrid) {
        assert(!PrintPuzzleGrid);  // TODO: permit them to coexist
        fprintf(out, "      \\put(\\PuzzleX,\\PuzzleY){\\makebox(1,1){\\PuzzleSolutionFont #1}}\n");
    } else {
        fprintf(out, "      \\put(\\PuzzleX,\\PuzzleY){\\makebox(1,.9)[tl]{\\hspace{.08\\PuzzleUnitlength}\\PuzzleNumberFont #1}}\n");
    }
    fprintf(out, "    \\fi\n");
    fprintf(out, "    \\advance\\PuzzleX 1\n");
    fprintf(out, "  \\fi\n");
    fprintf(out, "}\n");
    fprintf(out, "\\newcommand\\PuzzleCircledBox[2][]{%%\n");
    fprintf(out, "  \\put(\\the\\PuzzleX.5,\\the\\PuzzleY.5){\\circle{.94}}\n");
    fprintf(out, "  \\PuzzleBox[#1]{#2}\n");
    fprintf(out, "}\n");
    fprintf(out, "\\begingroup\n");
    fprintf(out, "  \\catcode`\\|=13\\catcode`\\(=13\\catcode`\\_=13\n");
    fprintf(out, "  \\gdef\\PuzzleHelper{\\catcode`\\|=13\\catcode`\\(=13\\let|=\\PuzzleBox\\let(=\\PuzzleCircledBox}\n");
    fprintf(out, "  \\gdef\\ClueHelper{\\catcode`\\_=13\\def_{\\underline{\\hskip 1ex}}\\catcode`\\&=12}\n");
    fprintf(out, "\\endgroup\n");
    fprintf(out, "\\newenvironment{Puzzle}[2]{"
                                    "\\par\\noindent\\PuzzleHelper\n");
    fprintf(out, "  \\let\\unitlength=\\PuzzleUnitlength"
                                                    " \\PuzzleY=#2\n");
    fprintf(out, "  \\begin{picture}(#1,#2)\\PuzzleBox.\n");
    fprintf(out, "}{\\end{picture}\\par\\noindent}\n");
    fprintf(out, "\\makeatletter\n");
    fprintf(out, "  \\def\\cluesec{\\@startsection{}{1}{\\z@}\n");
    fprintf(out, "    {-3.25ex plus -1ex minus -.2ex}"
                                   "{.8ex plus .1ex}{\\large\\bf}}\n");
    fprintf(out, "\\makeatother\n");
    fprintf(out, "\\newenvironment{AcrossClues}{\\ClueHelper"
                              "\\cluesec*{Across}\\footnotesize}{}\n");
    fprintf(out, "\\newenvironment{DownClues}{\\ClueHelper"
                                "\\cluesec*{Down}\\footnotesize}{}\n");
    fprintf(out, "\\newcommand\\Clue[3]{\\noindent"
                           "\\makebox[0pt][l]{#1.}\\qquad #3\\par}\n");
    fprintf(out, "\n");
    return 0;
}


/*
   This routine takes a user-defined string in something like HWEB
   format, in which slashes (|/|) surround /italicized/ text, and
   double quotation marks are not treated specially as they are in
   TeX. This routine writes a TeX conversion of that string to
   the provided output file. Notice that we do not specially handle
   "fill-in-the-blanks" underscores unless the user wants us to use
   |cwpuzzle.sty|; otherwise, underscores are handled with TeX macros
   provided by our boilerplate code.
*/
static int dump_HWEB_to_TeX(FILE *out, const char *hweb)
{
    int inside_it = 0;
    int i;

    for (i=0; hweb[i] != '\0'; ++i)
    {
        if (hweb[i] == '/' && !inside_it && (i==0 || !isalnum(hweb[i-1])))
        {
            inside_it = 1;
            fputs("{\\it ", out);
        }
        else if (hweb[i] == '/' && inside_it && !isalnum(hweb[i]))
        {
            inside_it = 0;
            fputs("}", out);
        }
        else if (hweb[i] == '"' && (i==0 || isspace(hweb[i-1])))
        {
            fputs("``", out);
        }
        else if (hweb[i] == '"')
        {
            fputs("''", out);
        }
        else if (hweb[i] == '&')
        {
            fputs("\\&", out);
        }
        else if (hweb[i] == '#')
        {
            fputs("\\#", out);
        }
        else if (hweb[i] == '$')
        {
            fputs("\\$", out);
        }
        else if (hweb[i] == '\\')
        {
            if (hweb[i+1] && strchr("'`^~\"c", hweb[i+1]))
              fputs("\\", out);
            else fputs("\\textbackslash", out);
        }
        else if (hweb[i] == '_')
        {
            if (UseCwpuzzleSty)
              fputs("\\_", out);
            else fputs("_", out);
        }
        else putc(hweb[i], out);
    }

    if (inside_it) putc('}', out);
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
    puts("xword-typeset [-?h] [-Pp1] [--solution-only] [-o outfile] filename");
    puts("Typesets a crossword puzzle in LaTeX.");
    puts("  -P[p]: Use [don't use] Gerd Neugebauer's cwpuzzle package");
    puts("  -1: Don't lay out clues beside the grid");
    puts("  --solution-only: Print only the solution grid");
    puts("  -o filename: send output to specified file");
    puts("  --help: show this message");
    puts("  --man: show complete help text");
    exit(0);
  man:
    puts("xword-typeset: Crossword typesetting tool.\n");
    puts(" This program takes as input a crossword puzzle (in the");
    puts("   same format as the input to 'xword-ent') and produces");
    puts("   a version of the same crossword typeset with LaTeX.");
    puts(" The input may also be in the format output by 'xword-ent',");
    puts("   with any combination of -SG and -HV options. This");
    puts("   program currently does not attempt to deal sensibly");
    puts("   with the output of 'xword-ent -T'.");
    puts(" The -P option tells 'xword-typeset' to produce LaTeX code");
    puts("   that uses Gerd Neugebauer's 'cwpuzzle' package. The");
    puts("   complementary -p option (the default) produces \"raw\"");
    puts("   LaTeX code heavily derivative of 'cwpuzzle'. The default");
    puts("   is recommended, because it handles the special character");
    puts("   '_' in an intuitive manner.");
    puts(" The --solution-only option prints a solution grid, without");
    puts("   title, clues, or grid numbers.");
    puts(" The -1 option tells 'xword-typeset' to use the 'multicol'");
    puts("   package in order to typeset the Across and Down clues in");
    puts("   two-column layout, starting below the grid. The default");
    puts("   behavior is to typeset the entire page in two-column");
    puts("   layout, with some clues appearing to the right of the");
    puts("   grid.");
    puts("");
    puts(" If the input file provides clues following the grid, they");
    puts("   should be in the form");
    puts("     [number][punctuation-opt][whitespace-opt][clue text]");
    puts("   with the text of the clue all on the same line. The clue");
    puts("   text should be in HWEB format: /italics/ and |teletype|");
    puts("   work as expected, and \"quotes\" do not need to be entered");
    puts("   as ``TeX-style'' quotes (although that will also work).");
    puts("   En-dashes and em-dashes are entered as -- and ---.");
    puts(" If the input file does not provide any clues, or skips some");
    puts("   clues, they will be shown as the placeholder text \"clue\".");
    puts(" If the input file starts with a quoted string between blank");
    puts("   lines, that string will be typeset as the title of the");
    puts("   crossword. For example:\n");
    puts("     \"A Simple Example\"\n");
    puts("     #HAM#");
    puts("     FERAL");
    puts("     ENERO");
    puts("     ENACT");
    puts("     #ASH#");
    exit(0);
}
