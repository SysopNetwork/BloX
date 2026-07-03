/*****************************************************************************
 *   BLXDATA.H                                                 BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   Persistence layer: player statistics, the score log used for the       *
 *   daily / weekly / monthly leaderboards, and the all-time Hall of        *
 *   Fame.                                                                  *
 *                                                                           *
 *   Three data files, all created automatically on first startup:          *
 *                                                                           *
 *     BLXPLYR.DAT   one record per player, keyed by User-ID                *
 *     BLXSCOR.DAT   one record per qualifying finished game, keyed by      *
 *                   completion time (duplicates allowed); pruned nightly   *
 *     BLXHALL.DAT   a single record holding the all-time Top 25            *
 *                                                                           *
 *   The Hall of Fame lives in its own record rather than being computed    *
 *   from the score log, so nightly pruning of old games can never erase    *
 *   an all-time record.                                                    *
 *****************************************************************************/

#ifndef BLXDATA_H
#define BLXDATA_H

#include "gcomm.h"
#include "majorbbs.h"

#define BLX_MODNAME  "BloX"        /* fixed name stamped into records       */
#define BLX_TOPN     25            /* leaderboards show this many places    */

/* Leaderboard periods for blxTopScores().                                  */
#define BLXP_TODAY   0
#define BLXP_WEEK    1
#define BLXP_MONTH   2
#define BLXP_ALLTIME 3

/*--------------------------------------------------------------------------
 * Achievements, stored as a bitmask in the player record.
 *--------------------------------------------------------------------------*/
#define BLXA_FIRST   0x00000001L   /* finished a first game                 */
#define BLXA_L100    0x00000002L   /* 100 lifetime lines                    */
#define BLXA_L500    0x00000004L   /* 500 lifetime lines                    */
#define BLXA_L1000   0x00000008L   /* 1,000 lifetime lines                  */
#define BLXA_S10K    0x00000010L   /* 10,000 points in one game             */
#define BLXA_S100K   0x00000020L   /* 100,000 points in one game            */
#define BLXA_MILLION 0x00000040L   /* 1,000,000 points in one game          */
#define BLXA_TETRIS  0x00000080L   /* first four-line clear                 */
#define BLXA_COMBO   0x00000100L   /* a four-clear combo run                */
#define BLXA_LVL20   0x00000200L   /* reached level 20                      */
#define BLXA_G100    0x00000400L   /* 100 games played                      */
#define BLXA_G1000   0x00000800L   /* 1,000 games played                    */
#define BLXA_PERFECT 0x00001000L   /* cleared the entire playfield          */
#define BLXA_COUNT   13            /* number of achievements defined        */

/*--------------------------------------------------------------------------
 * One player's lifetime record (BLXPLYR.DAT).
 *--------------------------------------------------------------------------*/
struct BlxPlayer {
    CHAR userid[UIDSIZ];           /* key 0: the player                     */
    LONG highScore;                /* best single-game score                */
    LONG gamesPlayed;
    LONG totalLines;
    LONG totalBlocks;              /* lifetime pieces dropped               */
    LONG totalScore;               /* lifetime points (for the average)     */
    LONG totalSecs;                /* lifetime seconds of play              */
    LONG longestSecs;              /* longest single survival               */
    LONG lastPlayed;               /* time() of most recent game            */
    LONG lastBonusDay;             /* day stamp of last first-game bonus    */
    LONG streakDays;               /* consecutive calendar days played      */
    LONG achievements;             /* BLXA_* bits                           */
    SHORT highestLevel;
    CHAR spare[26];                /* room to grow without a conversion     */
};

/*--------------------------------------------------------------------------
 * One finished game in the score log (BLXSCOR.DAT).  The timestamp comes
 * first so the whole record can be range-scanned by date.
 *--------------------------------------------------------------------------*/
struct BlxScoreRec {
    LONG when;                     /* key 0 (duplicates): time() at finish  */
    CHAR userid[UIDSIZ];
    LONG score;
    LONG seconds;                  /* survival time                         */
    SHORT level;
    SHORT lines;
    CHAR spare[14];
};

/*--------------------------------------------------------------------------
 * The all-time Hall of Fame (BLXHALL.DAT): a single record.  One entry
 * per player -- a player improves an existing entry rather than filling
 * several places.
 *--------------------------------------------------------------------------*/
struct BlxHallEnt {
    CHAR userid[UIDSIZ];
    LONG score;
    LONG when;
    SHORT level;
    SHORT lines;
};

struct BlxHall {
    CHAR hallkey[8];               /* key 0: constant "HALL"                */
    struct BlxHallEnt ent[BLX_TOPN];
    CHAR spare[62];
};

/* A leaderboard row handed back to the display code.                       */
struct BlxTopRow {
    CHAR userid[UIDSIZ];
    LONG score;
    LONG when;
    SHORT level;
    SHORT lines;
};

/* Everything worth reporting after a finished game is recorded.            */
struct BlxGameOutcome {
    int newPersonalBest;           /* beat their own record?                */
    int hallRank;                  /* 1..25 if the game entered the Hall    */
    long newAchievements;          /* BLXA_* bits earned just now           */
    long bonusPoints;              /* daily first-game bonus applied        */
};

/*--------------------------------------------------------------------------
 * Entry points.
 *--------------------------------------------------------------------------*/
void  blxDataInit(void);           /* open (creating if needed) all files   */
void  blxDataShutdown(void);       /* close all files                       */

/* Fetch a player's record; when none exists yet a zeroed record carrying
 * the User-ID is returned and the function reports 0.                     */
GBOOL blxLoadPlayer(const CHAR *uid, struct BlxPlayer *out);

/* Record one finished game: updates the player's lifetime statistics,
 * appends to the score log (when the score qualifies), maintains the
 * Hall of Fame, evaluates achievements, and applies the daily bonus.
 * gameEvents is the engine's BLXEV_* accumulation for the whole game
 * plus anything the caller tracked (combo/perfect flags).                 */
void  blxRecordGame(const CHAR *uid, long score, int level, int lines,
                    long seconds, long blocks, int sawTetris, int sawPerfect,
                    int bestCombo, long minScore, long dailyBonus,
                    struct BlxPlayer *statsOut,
                    struct BlxGameOutcome *outcome);

/* Fill rows[] with the Top 25 for a period; returns the row count.        */
int   blxTopScores(int period, struct BlxTopRow rows[], int max);

/* Remove score-log records older than keepDays.  Run at midnight.         */
void  blxPruneScores(int keepDays);

/* Remove every trace of a deleted user account.                           */
void  blxDeletePlayer(const CHAR *uid);

#endif /* BLXDATA_H */
