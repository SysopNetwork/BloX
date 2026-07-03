/*****************************************************************************
 *   SNBLOX.CPP                                                BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   BloX -- an ANSI falling-block puzzle game for The Major BBS v10.       *
 *                                                                           *
 *   This file is the BBS-facing layer: module registration, the main       *
 *   menu, keystroke handling, the real-time heartbeat, and the wiring      *
 *   between the game engine (BLXCORE), the ANSI renderer (BLXANSI), and    *
 *   the database layer (BLXDATA).                                          *
 *                                                                           *
 *   How the real-time game loop works                                      *
 *   ----------------------------------                                     *
 *   While a user is at the menus, BloX behaves like any conversational     *
 *   module: sttrou() receives one input line at a time.                    *
 *                                                                           *
 *   When a game starts, the channel is switched to character mode with     *
 *   btutrg(chan,1): from then on every keystroke raises an INBLK status    *
 *   that arrives in stsrou().  An 18.2 Hz heartbeat registered with        *
 *   rtihdlr() injects a private status code (BLXTICK) into every channel   *
 *   with a game in progress -- the heartbeat itself runs at interrupt      *
 *   level where only the chi* calls are legal, so all real work happens    *
 *   in stsrou() when the injected status arrives in safe main-loop         *
 *   context.  Gravity advances, the renderer emits only what changed,     *
 *   and the bytes go out with btuxct().                                    *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gcomm.h"
#include "majorbbs.h"
#include "brkthu.h"
#include "usracc.h"

#include "BLXCORE.H"
#include "BLXANSI.H"
#include "BLXDATA.H"
#include "SNBLOX.H"

/* ------------------------------------------------------------------------ */
/*  Module plumbing                                                          */
/* ------------------------------------------------------------------------ */

static GBOOL blox_sttrou(VOID);    /* line input (menus)                     */
static VOID  blox_stsrou(VOID);    /* status input (game keys + heartbeat)   */
static SHORT blox_lofrou(VOID);    /* logoff: abandon any game in progress   */
static VOID  blox_huprou(VOID);    /* hangup: same, but with no output       */
static VOID  blox_dlarou(CHAR *uid); /* account deleted: purge our records   */
static VOID  blox_finrou(VOID);    /* system shutdown: close files           */

static INT usrstt;                 /* our module number from register_module */

static struct module modblk={      /* module interface block                 */
    "",                            /* name filled in from SNBLOX.MDF         */
    NULL,                          /* no logon supplement                    */
    blox_sttrou,                   /* input routine                          */
    blox_stsrou,                   /* status-input routine                   */
    NULL,                          /* injoth() reprompter (default is fine)  */
    blox_lofrou,                   /* logoff supplement                      */
    blox_huprou,                   /* hangup routine                         */
    NULL,                          /* midnight cleanup (nothing to prune)    */
    blox_dlarou,                   /* delete-account routine                 */
    blox_finrou                    /* shutdown routine                       */
};

static HMCVFILE bloxmb=NULL;       /* SNBLOX.MCV message file                */

/* Sysop options, read once at startup.                                      */
static int  cfgStartLevel;
static int  cfgHardDrop;
static long cfgDailyBonus;
static long cfgMinScore;
static BlxUiOpts cfgUi;

/* Private status code carried by our heartbeat (the 200..249 range is
 * reserved for applications).                                               */
#define BLXTICK 217

/* Menu sub-states (usrptr->substt).                                         */
#define SUBMENU  1                 /* at the main menu prompt                */
#define SUBGAME  2                 /* playing (character mode)               */
#define SUBGOVR  3                 /* game-over summary, awaiting ENTER      */

/* How long a lone ESC waits for the rest of an arrow-key sequence before
 * it is treated as the exit request, in heartbeat ticks (~55ms each).       */
#define ESCWAIT 4

/* ------------------------------------------------------------------------ */
/*  Per-channel session state                                                */
/* ------------------------------------------------------------------------ */

struct BlxSession {
    BlxGame   game;
    BlxShadow shadow;
    char active;                   /* heartbeat drives this channel          */
    char escState;                 /* 0=idle 1=got ESC 2=got ESC-[ or ESC-O  */
    char escTicks;                 /* ticks since the lone ESC arrived       */
    char confirmQuit;              /* "really quit?" prompt is showing       */
    char gameEnded;                /* endGame() already ran for this game    */
    int  sawTetris;                /* four-line clear happened this game     */
    int  sawPerfect;               /* field was emptied this game            */
    int  bestCombo;                /* longest clear run this game            */
    long bestScore;                /* player's own record, for the panel     */
};

