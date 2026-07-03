/*****************************************************************************
 *   BLXDATA.CPP                                               BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   Persistence layer.  See BLXDATA.H for the file layout overview.        *
 *****************************************************************************/

#include <stddef.h>
#include <string.h>
#include <time.h>
#include "gcomm.h"
#include "majorbbs.h"
#include "dfaapi.h"
#include "BLXDATA.H"

static DFAFILE *plyrbb=NULL;       /* BLXPLYR.DAT                           */
static DFAFILE *scorbb=NULL;       /* BLXSCOR.DAT                           */
static DFAFILE *hallbb=NULL;       /* BLXHALL.DAT                           */

#define HALLKEY "HALL"

/*--------------------------------------------------------------------------
 * File creation.  Each file is created on first startup with the same key
 * layout the code expects, so a fresh installation needs no separate
 * database preparation step, and a deleted file heals itself.
 *--------------------------------------------------------------------------*/
static void
createIfMissing(const CHAR *filnam, size_t reclen, size_t pagesize,
                size_t keyoff, size_t keylen, int keytype, int keyflags)
{
    FILE *fp=fopen(filnam, "rb");
    struct dfaSegSpec seg;
    struct dfaKeySpec key;

    if (fp != NULL) {
        fclose(fp);
        return;
    }
    memset(&seg, 0, sizeof(seg));
    seg.position=keyoff;
    seg.length=keylen;
    seg.type=keytype;
    seg.flags=0;
    memset(&key, 0, sizeof(key));
    key.flags=keyflags;
    key.nSegments=1;
    key.segs=&seg;
    dfaCreateSpec(filnam, FALSE, reclen, pagesize, 0, 1, 1, &key, NULL);
}

void
blxDataInit(void)
{
    struct BlxHall hall;

    /* the Hall of Fame record is over a kilobyte, so its file needs the   */
    /* larger Btrieve page size                                            */
    createIfMissing("BLXPLYR.DAT", sizeof(struct BlxPlayer), 1024,
                    offsetof(struct BlxPlayer, userid), UIDSIZ,
                    DFAST_STRING, DFAKF_MODIFYABLE);
    createIfMissing("BLXSCOR.DAT", sizeof(struct BlxScoreRec), 1024,
                    offsetof(struct BlxScoreRec, when), sizeof(LONG),
                    DFAST_INTEGER, DFAKF_DUPLICATE);
    createIfMissing("BLXHALL.DAT", sizeof(struct BlxHall), 2048,
                    offsetof(struct BlxHall, hallkey), 8,
                    DFAST_STRING, DFAKF_MODIFYABLE);

    plyrbb=dfaOpen("BLXPLYR.DAT", sizeof(struct BlxPlayer), NULL);
    scorbb=dfaOpen("BLXSCOR.DAT", sizeof(struct BlxScoreRec), NULL);
    hallbb=dfaOpen("BLXHALL.DAT", sizeof(struct BlxHall), NULL);

    /* make sure the single Hall of Fame record exists                      */
    dfaSetBlk(hallbb);
    if (!dfaAcqEQ(&hall, (VOID*)HALLKEY, 0)) {
        memset(&hall, 0, sizeof(hall));
        stzcpy(hall.hallkey, HALLKEY, sizeof(hall.hallkey));
        dfaInsert(&hall);
    }
    dfaRstBlk();
}

void
blxDataShutdown(void)
{
    if (plyrbb != NULL) {
        dfaClose(plyrbb);
        plyrbb=NULL;
    }
    if (scorbb != NULL) {
        dfaClose(scorbb);
        scorbb=NULL;
    }
    if (hallbb != NULL) {
        dfaClose(hallbb);
        hallbb=NULL;
    }
}

/*--------------------------------------------------------------------------
 * Key helper: User-ID keys are fixed 30-byte fields, so lookups must use
 * a zero-padded copy rather than a bare C string.
 *--------------------------------------------------------------------------*/
static void
padUid(CHAR key[UIDSIZ], const CHAR *uid)
{
    memset(key, 0, UIDSIZ);
    stzcpy(key, uid, UIDSIZ);
}

GBOOL
blxLoadPlayer(const CHAR *uid, struct BlxPlayer *out)
{
    CHAR key[UIDSIZ];
    GBOOL found;

    padUid(key, uid);
    dfaSetBlk(plyrbb);
    found=dfaAcqEQ(out, key, 0);
    dfaRstBlk();
    if (!found) {
        memset(out, 0, sizeof(*out));
        stzcpy(out->userid, uid, UIDSIZ);
    }
    return(found);
}

