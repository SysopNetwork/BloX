# BloX Changelog

All notable changes to BloX are documented in this file.

The format follows Keep a Changelog conventions and the project follows
Semantic Versioning.

## [1.0.0] - 2026-07-02

Initial release. Built, deployed, and verified on a live Major BBS v10
test system; gameplay and score persistence confirmed over telnet. A BBS
deployment package is provided in DIST/1.0.0.

### Changed

* Simplified the high-score system to a single all-time Hall of Fame.
  The daily, weekly, and monthly boards and their backing score-log file
  have been removed; the menu now offers just Play, Instructions,
  Statistics, and Hall of Fame. The module now uses two Btrieve files
  (player statistics and the Hall of Fame) instead of three.
* Named the data files for a consistent SNBLOX prefix: the Hall of Fame
  is SNBLOX.DAT and player statistics are SNBLOXPL.DAT.
* Boot audit prints two lines: "BloX v1.0.0 Stacked" then
  "By SysopNetwork.com".

### Added

* Initial project structure and build environment.
* Core falling-block game engine: 10x20 playfield, seven shapes with
  wall-kick rotation, 7-bag randomizer, gravity and per-level speed,
  line clearing, classic scoring with combo, back-to-back, and
  perfect-clear bonuses, level progression, pause, and ghost piece.
* Differential ANSI renderer: playfield, side panels (score, level,
  lines, time, Next preview, best), controls help, combo indicator, and
  pause banner, repainting only changed cells for smooth play over slow
  links.
* MBBS module layer (SNBLOX): registration, ANSI main menu,
  character-mode gameplay driven by an 18 Hz heartbeat, game-over
  summary with achievements, statistics and leaderboard screens.
* Persistence: player statistics, a score log feeding daily/weekly/
  monthly leaderboards, and an all-time Hall of Fame, in self-creating
  Btrieve files.
* Sysop configuration options and a full ANSI message file.
* Console test harnesses for the engine and renderer, and a scripted
  telnet session driver for end-to-end testing.

### Fixed

Issues found and resolved during bring-up on the test system:

* The Hall of Fame file is created with a 2048-byte Btrieve page so its
  record fits (a smaller page caused a create-error crash at startup).
* The Hall of Fame record is fetched with an acquire (a Btrieve "get"),
  not a physical step. Btrieve requires updates and deletes to
  immediately follow a get; reading by step left scores silently
  unsaved and, earlier, caused a duplicate-key crash on restart.
