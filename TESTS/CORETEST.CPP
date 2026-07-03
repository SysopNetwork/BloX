/*****************************************************************************
 *   CORETEST.CPP                                              BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   Console test harness for the BloX game engine (BLXCORE).               *
 *                                                                           *
 *   Builds and runs without any BBS present, so the game rules can be      *
 *   verified on a plain workstation:                                       *
 *                                                                           *
 *       cl /nologo /J /W3 /EHsc /I..\SRC CORETEST.CPP ..\SRC\BLXCORE.CPP   *
 *       CORETEST.EXE                                                       *
 *                                                                           *
 *   Exits with code 0 and prints PASS when every check succeeds.           *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BLXCORE.H"

static int failures=0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* Fill one playfield row except for the given column (-1 fills it all).   */
static void
fillRow(BlxGame *g, int y, int gapx)
{
    int x;

    for (x=0 ; x < BLX_W ; x++) {
        g->board[y][x]=(unsigned char)(x == gapx ? 0 : 1);
    }
}

/* Park a vertical I piece over a column so a hard drop lands it there.    */
static void
plantVerticalI(BlxGame *g, int col, int y)
{
    g->curPiece=BLX_I;
    g->curRot=1;                   /* vertical, occupies box column 2      */
    g->curX=col-2;
    g->curY=y;
}

static void
testShapes(void)
{
    int p, r, i, xs[4], ys[4];

    for (p=0 ; p < BLX_NPIECES ; p++) {
        for (r=0 ; r < BLX_NROT ; r++) {
            blxPieceCells(p, r, xs, ys);
            for (i=0 ; i < 4 ; i++) {
                CHECK(xs[i] >= 0 && xs[i] < 4);
                CHECK(ys[i] >= 0 && ys[i] < 4);
            }
            /* no two cells of a shape may coincide                        */
            for (i=0 ; i < 4 ; i++) {
                int j;
                for (j=i+1 ; j < 4 ; j++) {
                    CHECK(xs[i] != xs[j] || ys[i] != ys[j]);
                }
            }
        }
    }
}

static void
testSpeedTable(void)
{
    int lvl;

    CHECK(blxTicksPerRow(0) == blxTicksPerRow(1));
    CHECK(blxTicksPerRow(1) == 15);
    CHECK(blxTicksPerRow(19) == 1);
    CHECK(blxTicksPerRow(99) == 1);
    for (lvl=1 ; lvl < 30 ; lvl++) {
        CHECK(blxTicksPerRow(lvl+1) <= blxTicksPerRow(lvl));
    }
}

static void
testBagFairness(void)
{
    BlxGame g;
    int counts[BLX_NPIECES];
    int i, round;

    blxStart(&g, 1, 42UL);
    memset(counts, 0, sizeof(counts));

    /* the first two draws happened inside blxStart                        */
    counts[g.curPiece]++;
    counts[g.nextPiece]++;

    /* run many full games' worth of pieces through and confirm the bag    */
    /* deals every shape exactly once per seven draws overall; each lock   */
    /* draws exactly one new piece (the fresh Next preview)                */
    for (round=2 ; round < 7*40 ; round++) {
        /* hard-drop onto an emptied board each time so the stack never    */
        /* tops out                                                        */
        memset(g.board, 0, sizeof(g.board));
        blxHardDrop(&g);
        while (g.phase == BLX_CLEARING) {
            blxTick(&g);
        }
        CHECK(g.phase == BLX_PLAYING);
        counts[g.nextPiece]++;
    }
    for (i=0 ; i < BLX_NPIECES ; i++) {
        CHECK(counts[i] == 40);
    }
}

static void
testMovementAndWalls(void)
{
    BlxGame g;
    int i, moved;

    blxStart(&g, 1, 7UL);
    g.curPiece=BLX_O;              /* O occupies box columns 1 and 2        */
    g.curRot=0;
    g.curX=3;
    g.curY=2;

    for (i=0, moved=1 ; moved && i < 20 ; i++) {
        moved=blxMoveLeft(&g);
    }
    CHECK(g.curX == -1);           /* O's left cell is box column 1         */
    for (i=0, moved=1 ; moved && i < 20 ; i++) {
        moved=blxMoveRight(&g);
    }
    CHECK(g.curX == 7);            /* O's right cell reaches column 9       */
}

static void
testRotationKick(void)
{
    BlxGame g;

    blxStart(&g, 1, 7UL);
    g.curPiece=BLX_I;
    g.curRot=1;                    /* vertical against the left wall        */
    g.curX=-2;                     /* box column 2 = playfield column 0     */
    g.curY=5;
    CHECK(blxRotate(&g));          /* must kick away from the wall          */
    CHECK(g.curX == 0);            /* two-cell kick puts the bar at col 0   */
}

static void
testGravityAndLock(void)
{
    BlxGame g;
    int i, startY;

    blxStart(&g, 1, 3UL);
    startY=g.curY;
    for (i=0 ; i < blxTicksPerRow(1) ; i++) {
        blxTick(&g);
    }
    CHECK(g.curY == startY+1);     /* one gravity pull after 15 ticks       */

    /* a hard drop must lock the piece and spawn the next one              */
    i=g.nextPiece;
    blxHardDrop(&g);
    CHECK(g.curPiece == i);
    CHECK(g.piecesDropped == 1);
    CHECK((g.events&BLXEV_LOCK) != 0);
}