static void
savePlayer(struct BlxPlayer *p)
{
    struct BlxPlayer cur;
    CHAR key[UIDSIZ];

    padUid(key, p->userid);
    dfaSetBlk(plyrbb);
    if (dfaAcqEQ(&cur, key, 0)) {
        dfaUpdate(p);
    }
    else {
        dfaInsert(p);
    }
    dfaRstBlk();
}

/*--------------------------------------------------------------------------
 * Local calendar day stamp (days since 1970 in local time), used for the
 * first-game-of-the-day bonus and the return-streak counter.
 *--------------------------------------------------------------------------*/
static long
dayStamp(time_t t)
{
    struct tm *lt=localtime(&t);

    if (lt == NULL) {
        return(0L);
    }
    /* year * 512 + day-of-year is compact, unique, and ordered enough to  */
    /* compare adjacent days                                               */
    return((long)(lt->tm_year)*512L+lt->tm_yday);
}

/* Is stamp b exactly the calendar day after stamp a?                      */
static int
isNextDay(long a, long b)
{
    /* handles the year rollover: Dec 31 -> Jan 1                          */
    return(b == a+1L
        || (b%512L == 0L && (a%512L) >= 364L && b/512L == a/512L+1L));
}

/*--------------------------------------------------------------------------
 * Hall of Fame maintenance: one entry per player, kept sorted by score.
 * Returns the 1-based rank the game earned, or 0 when it did not place.
 *--------------------------------------------------------------------------*/
static int
hallSubmit(const CHAR *uid, long score, int level, int lines, long when)
{
    struct BlxHall hall;
    struct BlxHallEnt ent;
    int i, slot, rank;

    dfaSetBlk(hallbb);
    if (!dfaAcqEQ(&hall, (VOID*)HALLKEY, 0)) {
        dfaRstBlk();               /* cannot happen: created at startup     */
        return(0);
    }

    memset(&ent, 0, sizeof(ent));
    stzcpy(ent.userid, uid, UIDSIZ);
    ent.score=score;
    ent.level=(SHORT)level;
    ent.lines=(SHORT)lines;
    ent.when=when;

    /* if the player already holds a place, only an improvement counts     */
    slot=-1;
    for (i=0 ; i < BLX_TOPN ; i++) {
        if (hall.ent[i].userid[0] != '\0'
         && sameas(hall.ent[i].userid, (CHAR*)uid)) {
            slot=i;
            break;
        }
    }
    if (slot >= 0 && hall.ent[slot].score >= score) {
        dfaRstBlk();
        return(0);
    }
    if (slot >= 0) {
        /* remove the old entry; the better one is inserted below          */
        for (i=slot ; i < BLX_TOPN-1 ; i++) {
            hall.ent[i]=hall.ent[i+1];
        }
        memset(&hall.ent[BLX_TOPN-1], 0, sizeof(hall.ent[0]));
    }

    /* find where the new score belongs (empty slots score as zero)        */
    rank=0;
    for (i=0 ; i < BLX_TOPN ; i++) {
        if (hall.ent[i].userid[0] == '\0' || score > hall.ent[i].score) {
            rank=i+1;
            break;
        }
    }
    if (rank == 0) {
        dfaRstBlk();
        return(0);
    }
    for (i=BLX_TOPN-1 ; i >= rank ; i--) {
        hall.ent[i]=hall.ent[i-1];
    }
    hall.ent[rank-1]=ent;
    dfaUpdate(&hall);
    dfaRstBlk();
    return(rank);
}

/*--------------------------------------------------------------------------
 * Achievements earned by the game just finished.  Lifetime totals must
 * already be updated when this is called.
 *--------------------------------------------------------------------------*/
static long
evalAchievements(const struct BlxPlayer *p, long score, int level,
                 int sawTetris, int sawPerfect, int bestCombo)
{
    long earned=0L;

    if (p->gamesPlayed >= 1)      earned|=BLXA_FIRST;
    if (p->totalLines >= 100)     earned|=BLXA_L100;
    if (p->totalLines >= 500)     earned|=BLXA_L500;
    if (p->totalLines >= 1000)    earned|=BLXA_L1000;
    if (score >= 10000L)          earned|=BLXA_S10K;
    if (score >= 100000L)         earned|=BLXA_S100K;
    if (score >= 1000000L)        earned|=BLXA_MILLION;
    if (sawTetris)                earned|=BLXA_TETRIS;
    if (bestCombo >= 4)           earned|=BLXA_COMBO;
    if (level >= 20)              earned|=BLXA_LVL20;
    if (p->gamesPlayed >= 100)    earned|=BLXA_G100;
    if (p->gamesPlayed >= 1000)   earned|=BLXA_G1000;
    if (sawPerfect)               earned|=BLXA_PERFECT;

    return(earned&~p->achievements);   /* only the newly earned bits        */
}

