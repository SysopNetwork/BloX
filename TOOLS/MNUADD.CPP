/*****************************************************************************
 *   MNUADD.CPP                                                BloX v1.0.0   *
 *                                                                           *
 *   Copyright (c) 2026 Sysop Network.  All Rights Reserved.                 *
 *                                                                           *
 *   Offline menu-tree helper for The Major BBS v10.                        *
 *                                                                           *
 *   Adds a module page to the ASCII menu tree (WGSMENU2.dat) by cloning    *
 *   an existing module page and patching its fields, then adding a         *
 *   selection entry to the parent page.  Run it from the BBS directory     *
 *   while the server is OFFLINE:                                           *
 *                                                                           *
 *       MNUADD INFO                     show the tree's module pages       *
 *       MNUADD ADD <newpage> <module> <template> <selchr> <title>          *
 *                                                                           *
 *   Example:                                                               *
 *       MNUADD ADD BLOX BloX FRACTURE B "BloX - Falling Blocks"            *
 *                                                                           *
 *   The tool talks to the same Btrieve engine the BBS uses (WBTRV32.DLL)   *
 *   and lays records out with the very structures the server was          *
 *   compiled with, so the tree it writes is exactly what the Menu Tree     *
 *   editor would have produced.                                            *
 *****************************************************************************/

#include <windows.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "gcomm.h"
#include "majorbbs.h"

/* The BBS headers reroute printf() to the server console; this tool is a
 * plain command-line program, so put the real one back.                    */
#ifdef printf
#undef printf
#endif

/* ---- Btrieve operation codes ------------------------------------------- */
#define B_OPEN      0
#define B_CLOSE     1
#define B_INSERT    2
#define B_UPDATE    3
#define B_GETEQ     5
#define B_GETNEXT   6
#define B_GETFIRST 12
#define B_STAT     15

typedef SHORT (__stdcall *BTRCALL_T)(WORD op, VOID *posblk, VOID *databuf,
                                     DWORD *datalen, VOID *keybuf,
                                     BYTE keylen, CHAR keynum);
static BTRCALL_T btrcall;

static CHAR posblk[128];

/* The menu key is the page type letter plus the page name.                 */
struct menukey {
    CHAR menutype[1];
    CHAR pagnam[PNMSIZ];
};

#define MAXREC 16384
static CHAR recbuf[MAXREC];

static SHORT
bget(WORD op, VOID *data, DWORD *len, VOID *key, BYTE keylen)
{
    return(btrcall(op, posblk, data, len, key, keylen, 0));
}

static void
makekey(struct menukey *k, const CHAR *pagnam)
{
    memset(k, 0, sizeof(*k));
    k->menutype[0]='A';
    strncpy(k->pagnam, pagnam, PNMSIZ);
}

static int
openTree(void)
{
    DWORD len=0;
    CHAR fname[]="WGSMENU2.dat";
    SHORT st;

    st=btrcall(B_OPEN, posblk, NULL, &len, fname, (BYTE)strlen(fname), 0);
    if (st != 0) {
        printf("Cannot open WGSMENU2.dat (Btrieve status %d).\n", st);
        printf("Run this from the BBS directory with the server down,\n");
        printf("and make sure the Btrieve engine (W32MKDE) is available.\n");
        return(0);
    }
    return(1);
}

static void
info(void)
{
    struct mnupag *pg=(struct mnupag *)recbuf;
    struct menukey key;
    DWORD len;
    SHORT st;
    int i;

    printf("compiled record: base=%u pglink=%u (record limit %u)\n\n",
           (unsigned)offsetof(struct mnupag, page),
           (unsigned)sizeof(struct pglink), (unsigned)MAXREC);

    memset(&key, 0, sizeof(key));
    len=MAXREC;
    st=bget(B_GETFIRST, recbuf, &len, &key, sizeof(key));
    while (st == 0) {
        printf("%c %-16.16s parent=%-16.16s module=%-24.24s len=%lu sels=%d\n",
               pg->menutype[0], pg->pagnam, pg->parpag, pg->modnam,
               (unsigned long)len, (int)pg->npages);
        for (i=0 ; i < pg->npages && i < MAXSEL ; i++) {
            printf("    [%c] -> %-16.16s (%s)\n",
                   pg->page[i].selchr, pg->page[i].destpage,
                   pg->page[i].shortd);
        }
        len=MAXREC;
        st=bget(B_GETNEXT, recbuf, &len, &key, sizeof(key));
    }
}

