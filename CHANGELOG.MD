# BloX Changelog

All notable changes to BloX are documented in this file.

The format follows Keep a Changelog conventions and the project follows
Semantic Versioning.

## [Unreleased]

### Changed

* Simplified the high-score system to a single all-time Hall of Fame.
  The daily, weekly, and monthly boards and their backing score-log file
  have been removed; the menu now offers just Play, Instructions,
  Statistics, and Hall of Fame. The module now uses two Btrieve files
  (player statistics and the Hall of Fame) instead of three.
* Renamed the Hall of Fame data file to SNBLOX.DAT.
* Boot audit line now reads "BloX v1.0.0 Stacked" / "By SysopNetwork.com".

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

* BLXHALL.DAT is created with a 2048-byte Btrieve page so the Hall of
  Fame record fits (previously caused a create-error crash at startup).
* The single Hall of Fame record is fetched by stepping to the first
  record rather than by key equality, fixing a duplicate-key crash on
  the second and later server starts.
* All-time Hall of Fame scores now persist. The record was read with a
  physical step before being updated, but Btrieve requires an update to
  immediately follow a get (acquire); the write was silently discarded.
  The Hall record and score-log deletions now acquire before writing.
