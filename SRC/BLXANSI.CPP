/*****************************************************************************
 *   BLXANSI.CPP                                               BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   ANSI renderer for the BloX game screen.  See BLXANSI.H for the         *
 *   design overview.                                                        *
 *                                                                           *
 *   Screen layout (80 x 24, 1-based ANSI coordinates):                     *
 *                                                                           *
 *     Row 1        title bar                                               *
 *     Rows 2-23    playfield frame; interior rows 3-22, cols 29-48         *
 *                  (each playfield cell is two characters wide)            *
 *     Left panel   player name, controls, combo indicator (cols 3-25)      *
 *     Right panel  score, level, lines, time, Next preview, best score     *
 *                  (cols 54-66)                                            *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include "BLXCORE.H"
#include "BLXANSI.H"

/*--------------------------------------------------------------------------
 * Screen geometry.
 *--------------------------------------------------------------------------*/
#define FRAME_TOP    2             /* row of the playfield's top border     */
#define FIELD_ROW    3             /* first interior playfield row          */
#define FRAME_LEFT   28            /* column of the left border             */
#define FIELD_COL    29            /* first interior playfield column       */
#define FRAME_RIGHT  (FIELD_COL+BLX_W*2)      /* right border column        */
#define FRAME_BOT    (FIELD_ROW+BLX_VISH)     /* bottom border row          */

#define LPAN_COL     3             /* left panel column                     */
#define RPAN_COL     54            /* right panel column                    */

/* IBM PC line-drawing and block characters (code page 437).                */
#define CH_H         '\xC4'        /* horizontal line                       */
#define CH_V         '\xB3'        /* vertical line                         */
#define CH_TL        '\xDA'        /* top-left corner                       */
#define CH_TR        '\xBF'        /* top-right corner                      */
#define CH_BL        '\xC0'        /* bottom-left corner                    */
#define CH_BR        '\xD9'        /* bottom-right corner                   */
#define CH_SOLID     '\xDB'        /* solid block (settled/falling cells)   */
#define CH_GHOST     '\xB0'        /* light shade (ghost landing spot)      */

/* Cell codes used in the shadow and in the desired screen image.           */
#define CC_EMPTY     0             /* blank cell                            */
                                   /* 1..7 = solid cell, colored by piece   */
#define CC_GHOST0    8             /* 8..14 = ghost cell, colored by piece  */
#define CC_FLASH     15            /* white flash during a line clear       */
#define CC_INVALID   0xFF          /* forces a repaint of the cell          */

/*--------------------------------------------------------------------------
 * Per-piece colors.  Index 1..7 matches the cell values the engine stores
 * (piece + 1).  Each entry is {bold, ANSI foreground}.
 *--------------------------------------------------------------------------*/
static const unsigned char cellBold[8]={ 0, 1, 1, 1, 0, 1, 1, 1 };
static const unsigned char cellFg[8]  ={ 0,36,33,35,33,34,32,31 };
                                   /*     I  O  T  L  J  S  Z              */

/*--------------------------------------------------------------------------
 * Emit context: a small append-only text builder that also remembers the
 * terminal's cursor position and color state, so it can skip cursor moves
 * and color changes that would have no effect.  Fewer bytes on the wire
 * means smoother play on slow connections.
 *--------------------------------------------------------------------------*/
struct Emit {
    char *buf;
    int   len;
    int   row, col;                /* believed cursor position (0=unknown)  */
    int   attr;                    /* believed color: bold*100+fg (0=unk)   */
};

static void
emitRaw(Emit *e, const char *s, int n)
{
    if (e->len+n < BLXA_BUFSIZ-1) {
        memcpy(e->buf+e->len, s, n);
        e->len+=n;
    }
}

static void
emitStr(Emit *e, const char *s)
{
    emitRaw(e, s, (int)strlen(s));
    e->col+=(int)strlen(s);
}

static void
emitGoto(Emit *e, int row, int col)
{
    char seq[16];

    if (e->row == row && e->col == col) {
        return;
    }
    sprintf(seq, "\x1B[%d;%dH", row, col);
    emitRaw(e, seq, (int)strlen(seq));
    e->row=row;
    e->col=col;
}

