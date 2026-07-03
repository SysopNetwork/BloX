/*****************************************************************************
 *   ANSITEST.CPP                                              BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   Console test harness for the BloX ANSI renderer (BLXANSI).             *
 *                                                                           *
 *       cl /nologo /J /W3 /EHsc /I..\SRC ANSITEST.CPP ..\SRC\BLXCORE.CPP   *
 *          ..\SRC\BLXANSI.CPP                                              *
 *       ANSITEST.EXE                                                       *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BLXCORE.H"
#include "BLXANSI.H"

static int failures=0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* Scan emitted text for every cursor-position sequence ESC[row;colH and
 * confirm nothing is ever addressed outside the 80 x 24 screen.           */
static void
checkBounds(const char *out, int len)
{
    int i, row, col;

    for (i=0 ; i+1 < len ; i++) {
        if (out[i] == '\x1B' && out[i+1] == '[') {
            if (sscanf(out+i+2, "%d;%dH", &row, &col) == 2
             && strchr(out+i+2, 'H') != NULL) {
                /* only judge it if this really is a position sequence      */
                const char *p=out+i+2;
                while (*p && (*p == ';' || (*p >= '0' && *p <= '9'))) {
                    p++;
                }
                if (*p == 'H') {
                    CHECK(row >= 1 && row <= 24);
                    CHECK(col >= 1 && col <= 80);
                }
            }
        }
    }
}

int
main(void)
{
    static char out[BLXA_BUFSIZ];
    BlxGame g;
    BlxShadow sh;
    BlxUiOpts opts;
    int n, i;

    opts.showGhost=1;
    opts.showNext=1;
    opts.sounds=1;

    blxStart(&g, 1, 99UL);

    /* --- full draw ------------------------------------------------------ */
    n=blxDrawFull(out, &g, &sh, "Test Player", 123456L, 0L, &opts);
    CHECK(n > 500);                    /* a real screen was emitted         */
    CHECK(n < BLXA_BUFSIZ-1);          /* and it fit with room to spare     */
    CHECK((int)strlen(out) == n);      /* no stray NULs inside              */
    CHECK(memcmp(out, "\x1B[0m\x1B[2J\x1B[H", 11) == 0);
    CHECK(strstr(out, "SCORE") != NULL);
    CHECK(strstr(out, "Test Player") != NULL);
    checkBounds(out, n);

    /* --- a quiet frame emits nothing ------------------------------------ */
    n=blxDrawDelta(out, &g, &sh, 0L, &opts);
    CHECK(n == 0);

    /* --- a move produces a small, bounded update ------------------------ */
    if (blxMoveLeft(&g)) {
        n=blxDrawDelta(out, &g, &sh, 0L, &opts);
        CHECK(n > 0);
        CHECK(n < 600);
        checkBounds(out, n);
        /* and drawing again with no further changes is quiet              */
        n=blxDrawDelta(out, &g, &sh, 0L, &opts);
        CHECK(n == 0);
    }

    /* --- clock updates repaint only the clock --------------------------- */
    n=blxDrawDelta(out, &g, &sh, 61L, &opts);
    CHECK(n > 0);
    CHECK(strstr(out, "01:01") != NULL);
    checkBounds(out, n);

    /* --- gravity: ticking eventually redraws the falling piece ---------- */
    for (i=0, n=0 ; i < blxTicksPerRow(1) ; i++) {
        blxTick(&g);
    }
    n=blxDrawDelta(out, &g, &sh, 61L, &opts);
    CHECK(n > 0);
    checkBounds(out, n);

    /* --- pause shows the banner once, then stays quiet ------------------ */
    blxTogglePause(&g);
    n=blxDrawDelta(out, &g, &sh, 61L, &opts);
    CHECK(n > 0);
    CHECK(strstr(out, "P A U S E D") != NULL);
    checkBounds(out, n);
    n=blxDrawDelta(out, &g, &sh, 61L, &opts);
    CHECK(n == 0);

    /* --- resume repaints the field -------------------------------------- */
    blxTogglePause(&g);
    n=blxDrawDelta(out, &g, &sh, 61L, &opts);
    CHECK(n > 0);
    CHECK(strstr(out, "P A U S E D") == NULL);
    checkBounds(out, n);

    /* --- sound cues ------------------------------------------------------ */
    n=blxDrawBells(out, BLXEV_TETRIS, &opts);
    CHECK(n == 2 && out[0] == '\a' && out[1] == '\a');
    n=blxDrawBells(out, BLXEV_CLEAR, &opts);
    CHECK(n == 1);
    opts.sounds=0;
    n=blxDrawBells(out, BLXEV_TETRIS, &opts);
    CHECK(n == 0);
    opts.sounds=1;

    /* --- exit sequence restores the terminal ----------------------------- */
    n=blxDrawExit(out);
    CHECK(n > 0);
    CHECK(strstr(out, "\x1B[0m") != NULL);

    /* --- a whole simulated game stays inside the buffer ------------------ */
    blxStart(&g, 9, 1234UL);
    n=blxDrawFull(out, &g, &sh, "Marathoner", 0L, 0L, &opts);
    checkBounds(out, n);
    for (i=0 ; i < 20000 && g.phase != BLX_GAMEOVER ; i++) {
        blxTick(&g);
        if ((i%7) == 0) {
            blxMoveLeft(&g);
        }
        if ((i%11) == 0) {
            blxRotate(&g);
        }
        if ((i%13) == 0) {
            blxMoveRight(&g);
        }
        n=blxDrawDelta(out, &g, &sh, (long)(g.playTicks/18), &opts);
        CHECK(n < BLXA_BUFSIZ-1);
        checkBounds(out, n);
    }
    CHECK(g.phase == BLX_GAMEOVER); /* an unattended game must end          */

    if (failures == 0) {
        printf("PASS: all renderer checks succeeded\n");
        return(0);
    }
    printf("%d check(s) FAILED\n", failures);
    return(1);
}