static int
add(const CHAR *newpage, const CHAR *module, const CHAR *templ,
    CHAR selchr, const CHAR *title)
{
    static CHAR newrec[MAXREC];
    struct mnupag *pg=(struct mnupag *)recbuf;
    struct mnupag *np=(struct mnupag *)newrec;
    struct menukey key;
    struct pglink *pl;
    CHAR parent[PNMSIZ+1];
    DWORD len, newlen;
    SHORT st;
    int i;

    /* refuse to add a page that already exists                             */
    makekey(&key, newpage);
    len=MAXREC;
    if (bget(B_GETEQ, recbuf, &len, &key, sizeof(key)) == 0) {
        printf("Page %s already exists; nothing done.\n", newpage);
        return(1);
    }

    /* fetch the template module page                                       */
    makekey(&key, templ);
    len=MAXREC;
    st=bget(B_GETEQ, recbuf, &len, &key, sizeof(key));
    if (st != 0) {
        printf("Template page %s not found (status %d).\n", templ, st);
        return(0);
    }
    memset(parent, 0, sizeof(parent));
    strncpy(parent, pg->parpag, PNMSIZ);

    /* clone it and patch identity, module, and title                       */
    memcpy(newrec, recbuf, len);
    memset(np->pagnam, 0, PNMSIZ);
    strncpy(np->pagnam, newpage, PNMSIZ);
    memset(np->modnam, 0, MNMSIZ);
    strncpy(np->modnam, module, MNMSIZ);
    memset(np->mnuttl, 0, TITLSZ);
    strncpy(np->mnuttl, title, TITLSZ-1);
    memset(np->cmdstg, 0, CMDSIZ);
    newlen=len;
    st=btrcall(B_INSERT, posblk, newrec, &newlen, &key, sizeof(key), 0);
    if (st != 0) {
        printf("Insert of page %s failed (status %d).\n", newpage, st);
        return(0);
    }
    printf("Added page %s (module %s, parent %s).\n", newpage, module,
           parent);

    /* append a selection entry to the parent page                          */
    makekey(&key, parent);
    len=MAXREC;
    st=bget(B_GETEQ, recbuf, &len, &key, sizeof(key));
    if (st != 0) {
        printf("Parent page %s not found (status %d).\n", parent, st);
        return(0);
    }
    if (pg->npages >= MAXSEL) {
        printf("Parent page %s is full (%d selections).\n", parent, MAXSEL);
        return(0);
    }
    for (i=0 ; i < pg->npages ; i++) {
        if (pg->page[i].selchr == selchr) {
            printf("Select character %c is taken on %s; pick another.\n",
                   selchr, parent);
            return(0);
        }
    }

    /* model the new selection on an existing one, then overwrite the      */
    /* fields that identify it                                             */
    pl=&pg->page[pg->npages];
    if (pg->npages > 0) {
        memcpy(pl, &pg->page[0], sizeof(*pl));
    }
    else {
        memset(pl, 0, sizeof(*pl));
        pl->optdsp='D';
    }
    pl->position=pg->npages;
    pl->selchr=selchr;
    memset(pl->iconame, 0, FNSIZE);
    memset(pl->destpage, 0, PNMSIZ);
    strncpy(pl->destpage, newpage, PNMSIZ);
    memset(pl->keyreq, 0, KEYSIZ);
    memset(pl->shortd, 0, SHDSIZ);
    strncpy(pl->shortd, title, SHDSIZ-1);
    memset(pl->longd, 0, LNDSIZ);
    strncpy(pl->longd, title, LNDSIZ-1);
    pg->npages++;

    len=(DWORD)(offsetof(struct mnupag, page)
                +(size_t)pg->npages*sizeof(struct pglink));
    st=btrcall(B_UPDATE, posblk, recbuf, &len, &key, sizeof(key), 0);
    if (st != 0) {
        printf("Update of parent %s failed (status %d).\n", parent, st);
        return(0);
    }
    printf("Added selection [%c] on %s -> %s.\n", selchr, parent, newpage);
    return(1);
}

int
main(int argc, char *argv[])
{
    HMODULE h=LoadLibraryA("WBTRV32.DLL");
    DWORD len=0;
    int ok=0;

    if (h == NULL) {
        printf("WBTRV32.DLL not found; run from the BBS directory.\n");
        return(1);
    }
    btrcall=(BTRCALL_T)GetProcAddress(h, "BTRCALL");
    if (btrcall == NULL) {
        printf("WBTRV32.DLL has no BTRCALL entry point.\n");
        return(1);
    }
    if (!openTree()) {
        return(1);
    }

    if (argc >= 2 && _stricmp(argv[1], "INFO") == 0) {
        info();
        ok=1;
    }
    else if (argc >= 7 && _stricmp(argv[1], "ADD") == 0) {
        ok=add(argv[2], argv[3], argv[4], argv[5][0], argv[6]);
    }
    else {
        printf("usage:  MNUADD INFO\n");
        printf("        MNUADD ADD <newpage> <module> <template> <selchr>"
               " <title>\n");
        ok=1;
    }

    btrcall(B_CLOSE, posblk, NULL, &len, NULL, 0, 0);
    return(ok ? 0 : 1);
}
