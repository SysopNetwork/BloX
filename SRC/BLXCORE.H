/*****************************************************************************
 *   BLXCORE.H                                                 BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   Core falling-block game engine.                                        *
 *                                                                           *
 *   This file is deliberately free of any BBS or operating system          *
 *   dependencies.  Everything here is pure game logic: the playfield,      *
 *   the seven block shapes, movement, rotation, gravity, line clearing,    *
 *   scoring, and level progression.  Because it has no outside             *
 *   dependencies it can be compiled into a small console harness and       *
 *   tested on its own (see TESTS/).                                        *
 *                                                                           *
 *   The BBS-facing code (SNBLOX.CPP) owns one BlxGame per channel and      *
 *   drives it from keystrokes and timer ticks.  The renderer               *
 *   (BLXANSI.CPP) reads the state here and draws it.                       *
 *****************************************************************************/

#ifndef BLXCORE_H
#define BLXCORE_H

/*--------------------------------------------------------------------------
 * Playfield dimensions.
 *
 * The visible playfield is 10 columns by 20 rows.  Two extra hidden rows
 * sit above the visible area so a new piece can enter the field gradually
 * instead of appearing all at once.
 *--------------------------------------------------------------------------*/
#define BLX_W        10            /* playfield width in cells              */
#define BLX_VISH     20            /* visible playfield height              */
#define BLX_HIDDEN   2             /* hidden spawn rows above the top       */
#define BLX_H        (BLX_VISH+BLX_HIDDEN)   /* total rows in the grid      */

#define BLX_NPIECES  7             /* the seven classic block shapes        */
#define BLX_NROT     4             /* rotations per shape                   */
#define BLX_NCELL    4             /* every shape covers four cells         */

#define BLX_MAXLEVEL 30            /* level number is capped here           */

/* How many timer ticks the line-clear flash animation lasts.  Ticks arrive
 * at roughly 18.2 per second, so 6 ticks is about a third of a second.     */
#define BLX_CLRTICKS 6

/*--------------------------------------------------------------------------
 * Piece identifiers.  The value stored in a playfield cell is 0 for empty
 * or (piece + 1) so the renderer can color each cell by the piece that
 * produced it.
 *--------------------------------------------------------------------------*/
enum BlxPiece {
    BLX_I = 0,
    BLX_O = 1,
    BLX_T = 2,
    BLX_L = 3,
    BLX_J = 4,
    BLX_S = 5,
    BLX_Z = 6
};

/*--------------------------------------------------------------------------
 * Overall game phase.
 *--------------------------------------------------------------------------*/
enum BlxPhase {
    BLX_PLAYING  = 0,              /* a piece is falling                    */
    BLX_CLEARING = 1,              /* full rows are flashing before removal */
    BLX_PAUSED   = 2,              /* frozen by the player                  */
    BLX_GAMEOVER = 3               /* stack reached the top                 */
};

/*--------------------------------------------------------------------------
 * Event flags.  Game actions set bits here; the caller reads and clears
 * them once per frame to trigger sounds, celebrations, and so on.
 *--------------------------------------------------------------------------*/
#define BLXEV_LOCK     0x0001      /* a piece locked into the stack         */
#define BLXEV_CLEAR    0x0002      /* one or more lines completed           */
#define BLXEV_TETRIS   0x0004      /* four lines completed at once          */
#define BLXEV_LEVELUP  0x0008      /* the level just increased              */
#define BLXEV_GAMEOVER 0x0010      /* the game just ended                   */
#define BLXEV_PERFECT  0x0020      /* the playfield was completely emptied  */
#define BLXEV_B2B      0x0040      /* back-to-back four-line bonus applied  */

/*--------------------------------------------------------------------------
 * The complete state of one game in progress.
 *
 * This is a plain data structure with no pointers, so the BBS layer can
 * keep an array of them (one per channel) and wipe or copy them freely.
 *--------------------------------------------------------------------------*/
struct BlxGame {
    /* --- playfield ----------------------------------------------------- */
    unsigned char board[BLX_H][BLX_W];  /* 0 = empty, else piece + 1        */

    /* --- falling piece ------------------------------------------------- */
    int  curPiece;                 /* which shape is falling (BlxPiece)     */
    int  curRot;                   /* current rotation, 0..3                */
    int  curX;                     /* x of the shape's 4x4 box, can be <0   */
    int  curY;                     /* y of the shape's 4x4 box              */
    int  nextPiece;                /* shape shown in the Next preview       */

    /* --- 7-bag randomizer ---------------------------------------------- */
    unsigned char bag[BLX_NPIECES];/* shuffled bag of upcoming shapes       */
    int  bagIdx;                   /* next unused entry in the bag          */
    unsigned long rngState;        /* private random-number state           */

    /* --- pace ---------------------------------------------------------- */
    int  phase;                    /* BlxPhase                              */
    int  gravTicks;                /* ticks remaining until gravity pulls   */
    int  clearTicks;               /* animation ticks left in BLX_CLEARING  */
    unsigned char clearRows[4];    /* rows being cleared (indexes in board) */
    int  nClearRows;               /* how many rows are being cleared       */
    int  pausedPhase;              /* phase to resume into after pause      */

    /* --- score and progress -------------------------------------------- */
    long score;                    /* current score                         */
    int  lines;                    /* total lines cleared this game         */
    int  level;                    /* current level                         */
    int  startLevel;               /* level the game began at               */
    int  comboRun;                 /* consecutive locks that cleared lines  */
    int  lastWasTetris;            /* was the previous clear a 4-liner?     */
    long playTicks;                /* timer ticks spent actually playing    */

    /* --- statistics for the player database ----------------------------- */
    long piecesDropped;            /* total pieces locked this game         */

    /* --- frame events --------------------------------------------------- */
    unsigned events;               /* BLXEV_* bits since last read          */
    int  lastClearCount;           /* lines in the most recent clear (1..4) */
};

/*--------------------------------------------------------------------------
 * Engine entry points.  All functions are free of side effects outside
 * the BlxGame structure passed to them.
 *--------------------------------------------------------------------------*/

/* Start a fresh game.  The seed feeds the piece randomizer; pass something
 * different per game (for example the clock combined with the channel).   */
void blxStart(BlxGame *g, int startLevel, unsigned long seed);

/* Advance the game by one timer tick (about 1/18 second).  Handles
 * gravity and the line-clear animation.  Safe to call in any phase.       */
void blxTick(BlxGame *g);

/* Player actions.  Each returns 1 if the action changed anything visible
 * (so the caller knows a redraw is worthwhile) and 0 otherwise.           */
int  blxMoveLeft(BlxGame *g);
int  blxMoveRight(BlxGame *g);
int  blxRotate(BlxGame *g);
int  blxSoftDrop(BlxGame *g);      /* one row down, awards 1 point per row  */
int  blxHardDrop(BlxGame *g);      /* straight to the floor, 2 points/row   */
int  blxTogglePause(BlxGame *g);

/* Where the falling piece would land if hard-dropped right now.  Used to
 * draw the ghost piece.  Returns the y of the shape box at rest.          */
int  blxGhostY(const BlxGame *g);

/* The four cells of a shape in a given rotation.  Fills xs[4]/ys[4] with
 * offsets inside the shape's 4x4 box.                                     */
void blxPieceCells(int piece, int rot, int xs[4], int ys[4]);

/* How many ticks between gravity pulls at a given level.                  */
int  blxTicksPerRow(int level);

#endif /* BLXCORE_H */