static void
emitAttr(Emit *e, int bold, int fg)
{
    char seq[16];
    int want=bold*100+fg;

    if (e->attr == want) {
        return;
    }
    if (bold) {
        sprintf(seq, "\x1B[0;1;%dm", fg);
    }
    else {
        sprintf(seq, "\x1B[0;%dm", fg);
    }
    emitRaw(e, seq, (int)strlen(seq));
    e->attr=want;
}

/* Repeat one character n times at the current cursor position.            */
static void
emitRep(Emit *e, char c, int n)
{
    char run[82];

    if (n > (int)sizeof(run)-1) {
        n=(int)sizeof(run)-1;
    }
    memset(run, c, n);
    run[n]='\0';
    emitStr(e, run);
}

/*--------------------------------------------------------------------------
 * Build the image the visible playfield should show right now: the settled
 * stack, plus the flash animation, the ghost landing spot, and the falling
 * piece.  While paused the field is shown blank so a player cannot stop
 * the clock to study the board.
 *--------------------------------------------------------------------------*/
static void
buildImage(const BlxGame *g, const BlxUiOpts *opts,
           unsigned char img[BLX_VISH][BLX_W])
{
    int xs[BLX_NCELL], ys[BLX_NCELL];
    int i, x, y, gy;

    if (g->phase == BLX_PAUSED) {
        memset(img, CC_EMPTY, BLX_VISH*BLX_W);
        return;
    }

    for (y=0 ; y < BLX_VISH ; y++) {
        memcpy(img[y], g->board[y+BLX_HIDDEN], BLX_W);
    }

    if (g->phase == BLX_CLEARING) {
        /* alternate the completed rows between white and blank             */
        for (i=0 ; i < g->nClearRows ; i++) {
            y=(int)g->clearRows[i]-BLX_HIDDEN;
            if (y >= 0) {
                memset(img[y], (g->clearTicks&1) ? CC_FLASH : CC_EMPTY,
                       BLX_W);
            }
        }
        return;
    }

    if (g->phase != BLX_PLAYING) {
        return;                    /* game over: just the settled stack     */
    }

    blxPieceCells(g->curPiece, g->curRot, xs, ys);

    if (opts->showGhost) {
        gy=blxGhostY(g);
        if (gy > g->curY) {
            for (i=0 ; i < BLX_NCELL ; i++) {
                y=gy+ys[i]-BLX_HIDDEN;
                x=g->curX+xs[i];
                if (y >= 0) {
                    img[y][x]=(unsigned char)(CC_GHOST0+g->curPiece);
                }
            }
        }
    }
    for (i=0 ; i < BLX_NCELL ; i++) {
        y=g->curY+ys[i]-BLX_HIDDEN;
        x=g->curX+xs[i];
        if (y >= 0) {
            img[y][x]=(unsigned char)(g->curPiece+1);
        }
    }
}

/* Draw one playfield cell (two characters wide) in its proper color.      */
static void
emitCell(Emit *e, int scrRow, int scrCol, int code)
{
    char two[3];

    emitGoto(e, scrRow, scrCol);
    if (code == CC_EMPTY) {
        emitStr(e, "  ");
        return;
    }
    if (code == CC_FLASH) {
        emitAttr(e, 1, 37);
        two[0]=two[1]=CH_SOLID;
    }
    else if (code >= CC_GHOST0) {
        emitAttr(e, 0, cellFg[code-CC_GHOST0+1]);
        two[0]=two[1]=CH_GHOST;
    }
    else {
        emitAttr(e, cellBold[code], cellFg[code]);
        two[0]=two[1]=CH_SOLID;
    }
    two[2]='\0';
    emitStr(e, two);
}

/*--------------------------------------------------------------------------
 * Right-panel numbers.  Each is redrawn only when its value changes.
 *--------------------------------------------------------------------------*/