static struct BlxSession *sessions=NULL;
static int numSessions=0;

/* One shared transmit build area: module code runs single-threaded in the
 * server's main loop, so one buffer serves every channel.                   */
static char xmtbuf[BLXA_BUFSIZ];

/* Achievement names, in BLXA_* bit order.                                   */
static const char *achvName[BLXA_COUNT]={
    "First Game",
    "100 Lines Cleared",
    "500 Lines Cleared",
    "1,000 Lines Cleared",
    "10,000 Points",
    "100,000 Points",
    "Millionaire",
    "Four-Line Master",
    "Combo Master",
    "Level 20",
    "Century Club (100 Games)",
    "Grand Club (1,000 Games)",
    "Perfect Clear"
};

/* ------------------------------------------------------------------------ */
/*  The 18.2 Hz heartbeat.                                                   */
/*                                                                           */
/*  Runs at interrupt level: touch nothing but our own flags and chiinj().   */
/* ------------------------------------------------------------------------ */
static VOID
bloxHeartbeat(VOID)
{
    int i;

    for (i=0 ; i < numSessions ; i++) {
        if (sessions[i].active) {
            chiinj(i, BLXTICK);
        }
    }
}

/* ------------------------------------------------------------------------ */
/*  Utility: transmit raw ANSI to the current user, skipping the frame       */
/*  entirely when the channel's output buffer is running low (the shadow     */
/*  is only updated when a frame is really sent, so a skipped frame just     */
/*  catches up on the next heartbeat).                                       */
/* ------------------------------------------------------------------------ */
static int
roomToSend(int need)
{
    return(btuoba(usrnum) >= need+256);
}

static void
sendRaw(const char *buf, int len)
{
    if (len > 0) {
        btuxct(usrnum, len, buf);
    }
}

static void
sendStr(const char *s)
{
    sendRaw(s, (int)strlen(s));
}

/* Draw or erase the "really quit?" question on the bottom line.             */
static void
drawConfirm(int show)
{
    if (show) {
        sendStr("\x1B[24;3H\x1B[0;1;33mQuit this game? (Y/N)\x1B[0m");
    }
    else {
        sendStr("\x1B[24;3H                     ");
    }
}

/* ------------------------------------------------------------------------ */
/*  Game lifecycle                                                           */
/* ------------------------------------------------------------------------ */

static long
gameSeconds(const struct BlxSession *s)
{
    return((long)(s->game.playTicks*10L/182L));   /* ticks at 18.2 Hz        */
}

/* Begin a fresh game on the current channel and switch it to char mode.     */
static void
startGame(VOID)
{
    struct BlxSession *s=&sessions[usrnum];
    struct BlxPlayer p;
    unsigned long seed;
    int n;

    memset(s, 0, sizeof(*s));
    blxLoadPlayer(usaptr->userid, &p);
    s->bestScore=p.highScore;

    seed=(unsigned long)time(NULL);
    seed^=((unsigned long)usrnum << 20)+(unsigned long)hrtval();
    blxStart(&s->game, cfgStartLevel, seed);

    btuech(usrnum, 0);             /* no echo: the renderer owns the screen  */
    btutrg(usrnum, 1);             /* every keystroke raises INBLK           */
    usrptr->flags|=NOINJO;         /* hold asynchronous messages until done  */

    n=blxDrawFull(xmtbuf, &s->game, &s->shadow, usaptr->userid,
                  s->bestScore, 0L, &cfgUi);
    sendRaw(xmtbuf, n);
    s->active=1;
    usrptr->substt=SUBGAME;
}

/* Take the channel out of character mode.                                   */
static void
restoreLineMode(VOID)
{
    btutrg(usrnum, 0);
    btuech(usrnum, 1);
    usrptr->flags&=~NOINJO;
}

/* Finish a game: record it, print the summary, and return to line mode.
 * 'quiet' suppresses all output (hangup path).                              */
