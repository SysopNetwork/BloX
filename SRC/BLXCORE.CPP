/*****************************************************************************
 *   BLXCORE.CPP                                               BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   Core falling-block game engine.  See BLXCORE.H for an overview.        *
 *****************************************************************************/

#include <string.h>
#include "BLXCORE.H"

/*--------------------------------------------------------------------------
 * Shape tables.
 *
 * Each of the seven shapes is described in a 4x4 box, one entry per
 * rotation, four (x,y) cells per entry.  x runs right, y runs down.
 * These are the familiar shapes and spawn orientations players expect:
 * every piece appears flat-side down and rotates clockwise.
 *--------------------------------------------------------------------------*/
struct ShapeCells {
    unsigned char x[BLX_NCELL];
    unsigned char y[BLX_NCELL];
};

static const ShapeCells shapes[BLX_NPIECES][BLX_NROT] = {
    /* I ---------------------------------------------------------------- */
    { {{0,1,2,3},{1,1,1,1}}, {{2,2,2,2},{0,1,2,3}},
      {{0,1,2,3},{2,2,2,2}}, {{1,1,1,1},{0,1,2,3}} },
    /* O ---------------------------------------------------------------- */
    { {{1,2,1,2},{0,0,1,1}}, {{1,2,1,2},{0,0,1,1}},
      {{1,2,1,2},{0,0,1,1}}, {{1,2,1,2},{0,0,1,1}} },
    /* T ---------------------------------------------------------------- */
    { {{1,0,1,2},{0,1,1,1}}, {{1,1,2,1},{0,1,1,2}},
      {{0,1,2,1},{1,1,1,2}}, {{1,0,1,1},{0,1,1,2}} },
    /* L ---------------------------------------------------------------- */
    { {{2,0,1,2},{0,1,1,1}}, {{1,1,1,2},{0,1,2,2}},
      {{0,1,2,0},{1,1,1,2}}, {{0,1,1,1},{0,0,1,2}} },
    /* J ---------------------------------------------------------------- */
    { {{0,0,1,2},{0,1,1,1}}, {{1,2,1,1},{0,0,1,2}},
      {{0,1,2,2},{1,1,1,2}}, {{1,1,0,1},{0,1,2,2}} },
    /* S ---------------------------------------------------------------- */
    { {{1,2,0,1},{0,0,1,1}}, {{1,1,2,2},{0,1,1,2}},
      {{1,2,0,1},{1,1,2,2}}, {{0,0,1,1},{0,1,1,2}} },
    /* Z ---------------------------------------------------------------- */
    { {{0,1,1,2},{0,0,1,1}}, {{2,1,2,1},{0,1,1,2}},
      {{0,1,1,2},{1,1,2,2}}, {{1,0,1,0},{0,1,1,2}} }
};

/*--------------------------------------------------------------------------
 * Falling speed, expressed as timer ticks (about 1/18 second each) between
 * one-row gravity pulls.  Level 1 is a relaxed 0.8 seconds per row; by
 * level 19 the piece falls one row per tick, which is classic expert pace.
 *--------------------------------------------------------------------------*/
static const unsigned char speedTable[] = {
    15,                            /* level 1                               */
    13,                            /* level 2                               */
    12,                            /* level 3                               */
    11,                            /* level 4                               */
    10,                            /* level 5                               */
     9,                            /* level 6                               */
     8,                            /* level 7                               */
     7,                            /* level 8                               */
     6,                            /* level 9                               */
     5,                            /* level 10                              */
     5,                            /* level 11                              */
     4,                            /* level 12                              */
     4,                            /* level 13                              */
     3,                            /* level 14                              */
     3,                            /* level 15                              */
     2,                            /* level 16                              */
     2,                            /* level 17                              */
     2,                            /* level 18                              */
     1                             /* level 19 and beyond                   */
};

int
blxTicksPerRow(int level)
{
    if (level < 1) {
        level=1;
    }
    if (level > (int)sizeof(speedTable)) {
        level=(int)sizeof(speedTable);
    }
    return(speedTable[level-1]);
}

