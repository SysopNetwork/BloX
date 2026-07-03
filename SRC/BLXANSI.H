/*****************************************************************************
 *   BLXANSI.H                                                 BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   ANSI renderer for the BloX game screen.                                *
 *                                                                           *
 *   Like the game engine, this file has no BBS dependencies: it writes     *
 *   ANSI text into a caller-supplied buffer, and the BBS layer decides     *
 *   how to transmit it.  That keeps it testable on a plain workstation.    *
 *                                                                           *
 *   The renderer is built around differential updates.  A BlxShadow        *
 *   structure remembers exactly what was last drawn on the user's          *
 *   terminal; each frame the renderer computes what the screen should      *
 *   look like now and emits ANSI only for the cells and numbers that       *
 *   changed.  A quiet frame emits nothing at all.  This is what keeps      *
 *   the game smooth and flicker-free over slow telnet links.               *
 *****************************************************************************/

#ifndef BLXANSI_H
#define BLXANSI_H

#include "BLXCORE.H"

/* A full redraw of the game screen fits comfortably in this many bytes;
 * incremental frames are usually well under a hundred.                    */
#define BLXA_BUFSIZ  8192

/* Longest player name the side panel will show.                           */
#define BLXA_NAMELEN 25

/*--------------------------------------------------------------------------
 * Display options, resolved by the BBS layer from Sysop configuration.
 *--------------------------------------------------------------------------*/
struct BlxUiOpts {
    int showGhost;                 /* draw the landing-spot ghost piece?    */
    int showNext;                  /* draw the Next preview box?            */
    int sounds;                    /* append terminal bells on events?      */
};

/*--------------------------------------------------------------------------
 * Everything the renderer remembers about one user's terminal.
 * One of these lives alongside each channel's BlxGame.
 *--------------------------------------------------------------------------*/
struct BlxShadow {
    unsigned char cells[BLX_VISH][BLX_W]; /* cell codes as last drawn       */
    long score;                    /* numbers as last drawn                 */
    int  level;
    int  lines;
    long seconds;
    int  nextPiece;
    long highScore;
    int  comboShown;               /* combo run as last drawn (0 = none)    */
    int  pauseShown;               /* PAUSED banner currently on screen?    */
};

/*--------------------------------------------------------------------------
 * Renderer entry points.  Each writes NUL-terminated ANSI text into out[]
 * (at most BLXA_BUFSIZ bytes including the NUL) and returns the number of
 * text bytes written, 0 when nothing changed.
 *--------------------------------------------------------------------------*/

/* Draw the complete game screen from scratch: clears the terminal, draws
 * the frame, panels, and every cell, and resets the shadow to match.      */
int blxDrawFull(char *out, const BlxGame *g, BlxShadow *sh,
                const char *playerName, long highScore, long seconds,
                const BlxUiOpts *opts);

/* Draw only what changed since the previous call.                         */
int blxDrawDelta(char *out, const BlxGame *g, BlxShadow *sh,
                 long seconds, const BlxUiOpts *opts);

/* Sound cues for the frame's events, as terminal bells (nothing when
 * sounds are disabled).  Appended by the caller after a draw call.        */
int blxDrawBells(char *out, unsigned events, const BlxUiOpts *opts);

/* Move the cursor to the bottom line and restore normal attributes; used
 * when leaving the game screen so the BBS prompt appears sanely.          */
int blxDrawExit(char *out);

#endif /* BLXANSI_H */
