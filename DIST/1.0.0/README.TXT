===========================================================================
  BloX  v1.0.0
  An ANSI falling-block puzzle game for The Major BBS v10
  Copyright (c) 2026 Sysop Network
===========================================================================

OVERVIEW

  BloX is a fast, smooth, keyboard-responsive falling-block game for the
  BBS door environment. Players arrange falling blocks to complete
  horizontal lines; completed lines disappear and score points. Every ten
  lines advances the level and speeds the pieces up. The game ends when
  the stack reaches the top of the well.

  Features include the seven standard block shapes, a modern 7-bag piece
  randomizer, soft drop, hard drop, next-piece preview, ghost piece,
  pause, combo and back-to-back bonuses, persistent player statistics,
  achievements, and an all-time Hall of Fame. All rendering is ANSI with
  differential screen updates, so play stays smooth over slow links.


PACKAGE CONTENTS

  SNBLOX.DLL   The game module (32-bit)
  SNBLOX.MDF   Module definition file
  SNBLOX.MSG   Message and configuration text
  SNBLOX.MCV   Compiled runtime message file
  README.TXT   This file
  SNBLOX.RLN   Release notes


REQUIREMENTS

  * The Major BBS v10
  * Players need an ANSI-capable terminal (non-ANSI users are told to
    enable ANSI and are returned to the menu).


INSTALLATION

  1. Shut the server down.
  2. Copy the package files into your BBS directory (default C:\BBSV10).
  3. Start the server. BloX registers itself and creates its two data
     files automatically on first run:
         SNBLOXPL.DAT  player statistics
         SNBLOX.DAT    the all-time Hall of Fame
     No separate database preparation is required.
  4. In the offline Menu Tree editor, add a module page that points a
     menu selection at the module named "BloX", so players can reach it.


CONFIGURATION

  Use the offline Configuration Editor (Configuration Options level) to
  adjust the BloX options:

    Starting level for new games        (1-30)
    Allow the hard drop                 (Yes/No)
    Show the ghost piece                (Yes/No)
    Show the Next piece preview         (Yes/No)
    Send sound effects (terminal bells) (Yes/No)
    Daily first-game bonus points       (0-100000)
    Minimum score for the Hall of Fame  (0-1000000)


PLAYING

  From the BloX menu:  P Play   I Instructions   S Statistics
                       H Hall of Fame            X Exit

  In game:  Arrow keys move/rotate/soft-drop, or A/D/W/S.
            SPACE hard drop, P pause, Q quit, ESC exit.


VERSION

  BloX v1.0.0 - initial release.


LICENSE

  MIT License.


SUPPORT

  BloX is developed and maintained by Mark Laudenbach at Sysop Network.
===========================================================================