void
blxPieceCells(int piece, int rot, int xs[4], int ys[4])
{
    const ShapeCells *sc=&shapes[piece][rot&3];
    int i;

    for (i=0 ; i < BLX_NCELL ; i++) {
        xs[i]=sc->x[i];
        ys[i]=sc->y[i];
    }
}

/*--------------------------------------------------------------------------
 * Random numbers.
 *
 * A small self-contained xorshift generator.  Each game carries its own
 * state so simultaneous games on different channels never disturb each
 * other, and replays with the same seed behave identically.
 *--------------------------------------------------------------------------*/
static unsigned long
nextRand(BlxGame *g)
{
    unsigned long x=g->rngState;

    x^=x << 13;
    x^=x >> 17;
    x^=x << 5;
    g->rngState=(x != 0UL ? x : 0x1234567UL);
    return(g->rngState);
}

/*--------------------------------------------------------------------------
 * The 7-bag randomizer.
 *
 * All seven shapes are shuffled together and dealt out one by one; only
 * when the bag is empty is it refilled and reshuffled.  This guarantees
 * the player never waits more than 12 pieces for any shape, which keeps
 * the game feeling fair.
 *--------------------------------------------------------------------------*/
static void
refillBag(BlxGame *g)
{
    int i, j;
    unsigned char t;

    for (i=0 ; i < BLX_NPIECES ; i++) {
        g->bag[i]=(unsigned char)i;
    }
    for (i=BLX_NPIECES-1 ; i > 0 ; i--) {  /* Fisher-Yates shuffle          */
        j=(int)(nextRand(g)%(unsigned long)(i+1));
        t=g->bag[i];
        g->bag[i]=g->bag[j];
        g->bag[j]=t;
    }
    g->bagIdx=0;
}

static int
drawFromBag(BlxGame *g)
{
    if (g->bagIdx >= BLX_NPIECES) {
        refillBag(g);
    }
    return(g->bag[g->bagIdx++]);
}

/*--------------------------------------------------------------------------
 * Collision test: would the given shape, rotation, and box position
 * overlap the walls, the floor, or settled blocks?
 *--------------------------------------------------------------------------*/
static int
collides(const BlxGame *g, int piece, int rot, int px, int py)
{
    int xs[BLX_NCELL], ys[BLX_NCELL];
    int i, cx, cy;

    blxPieceCells(piece, rot, xs, ys);
    for (i=0 ; i < BLX_NCELL ; i++) {
        cx=px+xs[i];
        cy=py+ys[i];
        if (cx < 0 || cx >= BLX_W || cy < 0 || cy >= BLX_H) {
            return(1);
        }
        if (g->board[cy][cx] != 0) {
            return(1);
        }
    }
    return(0);
}

/*--------------------------------------------------------------------------
 * Bring the next shape into play at the top of the field.  If it cannot
 * fit, the stack has reached the top and the game is over.
 *--------------------------------------------------------------------------*/
static void
spawnPiece(BlxGame *g)
{
    g->curPiece=g->nextPiece;
    g->nextPiece=drawFromBag(g);
    g->curRot=0;
    g->curX=3;                     /* centers the 4x4 box on a 10-wide field*/
    g->curY=0;
    g->gravTicks=blxTicksPerRow(g->level);

    if (collides(g, g->curPiece, g->curRot, g->curX, g->curY)) {
        g->phase=BLX_GAMEOVER;
        g->events|=BLXEV_GAMEOVER;
    }
}

/*--------------------------------------------------------------------------
 * Scoring for a completed clear, per the classic table, scaled by level:
 *
 *      1 line   100 x level          3 lines  500 x level
 *      2 lines  300 x level          4 lines  800 x level
 *
 * Bonuses:
 *   - back-to-back four-line clears score half again (1200 x level)
 *   - clearing lines with several pieces in a row earns a rising combo
 *     bonus of 50 x (run length - 1) x level
 *   - emptying the playfield completely adds 1000 x level
 *--------------------------------------------------------------------------*/