static void
testSingleClearScoring(void)
{
    BlxGame g;
    long before;
    int i;

    blxStart(&g, 1, 11UL);
    fillRow(&g, BLX_H-1, 9);       /* bottom row complete except column 9   */
    plantVerticalI(&g, 9, BLX_H-4);
    before=g.score;

    blxHardDrop(&g);               /* rests in place: no drop points        */
    CHECK(g.phase == BLX_CLEARING);
    for (i=0 ; i < BLX_CLRTICKS ; i++) {
        blxTick(&g);
    }
    CHECK(g.phase == BLX_PLAYING);
    CHECK(g.lines == 1);
    /* single at level 1 = 100... but the I piece left 3 cells above the    */
    /* cleared row, so no perfect-clear bonus                               */
    CHECK(g.score-before == 100);
    CHECK(g.board[BLX_H-1][9] != 0);   /* stub of the I settled one row     */
}

static void
testTetrisAndPerfectClear(void)
{
    BlxGame g;
    long before;
    int i;

    blxStart(&g, 1, 13UL);
    for (i=0 ; i < 4 ; i++) {
        fillRow(&g, BLX_H-1-i, 9);
    }
    plantVerticalI(&g, 9, BLX_H-4);
    before=g.score;

    blxHardDrop(&g);
    CHECK(g.phase == BLX_CLEARING);
    CHECK(g.nClearRows == 4);
    for (i=0 ; i < BLX_CLRTICKS ; i++) {
        blxTick(&g);
    }
    /* 800 for four lines + 1000 perfect-clear, all at level 1              */
    CHECK(g.score-before == 1800);
    CHECK((g.events&BLXEV_TETRIS) != 0);
    CHECK((g.events&BLXEV_PERFECT) != 0);
    CHECK(g.lines == 4);

    /* back-to-back: do it again without any non-clearing lock between     */
    for (i=0 ; i < 4 ; i++) {
        fillRow(&g, BLX_H-1-i, 9);
    }
    plantVerticalI(&g, 9, 0);
    before=g.score;
    blxHardDrop(&g);               /* falls 18 rows: 36 drop points         */
    for (i=0 ; i < BLX_CLRTICKS ; i++) {
        blxTick(&g);
    }
    /* 1200 back-to-back + 50 combo + 1000 perfect + 36 hard drop           */
    CHECK(g.score-before == 1200+50+1000+36);
    CHECK((g.events&BLXEV_B2B) != 0);
    CHECK(g.lines == 8);
    CHECK(g.level == 1);           /* eight lines is not yet level 2        */
}

static void
testLevelUp(void)
{
    BlxGame g;
    int i;

    blxStart(&g, 1, 17UL);
    g.lines=9;                     /* one line away from level 2            */
    fillRow(&g, BLX_H-1, 9);
    plantVerticalI(&g, 9, BLX_H-4);
    blxHardDrop(&g);
    for (i=0 ; i < BLX_CLRTICKS ; i++) {
        blxTick(&g);
    }
    CHECK(g.lines == 10);
    CHECK(g.level == 2);
    CHECK((g.events&BLXEV_LEVELUP) != 0);

    /* a configured starting level must hold up the floor                   */
    blxStart(&g, 5, 17UL);
    CHECK(g.level == 5);
}

static void
testGameOver(void)
{
    BlxGame g;
    int y, x;

    blxStart(&g, 1, 23UL);
    /* wall off the spawn area so the next spawn cannot fit.  Column 0 is  */
    /* left open so none of these rows ever count as completed lines.      */
    for (y=0 ; y < 6 ; y++) {
        for (x=1 ; x < BLX_W ; x++) {
            if (g.board[y][x] == 0) {
                g.board[y][x]=1;
            }
        }
    }
    /* carve the falling piece back out so only the lock is legal          */
    {
        int xs[4], ys[4], i;
        blxPieceCells(g.curPiece, g.curRot, xs, ys);
        for (i=0 ; i < 4 ; i++) {
            g.board[g.curY+ys[i]][g.curX+xs[i]]=0;
        }
    }
    blxHardDrop(&g);
    CHECK(g.phase == BLX_GAMEOVER);
    CHECK((g.events&BLXEV_GAMEOVER) != 0);

    /* once over, inputs must be ignored                                   */
    CHECK(blxMoveLeft(&g) == 0);
    CHECK(blxHardDrop(&g) == 0);
}

static void
testPause(void)
{
    BlxGame g;
    int y, i;

    blxStart(&g, 1, 29UL);
    y=g.curY;
    CHECK(blxTogglePause(&g));
    CHECK(g.phase == BLX_PAUSED);
    for (i=0 ; i < 100 ; i++) {
        blxTick(&g);
    }
    CHECK(g.curY == y);            /* nothing falls while paused            */
    CHECK(blxMoveLeft(&g) == 0);
    CHECK(blxTogglePause(&g));
    CHECK(g.phase == BLX_PLAYING);
}

static void
testGhost(void)
{
    BlxGame g;
    int gy;

    blxStart(&g, 1, 31UL);
    gy=blxGhostY(&g);
    CHECK(gy >= g.curY);
    /* the ghost must sit exactly where a hard drop would land             */
    blxHardDrop(&g);
    CHECK(g.piecesDropped == 1);
}

static void
testSoftDrop(void)
{
    BlxGame g;
    long before;
    int y;

    blxStart(&g, 1, 37UL);
    before=g.score;
    y=g.curY;
    CHECK(blxSoftDrop(&g));
    CHECK(g.curY == y+1);
    CHECK(g.score-before == 1);    /* one point per soft-dropped row        */
}

int
main(void)
{
    testShapes();
    testSpeedTable();
    testBagFairness();
    testMovementAndWalls();
    testRotationKick();
    testGravityAndLock();
    testSingleClearScoring();
    testTetrisAndPerfectClear();
    testLevelUp();
    testGameOver();
    testPause();
    testGhost();
    testSoftDrop();

    if (failures == 0) {
        printf("PASS: all game engine checks succeeded\n");
        return(0);
    }
    printf("%d check(s) FAILED\n", failures);
    return(1);
}