static void
endGame(int quiet)
{
    struct BlxSession *s=&sessions[usrnum];
    struct BlxPlayer p;
    struct BlxGameOutcome oc;
    char scorestr[16];
    long secs=gameSeconds(s);
    int i, n;

    if (s->gameEnded) {
        return;
    }
    s->gameEnded=1;
    s->active=0;                   /* stop the heartbeat first               */

    blxRecordGame(usaptr->userid, s->game.score, s->game.level,
                  s->game.lines, secs, s->game.piecesDropped,
                  s->sawTetris, s->sawPerfect, s->bestCombo,
                  cfgMinScore, cfgDailyBonus, &p, &oc);

    if (quiet) {
        return;                    /* channel is gone; no goodbye screen     */
    }

    restoreLineMode();
    n=blxDrawExit(xmtbuf);
    sendRaw(xmtbuf, n);

    setmbk(bloxmb);
    sprintf(scorestr, "%ld", s->game.score+oc.bonusPoints);
    prfmsg(GOVRHDR, commas(scorestr), s->game.level, s->game.lines,
           secs/60L, secs%60L);
    if (oc.bonusPoints > 0L) {
        prfmsg(BONUSMSG, oc.bonusPoints);
    }
    if (oc.newPersonalBest) {
        prfmsg(NEWBEST);
    }
    if (oc.hallRank > 0) {
        prfmsg(HALLRANK, oc.hallRank);
    }
    for (i=0 ; i < BLXA_COUNT ; i++) {
        if (oc.newAchievements&(1L << i)) {
            prfmsg(ACHVMSG, achvName[i]);
        }
    }
    prfmsg(PRESSENT);
    outprf(usrnum);
    rstmbk();
    usrptr->substt=SUBGOVR;
}

/* ------------------------------------------------------------------------ */
/*  In-game keystroke handling                                               */
/* ------------------------------------------------------------------------ */

static void
gameAction(int action)             /* 'L','R','U','D',' ','P','Q'            */
{
    struct BlxSession *s=&sessions[usrnum];

    switch (action) {
    case 'L':
        blxMoveLeft(&s->game);
        break;
    case 'R':
        blxMoveRight(&s->game);
        break;
    case 'U':
        blxRotate(&s->game);
        break;
    case 'D':
        blxSoftDrop(&s->game);
        break;
    case ' ':
        if (cfgHardDrop) {
            blxHardDrop(&s->game);
        }
        break;
    case 'P':
        blxTogglePause(&s->game);
        break;
    case 'Q':
        endGame(0);
        break;
    default:
        break;
    }
}

/* Freeze the game and pose the quit question.                              */
static void
askQuit(VOID)
{
    struct BlxSession *s=&sessions[usrnum];

    if (s->game.phase != BLX_PLAYING && s->game.phase != BLX_CLEARING
     && s->game.phase != BLX_PAUSED) {
        return;                    /* nothing quittable is happening         */
    }
    if (s->game.phase != BLX_PAUSED) {
        blxTogglePause(&s->game);  /* freeze (and blank) while asking        */
    }
    s->confirmQuit=1;
    if (roomToSend(600)) {
        int n=blxDrawDelta(xmtbuf, &s->game, &s->shadow,
                           gameSeconds(s), &cfgUi);
        sendRaw(xmtbuf, n);
    }
    drawConfirm(1);
}

static void
gameKey(unsigned char c)
{
    struct BlxSession *s=&sessions[usrnum];

    /* answer to the quit question, if it is showing                        */
    if (s->confirmQuit) {
        if (c == 'y' || c == 'Y') {
            drawConfirm(0);
            s->confirmQuit=0;
            gameAction('Q');
            return;
        }
        s->confirmQuit=0;
        drawConfirm(0);
        if (s->game.phase == BLX_PAUSED) {
            blxTogglePause(&s->game);       /* resume play                   */
        }
        return;
    }

    /* arrow keys arrive as ESC [ A/B/C/D (or ESC O A/B/C/D)                */
    if (s->escState == 1) {
        if (c == '[' || c == 'O') {
            s->escState=2;
            return;
        }
        s->escState=0;             /* lone ESC: ask about quitting; the key  */
        askQuit();                 /* that broke the sequence is swallowed   */
        return;
    }
    if (s->escState == 2) {
        s->escState=0;
        switch (c) {
        case 'A':
            gameAction('U');       /* up = rotate                            */
            return;
        case 'B':
            gameAction('D');       /* down = soft drop                       */
            return;
        case 'C':
            gameAction('R');
            return;
        case 'D':
            gameAction('L');
            return;
        default:
            return;                /* some other sequence: swallow it        */
        }
    }

    switch (c) {
    case 0x1B:                     /* ESC: might be an arrow key prefix      */
        s->escState=1;
        s->escTicks=0;
        return;
    case 'a': case 'A': case '4':
        gameAction('L');
        return;
    case 'd': case 'D': case '6':
        gameAction('R');
        return;
    case 'w': case 'W': case '8':
        gameAction('U');
        return;
    case 's': case 'S': case '2':
        gameAction('D');
        return;
    case ' ':
        gameAction(' ');
        return;
    case 'p': case 'P':
        gameAction('P');
        return;
    case 'q': case 'Q':
        gameAction('Q');
        return;
    default:
        return;                    /* everything else is ignored             */
    }
}