static const short clearBase[5] = { 0, 100, 300, 500, 800 };

static void
scoreClear(BlxGame *g, int n, int perfect)
{
    long pts=(long)clearBase[n]*g->level;

    if (n == 4) {
        g->events|=BLXEV_TETRIS;
        if (g->lastWasTetris) {
            pts=(pts*3)/2;         /* back-to-back four-line bonus          */
            g->events|=BLXEV_B2B;
        }
        g->lastWasTetris=1;
    }
    else {
        g->lastWasTetris=0;
    }

    if (g->comboRun >= 2) {
        pts+=50L*(g->comboRun-1)*g->level;
    }
    if (perfect) {
        pts+=1000L*g->level;
        g->events|=BLXEV_PERFECT;
    }
    g->score+=pts;
    g->lastClearCount=n;
    g->events|=BLXEV_CLEAR;
}

/*--------------------------------------------------------------------------
 * Level progression: every 10 lines advances one level (never past the
 * cap, and never below the starting level).
 *--------------------------------------------------------------------------*/
static void
updateLevel(BlxGame *g)
{
    int lvl=g->startLevel+g->lines/10;

    if (lvl > BLX_MAXLEVEL) {
        lvl=BLX_MAXLEVEL;
    }
    if (lvl > g->level) {
        g->level=lvl;
        g->events|=BLXEV_LEVELUP;
    }
}

/*--------------------------------------------------------------------------
 * Lock the falling piece into the stack, then either begin the line-clear
 * animation or spawn the next piece.
 *--------------------------------------------------------------------------*/
static void
lockPiece(BlxGame *g)
{
    int xs[BLX_NCELL], ys[BLX_NCELL];
    int i, y, x, full;

    blxPieceCells(g->curPiece, g->curRot, xs, ys);
    for (i=0 ; i < BLX_NCELL ; i++) {
        g->board[g->curY+ys[i]][g->curX+xs[i]]=(unsigned char)(g->curPiece+1);
    }
    g->piecesDropped++;
    g->events|=BLXEV_LOCK;

    /* find any rows the lock completed                                     */
    g->nClearRows=0;
    for (y=0 ; y < BLX_H ; y++) {
        full=1;
        for (x=0 ; x < BLX_W ; x++) {
            if (g->board[y][x] == 0) {
                full=0;
                break;
            }
        }
        if (full && g->nClearRows < 4) {
            g->clearRows[g->nClearRows++]=(unsigned char)y;
        }
    }

    if (g->nClearRows > 0) {
        g->comboRun++;
        g->phase=BLX_CLEARING;
        g->clearTicks=BLX_CLRTICKS;
    }
    else {
        g->comboRun=0;
        spawnPiece(g);
    }
}

/*--------------------------------------------------------------------------
 * Remove the flashed rows, settle everything above them, apply the score,
 * and put the next piece in play.
 *--------------------------------------------------------------------------*/
static void
collapseRows(BlxGame *g)
{
    int i, y, x, perfect;

    for (i=0 ; i < g->nClearRows ; i++) {
        for (y=g->clearRows[i] ; y > 0 ; y--) {
            memcpy(g->board[y], g->board[y-1], BLX_W);
        }
        memset(g->board[0], 0, BLX_W);
    }

    perfect=1;
    for (y=0 ; y < BLX_H && perfect ; y++) {
        for (x=0 ; x < BLX_W ; x++) {
            if (g->board[y][x] != 0) {
                perfect=0;
                break;
            }
        }
    }

    g->lines+=g->nClearRows;
    scoreClear(g, g->nClearRows, perfect);
    updateLevel(g);
    g->nClearRows=0;
    g->phase=BLX_PLAYING;
    spawnPiece(g);
}

/*--------------------------------------------------------------------------
 * Public entry points.
 *--------------------------------------------------------------------------*/