static void
emitNumber(Emit *e, int row, long value)
{
    char txt[16];

    sprintf(txt, "%10ld", value);
    emitGoto(e, row, RPAN_COL);
    emitAttr(e, 1, 37);
    emitStr(e, txt);
}

static void
emitTime(Emit *e, int row, long seconds)
{
    char txt[16];

    if (seconds > 5999L) {
        seconds=5999L;             /* display caps at 99:59                 */
    }
    sprintf(txt, "     %02ld:%02ld", seconds/60L, seconds%60L);
    emitGoto(e, row, RPAN_COL);
    emitAttr(e, 1, 37);
    emitStr(e, txt);
}

/* Rows used by the right-hand panel.                                       */
#define ROW_SCORELBL  3
#define ROW_SCORE     4
#define ROW_LEVELLBL  6
#define ROW_LEVEL     7
#define ROW_LINESLBL  9
#define ROW_LINES    10
#define ROW_TIMELBL  12
#define ROW_TIME     13
#define ROW_NEXTLBL  15
#define ROW_NEXT     16            /* four rows: 16..19                     */
#define ROW_BESTLBL  21
#define ROW_BEST     22

#define ROW_NAME      3            /* left panel                            */
#define ROW_CTLLBL    6
#define ROW_COMBO    19

/*--------------------------------------------------------------------------
 * The Next preview: the upcoming shape drawn in a 4x2-cell box under the
 * NEXT label.  Redrawn only when the upcoming piece changes.
 *--------------------------------------------------------------------------*/
static void
emitNext(Emit *e, int piece, const BlxUiOpts *opts)
{
    int xs[BLX_NCELL], ys[BLX_NCELL];
    unsigned char box[2][4];
    char two[3];
    int i, x, y;

    if (!opts->showNext) {
        return;
    }
    memset(box, 0, sizeof(box));
    blxPieceCells(piece, 0, xs, ys);
    for (i=0 ; i < BLX_NCELL ; i++) {
        if (ys[i] < 2) {           /* spawn orientations fit in two rows    */
            box[ys[i]][xs[i]]=1;
        }
    }
    two[2]='\0';
    for (y=0 ; y < 2 ; y++) {
        emitGoto(e, ROW_NEXT+y, RPAN_COL);
        for (x=0 ; x < 4 ; x++) {
            if (box[y][x]) {
                emitAttr(e, cellBold[piece+1], cellFg[piece+1]);
                two[0]=two[1]=CH_SOLID;
                emitStr(e, two);
            }
            else {
                emitStr(e, "  ");
            }
        }
    }
}

/*--------------------------------------------------------------------------
 * Combo indicator on the left panel: appears at a two-clear run, grows
 * with the run, disappears when the run ends.
 *--------------------------------------------------------------------------*/
static void
emitCombo(Emit *e, int comboRun)
{
    char txt[26];

    emitGoto(e, ROW_COMBO, LPAN_COL);
    if (comboRun >= 2) {
        emitAttr(e, 1, 33);
        sprintf(txt, "COMBO x%-2d !", comboRun-1);
        emitStr(e, txt);
    }
    else {
        emitAttr(e, 0, 37);
        emitRep(e, ' ', 12);
    }
}

/*--------------------------------------------------------------------------
 * Pausing blanks the playfield (so the clock cannot be stopped to study
 * the board) and centers a PAUSED banner over it.  Resuming invalidates
 * the banner row so the normal diff pass repaints it.
 *--------------------------------------------------------------------------*/
#define BANNER_ROW 9               /* playfield row the banner sits on      */

static void
emitPauseScreen(Emit *e, BlxShadow *sh)
{
    int y;

    for (y=0 ; y < BLX_VISH ; y++) {
        emitGoto(e, FIELD_ROW+y, FIELD_COL);
        emitRep(e, ' ', BLX_W*2);
        memset(sh->cells[y], CC_EMPTY, BLX_W);
    }
    emitGoto(e, FIELD_ROW+BANNER_ROW, FIELD_COL+4);
    emitAttr(e, 1, 37);
    emitStr(e, " P A U S E D ");
}