/* Render whatever changed and pass along any sound cues.                    */
static void
renderFrame(VOID)
{
    struct BlxSession *s=&sessions[usrnum];
    unsigned ev;
    int n;

    if (!roomToSend(600)) {
        return;                    /* slow link: catch up next heartbeat     */
    }
    n=blxDrawDelta(xmtbuf, &s->game, &s->shadow, gameSeconds(s), &cfgUi);
    sendRaw(xmtbuf, n);

    ev=s->game.events;
    s->game.events=0;
    if (ev != 0) {
        if (ev&BLXEV_TETRIS) {
            s->sawTetris=1;
        }
        if (ev&BLXEV_PERFECT) {
            s->sawPerfect=1;
        }
        n=blxDrawBells(xmtbuf, ev, &cfgUi);
        sendRaw(xmtbuf, n);
    }
    if (s->game.comboRun > s->bestCombo) {
        s->bestCombo=s->game.comboRun;
    }
}

/* One heartbeat for the current user's game.                                */
static void
gameTick(VOID)
{
    struct BlxSession *s=&sessions[usrnum];

    /* a lone ESC that never became an arrow key is the quit request        */
    if (s->escState == 1 && ++s->escTicks > ESCWAIT) {
        s->escState=0;
        askQuit();
    }

    blxTick(&s->game);

    if (s->game.phase == BLX_GAMEOVER && !s->gameEnded) {
        renderFrame();             /* let the final stack appear             */
        endGame(0);
        return;
    }
    renderFrame();
}

/* ------------------------------------------------------------------------ */
/*  Menus and reports (ordinary line mode)                                   */
/* ------------------------------------------------------------------------ */

static void
showHallOfFame(VOID)
{
    struct BlxTopRow rows[BLX_TOPN];
    char scorestr[16], datestr[12];
    struct tm *lt;
    time_t t;
    int i, n;

    n=blxHallTop(rows, BLX_TOPN);
    prfmsg(HALLHDR);
    if (n == 0) {
        prfmsg(HSEMPTY);
    }
    for (i=0 ; i < n ; i++) {
        sprintf(scorestr, "%ld", rows[i].score);
        t=(time_t)rows[i].when;
        lt=localtime(&t);
        if (lt != NULL) {
            sprintf(datestr, "%02d/%02d/%02d", lt->tm_mon+1, lt->tm_mday,
                    lt->tm_year%100);
        }
        else {
            strcpy(datestr, "--/--/--");
        }
        prfmsg(HSROW, i+1, rows[i].userid, commas(scorestr),
               (int)rows[i].level, (int)rows[i].lines, datestr);
    }
    prfmsg(HSFOOT);
}

static void
showStats(VOID)
{
    struct BlxPlayer p;
    char best[16], avg[16];
    long avgScore;
    int i, nachv;

    blxLoadPlayer(usaptr->userid, &p);
    avgScore=(p.gamesPlayed > 0L ? p.totalScore/p.gamesPlayed : 0L);
    nachv=0;
    for (i=0 ; i < BLXA_COUNT ; i++) {
        if (p.achievements&(1L << i)) {
            nachv++;
        }
    }
    sprintf(best, "%ld", p.highScore);
    sprintf(avg, "%ld", avgScore);
    prfmsg(STATSSCR,
           usaptr->userid,
           commas(best),
           p.gamesPlayed,
           (int)p.highestLevel,
           p.totalLines,
           p.totalBlocks,
           commas(avg),
           p.longestSecs/60L, p.longestSecs%60L,
           p.totalSecs/3600L, (p.totalSecs%3600L)/60L,
           p.streakDays,
           nachv, (int)BLXA_COUNT);
    /* list the earned achievements                                          */
    for (i=0 ; i < BLXA_COUNT ; i++) {
        if (p.achievements&(1L << i)) {
            prfmsg(ACHVMSG, achvName[i]);
        }
    }
}

/* ------------------------------------------------------------------------ */
/*  Module entry points                                                      */
/* ------------------------------------------------------------------------ */