void
blxStart(BlxGame *g, int startLevel, unsigned long seed)
{
    memset(g, 0, sizeof(*g));
    if (startLevel < 1) {
        startLevel=1;
    }
    if (startLevel > BLX_MAXLEVEL) {
        startLevel=BLX_MAXLEVEL;
    }
    g->rngState=(seed != 0UL ? seed : 0xB10C5EEDUL);
    g->level=startLevel;
    g->startLevel=startLevel;
    g->phase=BLX_PLAYING;
    refillBag(g);
    g->nextPiece=drawFromBag(g);
    spawnPiece(g);
}

void
blxTick(BlxGame *g)
{
    switch (g->phase) {
    case BLX_PLAYING:
        g->playTicks++;
        if (--g->gravTicks <= 0) {
            g->gravTicks=blxTicksPerRow(g->level);
            if (!collides(g, g->curPiece, g->curRot, g->curX, g->curY+1)) {
                g->curY++;
            }
            else {
                lockPiece(g);
            }
        }
        break;
    case BLX_CLEARING:
        g->playTicks++;
        if (--g->clearTicks <= 0) {
            collapseRows(g);
        }
        break;
    default:                       /* paused or game over: nothing to do    */
        break;
    }
}

int
blxMoveLeft(BlxGame *g)
{
    if (g->phase != BLX_PLAYING
     || collides(g, g->curPiece, g->curRot, g->curX-1, g->curY)) {
        return(0);
    }
    g->curX--;
    return(1);
}

int
blxMoveRight(BlxGame *g)
{
    if (g->phase != BLX_PLAYING
     || collides(g, g->curPiece, g->curRot, g->curX+1, g->curY)) {
        return(0);
    }
    g->curX++;
    return(1);
}

/*
 * Rotation tries the turned shape in place first, then nudged one cell
 * right or left, then two cells (the long I piece needs the two-cell
 * nudge to turn flat when it is standing against a wall).  This simple
 * wall kick lets pieces turn beside the walls without allowing anything
 * unfair.
 */
int
blxRotate(BlxGame *g)
{
    static const int kicks[]={ 0, 1, -1, 2, -2 };
    int newRot, i;

    if (g->phase != BLX_PLAYING) {
        return(0);
    }
    newRot=(g->curRot+1)&3;
    for (i=0 ; i < (int)(sizeof(kicks)/sizeof(kicks[0])) ; i++) {
        if (!collides(g, g->curPiece, newRot, g->curX+kicks[i], g->curY)) {
            g->curRot=newRot;
            g->curX+=kicks[i];
            return(1);
        }
    }
    return(0);
}

int
blxSoftDrop(BlxGame *g)
{
    if (g->phase != BLX_PLAYING) {
        return(0);
    }
    if (!collides(g, g->curPiece, g->curRot, g->curX, g->curY+1)) {
        g->curY++;
        g->score++;                /* one point per row of assisted descent */
        g->gravTicks=blxTicksPerRow(g->level);
        return(1);
    }
    lockPiece(g);                  /* pressing down on the floor locks      */
    return(1);
}

int
blxHardDrop(BlxGame *g)
{
    int rows=0;

    if (g->phase != BLX_PLAYING) {
        return(0);
    }
    while (!collides(g, g->curPiece, g->curRot, g->curX, g->curY+1)) {
        g->curY++;
        rows++;
    }
    g->score+=2L*rows;             /* two points per row fallen             */
    lockPiece(g);
    return(1);
}

int
blxTogglePause(BlxGame *g)
{
    if (g->phase == BLX_PAUSED) {
        g->phase=g->pausedPhase;
        return(1);
    }
    if (g->phase == BLX_PLAYING || g->phase == BLX_CLEARING) {
        g->pausedPhase=g->phase;
        g->phase=BLX_PAUSED;
        return(1);
    }
    return(0);
}

int
blxGhostY(const BlxGame *g)
{
    int y=g->curY;

    while (!collides(g, g->curPiece, g->curRot, g->curX, y+1)) {
        y++;
    }
    return(y);
}