/*--------------------------------------------------------------------------
 * Public: incremental frame.
 *--------------------------------------------------------------------------*/
int
blxDrawDelta(char *out, const BlxGame *g, BlxShadow *sh,
             long seconds, const BlxUiOpts *opts)
{
    unsigned char img[BLX_VISH][BLX_W];
    Emit e;
    int x, y;

    e.buf=out;
    e.len=0;
    e.row=0;                       /* cursor position unknown               */
    e.col=0;
    e.attr=0;                      /* color state unknown                   */

    /* pause is handled apart from the normal diff: entering it blanks     */
    /* the field and shows the banner, and nothing else can change until   */
    /* the player resumes                                                  */
    if (g->phase == BLX_PAUSED) {
        if (!sh->pauseShown) {
            emitPauseScreen(&e, sh);
            sh->pauseShown=1;
        }
        out[e.len]='\0';
        return(e.len);
    }
    if (sh->pauseShown) {
        /* just resumed: the banner row must repaint even where the image  */
        /* agrees with the blanked shadow                                  */
        memset(sh->cells[BANNER_ROW], CC_INVALID, BLX_W);
        sh->pauseShown=0;
    }

    buildImage(g, opts, img);
    for (y=0 ; y < BLX_VISH ; y++) {
        for (x=0 ; x < BLX_W ; x++) {
            if (img[y][x] != sh->cells[y][x]) {
                emitCell(&e, FIELD_ROW+y, FIELD_COL+x*2, img[y][x]);
                sh->cells[y][x]=img[y][x];
            }
        }
    }

    if (g->score != sh->score) {
        emitNumber(&e, ROW_SCORE, g->score);
        sh->score=g->score;
    }
    if (g->level != sh->level) {
        emitNumber(&e, ROW_LEVEL, (long)g->level);
        sh->level=g->level;
    }
    if (g->lines != sh->lines) {
        emitNumber(&e, ROW_LINES, (long)g->lines);
        sh->lines=g->lines;
    }
    if (seconds != sh->seconds && g->phase != BLX_PAUSED) {
        emitTime(&e, ROW_TIME, seconds);
        sh->seconds=seconds;
    }
    if (g->nextPiece != sh->nextPiece) {
        emitNext(&e, g->nextPiece, opts);
        sh->nextPiece=g->nextPiece;
    }
    if (g->comboRun != sh->comboShown) {
        emitCombo(&e, g->comboRun);
        sh->comboShown=g->comboRun;
    }

    out[e.len]='\0';
    return(e.len);
}

/*--------------------------------------------------------------------------
 * Public: complete redraw.
 *--------------------------------------------------------------------------*/