extern "C" {

VOID EXPORT
init__snblox(VOID)                 /* module initialization                  */
{
    stzcpy(modblk.descrp, gmdnam("SNBLOX.MDF"), MNMSIZ);
    usrstt=register_module(&modblk);

    bloxmb=opnmsg("SNBLOX.MCV");
    cfgStartLevel=(int)numopt(STARTLVL, 1, BLX_MAXLEVEL);
    cfgHardDrop=ynopt(HARDDROP);
    cfgUi.showGhost=ynopt(GHOSTPC);
    cfgUi.showNext=ynopt(SHOWNEXT);
    cfgUi.sounds=ynopt(SOUNDS);
    cfgDailyBonus=lngopt(DLYBONUS, 0L, 100000L);
    cfgMinScore=lngopt(MINSCORE, 0L, 1000000L);

    blxDataInit();

    numSessions=nterms;
    sessions=(struct BlxSession *)alczer(numSessions
                                         *sizeof(struct BlxSession));
    rtihdlr(bloxHeartbeat);

    /* Two audit-trail lines at boot, matching the house style: the module
     * banner on the first line and the attribution on the second.          */
    shocst("BloX v1.0.0 Stacked", "");
    shocst("By SysopNetwork.com", "");
}

}   /* extern "C" */

static GBOOL
blox_sttrou(VOID)                  /* line input while at the menus          */
{
    GBOOL stay=TRUE;

    setmbk(bloxmb);
    switch (usrptr->substt) {
    case 0:                        /* just entered from the menu tree        */
        prfmsg(MENU);
        usrptr->substt=SUBMENU;
        break;

    case SUBMENU:
        if (margc == 0 || sameas(margv[0], "")) {
            prfmsg(SHORTMNU);      /* covers injoth() reprompts too          */
        }
        else if (sameas(margv[0], "x")) {
            prfmsg(BYE);
            stay=FALSE;
        }
        else if (sameas(margv[0], "?")) {
            prfmsg(MENU);
        }
        else if (sameas(margv[0], "p")) {
            if (!(usaptr->ansifl&ANSON)) {
                prfmsg(NOANSI);
                prfmsg(SHORTMNU);
            }
            else {
                outprf(usrnum);
                rstmbk();
                startGame();       /* switches the channel to char mode      */
                return(TRUE);
            }
        }
        else if (sameas(margv[0], "i")) {
            prfmsg(INSTRUCT);
            prfmsg(SHORTMNU);
        }
        else if (sameas(margv[0], "s")) {
            showStats();
            prfmsg(SHORTMNU);
        }
        else if (sameas(margv[0], "h") || sameas(margv[0], "a")
              || sameas(margv[0], "f")) {
            showHallOfFame();
            prfmsg(SHORTMNU);
        }
        else {
            prfmsg(INVALID);
            prfmsg(SHORTMNU);
        }
        break;

    case SUBGAME:
        /* stray line input while in character mode: ignore it              */
        break;

    case SUBGOVR:                  /* any line returns to the menu           */
        prfmsg(MENU);
        usrptr->substt=SUBMENU;
        break;

    default:
        prfmsg(MENU);
        usrptr->substt=SUBMENU;
        break;
    }
    outprf(usrnum);
    rstmbk();
    return(stay);
}

static VOID
blox_stsrou(VOID)                  /* statuses: keystrokes and heartbeats    */
{
    struct BlxSession *s=&sessions[usrnum];
    CHAR c;

    if (status == BLXTICK) {
        if (s->active && usrptr->substt == SUBGAME) {
            gameTick();
        }
        return;                    /* stale heartbeats are simply dropped    */
    }
    if (status == INBLK) {
        btuict(usrnum, &c);
        if (s->active && usrptr->substt == SUBGAME) {
            gameKey((unsigned char)c);
            if (s->active) {
                renderFrame();     /* immediate response to the keystroke    */
            }
        }
        return;
    }
    dfsthn();                      /* anything else gets default handling    */
}

static SHORT
blox_lofrou(VOID)                  /* user logging off                       */
{
    struct BlxSession *s=&sessions[usrnum];

    if (s->active) {
        endGame(1);                /* record the abandoned game quietly      */
        restoreLineMode();
    }
    return(0);
}

static VOID
blox_huprou(VOID)                  /* carrier lost                           */
{
    struct BlxSession *s=&sessions[usrnum];

    if (s->active) {
        s->active=0;
        endGame(1);                /* record it; never write to the channel  */
    }
}

static VOID
blox_dlarou(CHAR *uid)             /* account deleted                        */
{
    blxDeletePlayer(uid);
}

static VOID
blox_finrou(VOID)                  /* system shutdown                        */
{
    blxDataShutdown();
    if (bloxmb != NULL) {
        clsmsg(bloxmb);
        bloxmb=NULL;
    }
}

extern "C" {

VOID EXPORT
end_snblox(VOID)                   /* marks the module's end for crash logs  */
{
}

}   /* extern "C" */
