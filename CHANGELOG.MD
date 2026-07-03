# BloX Changelog

All notable changes to BloX are documented in this file.

The format follows Keep a Changelog conventions and the project follows
Semantic Versioning.

## [Unreleased]

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
* Console test harnesses for the engine and renderer.

### Fixed

* BLXHALL.DAT is created with a 2048-byte Btrieve page so the Hall of
  Fame record fits (previously caused a create-error crash at startup).
* The single Hall of Fame record is fetched by stepping to the first
  record rather than by key equality, fixing a duplicate-key crash on
  the second and later server starts.