void
blxRecordGame(const CHAR *uid, long score, int level, int lines,
              long seconds, long blocks, int sawTetris, int sawPerfect,
              int bestCombo, long minScore, long dailyBonus,
              struct BlxPlayer *statsOut, struct BlxGameOutcome *outcome)
{
    struct BlxPlayer p;
    struct BlxScoreRec rec;
    time_t now=time(NULL);
    long today=dayStamp(now);
    long lastDay;

    memset(outcome, 0, sizeof(*outcome));
    blxLoadPlayer(uid, &p);

    /* ----- first game of the day: bonus and return streak --------------- */
    lastDay=dayStamp((time_t)p.lastPlayed);
    if (p.lastBonusDay != today) {
        if (dailyBonus > 0L) {
            score+=dailyBonus;
            outcome->bonusPoints=dailyBonus;
        }
        p.lastBonusDay=today;
        if (p.lastPlayed != 0L && isNextDay(lastDay, today)) {
            p.streakDays++;
        }
        else if (lastDay != today) {
            p.streakDays=1;
        }
    }

    /* ----- lifetime statistics ------------------------------------------ */
    p.gamesPlayed++;
    p.totalLines+=lines;
    p.totalBlocks+=blocks;
    p.totalScore+=score;
    p.totalSecs+=seconds;
    if (seconds > p.longestSecs) {
        p.longestSecs=seconds;
    }
    if (level > (int)p.highestLevel) {
        p.highestLevel=(SHORT)level;
    }
    if (score > p.highScore) {
        p.highScore=score;
        outcome->newPersonalBest=1;
    }
    p.lastPlayed=(LONG)now;

    /* ----- achievements -------------------------------------------------- */
    outcome->newAchievements=evalAchievements(&p, score, level, sawTetris,
                                              sawPerfect, bestCombo);
    p.achievements|=outcome->newAchievements;

    savePlayer(&p);
    if (statsOut != NULL) {
        *statsOut=p;
    }

    /* ----- score log (feeds the daily/weekly/monthly boards) ------------- */
    if (score >= minScore) {
        memset(&rec, 0, sizeof(rec));
        rec.when=(LONG)now;
        stzcpy(rec.userid, uid, UIDSIZ);
        rec.score=score;
        rec.seconds=seconds;
        rec.level=(SHORT)level;
        rec.lines=(SHORT)lines;
        dfaSetBlk(scorbb);
        dfaInsert(&rec);
        dfaRstBlk();
    }

    /* ----- all-time Hall of Fame ----------------------------------------- */
    if (score >= minScore) {
        outcome->hallRank=hallSubmit(uid, score, level, lines, (long)now);
    }
}

/*--------------------------------------------------------------------------
 * Leaderboards.
 *
 * The daily, weekly, and monthly boards scan the score log from a cutoff
 * time forward, keeping each player's single best game, and hand back the
 * Top 25.  The all-time board reads the Hall of Fame record directly.
 *--------------------------------------------------------------------------*/
static time_t
periodCutoff(int period)
{
    time_t now=time(NULL);
    struct tm *lt=localtime(&now);
    time_t midnight;

    if (lt == NULL) {
        return(now-86400L);
    }
    lt->tm_hour=0;
    lt->tm_min=0;
    lt->tm_sec=0;
    midnight=mktime(lt);
    switch (period) {
    case BLXP_TODAY:
        return(midnight);
    case BLXP_WEEK:
        return(midnight-(time_t)6*86400L);
    default:                       /* BLXP_MONTH                            */
        return(midnight-(time_t)29*86400L);
    }
}