int
blxDrawFull(char *out, const BlxGame *g, BlxShadow *sh,
            const char *playerName, long highScore, long seconds,
            const BlxUiOpts *opts)
{
    Emit e;
    char name[BLXA_NAMELEN+1];
    int y, n;

    e.buf=out;
    e.len=0;
    e.row=0;
    e.col=0;
    e.attr=0;

    /* wipe the terminal and home the cursor                                */
    emitRaw(&e, "\x1B[0m\x1B[2J\x1B[H", 11);
    e.attr=37;                     /* after reset: normal white             */
    e.row=1;
    e.col=1;

    /* title bar                                                            */
    emitGoto(&e, 1, 33);
    emitAttr(&e, 1, 37);
    emitStr(&e, "B  L  O  X");

    /* playfield frame                                                      */
    emitAttr(&e, 1, 34);
    emitGoto(&e, FRAME_TOP, FRAME_LEFT);
    emitRep(&e, CH_TL, 1);
    emitRep(&e, CH_H, BLX_W*2);
    emitRep(&e, CH_TR, 1);
    for (y=0 ; y < BLX_VISH ; y++) {
        emitGoto(&e, FIELD_ROW+y, FRAME_LEFT);
        emitRep(&e, CH_V, 1);
        emitGoto(&e, FIELD_ROW+y, FRAME_RIGHT);
        emitRep(&e, CH_V, 1);
    }
    emitGoto(&e, FRAME_BOT, FRAME_LEFT);
    emitRep(&e, CH_BL, 1);
    emitRep(&e, CH_H, BLX_W*2);
    emitRep(&e, CH_BR, 1);

    /* left panel: player name and controls                                 */
    n=(int)strlen(playerName);
    if (n > BLXA_NAMELEN) {
        n=BLXA_NAMELEN;
    }
    memcpy(name, playerName, n);
    name[n]='\0';
    emitGoto(&e, ROW_NAME, LPAN_COL);
    emitAttr(&e, 1, 32);
    emitStr(&e, name);

    emitAttr(&e, 0, 36);
    emitGoto(&e, ROW_CTLLBL,   LPAN_COL);
    emitStr(&e, "CONTROLS");
    emitAttr(&e, 0, 37);
    emitGoto(&e, ROW_CTLLBL+2, LPAN_COL);
    emitStr(&e, "Left/Right  move");
    emitGoto(&e, ROW_CTLLBL+3, LPAN_COL);
    emitStr(&e, "Up          rotate");
    emitGoto(&e, ROW_CTLLBL+4, LPAN_COL);
    emitStr(&e, "Down        soft drop");
    emitGoto(&e, ROW_CTLLBL+5, LPAN_COL);
    emitStr(&e, "Space       hard drop");
    emitGoto(&e, ROW_CTLLBL+6, LPAN_COL);
    emitStr(&e, "P           pause");
    emitGoto(&e, ROW_CTLLBL+7, LPAN_COL);
    emitStr(&e, "Q           quit");

    /* right panel labels                                                   */
    emitAttr(&e, 0, 36);
    emitGoto(&e, ROW_SCORELBL, RPAN_COL);
    emitStr(&e, "SCORE");
    emitGoto(&e, ROW_LEVELLBL, RPAN_COL);
    emitStr(&e, "LEVEL");
    emitGoto(&e, ROW_LINESLBL, RPAN_COL);
    emitStr(&e, "LINES");
    emitGoto(&e, ROW_TIMELBL,  RPAN_COL);
    emitStr(&e, "TIME");
    if (opts->showNext) {
        emitGoto(&e, ROW_NEXTLBL, RPAN_COL);
        emitStr(&e, "NEXT");
    }
    emitGoto(&e, ROW_BESTLBL,  RPAN_COL);
    emitStr(&e, "BEST");
    emitNumber(&e, ROW_BEST, highScore);

    /* reset the shadow so the delta pass repaints every cell and number    */
    memset(sh, 0, sizeof(*sh));
    memset(sh->cells, CC_INVALID, sizeof(sh->cells));
    sh->score=-1;
    sh->level=-1;
    sh->lines=-1;
    sh->seconds=-1;
    sh->nextPiece=-1;
    sh->comboShown=-1;
    sh->highScore=highScore;

    out[e.len]='\0';
    n=e.len;
    n+=blxDrawDelta(out+n, g, sh, seconds, opts);
    return(n);
}

/*--------------------------------------------------------------------------
 * Public: sound cues.  At most two bells per frame so a hot streak never
 * turns into a siren.
 *--------------------------------------------------------------------------*/
int
blxDrawBells(char *out, unsigned events, const BlxUiOpts *opts)
{
    int n=0;

    if (opts->sounds) {
        if (events&(BLXEV_TETRIS|BLXEV_PERFECT)) {
            out[n++]='\a';
            out[n++]='\a';
        }
        else if (events&(BLXEV_CLEAR|BLXEV_LEVELUP|BLXEV_GAMEOVER)) {
            out[n++]='\a';
        }
    }
    out[n]='\0';
    return(n);
}

/*--------------------------------------------------------------------------
 * Public: leave the game screen tidily.
 *--------------------------------------------------------------------------*/
int
blxDrawExit(char *out)
{
    strcpy(out, "\x1B[0m\x1B[24;1H\r\n");
    return((int)strlen(out));
}