int
blxTopScores(int period, struct BlxTopRow rows[], int max)
{
    struct BlxScoreRec rec;
    struct BlxHall hall;
    LONG cutoff;
    GBOOL more;
    int n=0;
    int i, j, slot;

    if (max > BLX_TOPN) {
        max=BLX_TOPN;
    }

    if (period == BLXP_ALLTIME) {
        dfaSetBlk(hallbb);
        if (dfaAcqEQ(&hall, (VOID*)HALLKEY, 0)) {
            for (i=0 ; i < max && hall.ent[i].userid[0] != '\0' ; i++) {
                stzcpy(rows[i].userid, hall.ent[i].userid, UIDSIZ);
                rows[i].score=hall.ent[i].score;
                rows[i].when=hall.ent[i].when;
                rows[i].level=hall.ent[i].level;
                rows[i].lines=hall.ent[i].lines;
                n++;
            }
        }
        dfaRstBlk();
        return(n);
    }

    cutoff=(LONG)periodCutoff(period);
    dfaSetBlk(scorbb);
    more=dfaAcqGE(&rec, &cutoff, 0);
    while (more) {
        /* is this player already on the board?                            */
        slot=-1;
        for (i=0 ; i < n ; i++) {
            if (sameas(rows[i].userid, rec.userid)) {
                slot=i;
                break;
            }
        }
        if (slot >= 0) {
            if (rec.score > rows[slot].score) {
                /* replace their weaker game                               */
                for (i=slot ; i < n-1 ; i++) {
                    rows[i]=rows[i+1];
                }
                n--;
                slot=-1;
            }
        }
        if (slot < 0) {
            /* insert in descending score order                            */
            for (i=0 ; i < n && rows[i].score >= rec.score ; i++) {
            }
            if (i < max) {
                for (j=(n < max ? n : max-1) ; j > i ; j--) {
                    rows[j]=rows[j-1];
                }
                stzcpy(rows[i].userid, rec.userid, UIDSIZ);
                rows[i].score=rec.score;
                rows[i].when=rec.when;
                rows[i].level=rec.level;
                rows[i].lines=rec.lines;
                if (n < max) {
                    n++;
                }
            }
        }
        more=dfaAcqNX(&rec);
    }
    dfaRstBlk();
    return(n);
}

/*--------------------------------------------------------------------------
 * Nightly maintenance: drop score-log records older than the retention
 * window.  The Hall of Fame is untouched by design.
 *--------------------------------------------------------------------------*/
void
blxPruneScores(int keepDays)
{
    struct BlxScoreRec rec;
    LONG cutoff=(LONG)(time(NULL)-(time_t)keepDays*86400L);
    int guard=100000;              /* safety valve against a runaway loop   */

    dfaSetBlk(scorbb);
    while (guard-- > 0 && dfaAcqLO(&rec, 0) && rec.when < cutoff) {
        dfaDelete();
    }
    dfaRstBlk();
}

/*--------------------------------------------------------------------------
 * Account deletion: remove the player record, their score-log entries,
 * and any Hall of Fame places.
 *--------------------------------------------------------------------------*/
void
blxDeletePlayer(const CHAR *uid)
{
    struct BlxPlayer p;
    struct BlxScoreRec rec;
    struct BlxHall hall;
    CHAR key[UIDSIZ];
    GBOOL more;
    int i, j, changed;

    padUid(key, uid);
    dfaSetBlk(plyrbb);
    if (dfaAcqEQ(&p, key, 0)) {
        dfaDelete();
    }
    dfaRstBlk();

    /* deleting invalidates the scan position, so restart after each hit   */
    dfaSetBlk(scorbb);
    for (;;) {
        more=dfaStepLO(&rec) != 0;
        while (more && !sameas(rec.userid, (CHAR*)uid)) {
            more=dfaStepNX(&rec) != 0;
        }
        if (!more) {
            break;
        }
        dfaDelete();
    }
    dfaRstBlk();

    dfaSetBlk(hallbb);
    if (dfaAcqEQ(&hall, (VOID*)HALLKEY, 0)) {
        changed=0;
        for (i=0 ; i < BLX_TOPN ; i++) {
            if (hall.ent[i].userid[0] != '\0'
             && sameas(hall.ent[i].userid, (CHAR*)uid)) {
                for (j=i ; j < BLX_TOPN-1 ; j++) {
                    hall.ent[j]=hall.ent[j+1];
                }
                memset(&hall.ent[BLX_TOPN-1], 0, sizeof(hall.ent[0]));
                changed=1;
                i--;               /* re-examine the slot just shifted in   */
            }
        }
        if (changed) {
            dfaUpdate(&hall);
        }
    }
    dfaRstBlk();
}
