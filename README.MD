# BloX

An ANSI falling-block puzzle game for The Major BBS v10.

BloX brings fast, smooth, keyboard-responsive falling-block gameplay to the
BBS door environment, with persistent player statistics, multi-period high
score leaderboards, and extensive Sysop configuration.

## Overview

Players arrange falling blocks to complete horizontal lines. Completed lines
disappear and earn points. Every ten cleared lines advances the level, and
each level increases the falling speed. The game ends when the stack reaches
the top of the playfield.

BloX is rendered entirely in ANSI with differential screen updates, so it
stays smooth and flicker-free even over slower telnet connections.

## Features

* Classic 10 x 20 playfield with the seven standard block shapes
* Modern 7-bag piece randomizer for fair piece distribution
* Soft drop, hard drop, next-piece preview, and pause
* Level progression with increasing speed and score multipliers
* Combo and back-to-back clear bonuses
* Persistent per-player statistics (games, lines, levels, play time, more)
* Daily, weekly, monthly, and all-time Top 25 leaderboards
* Hall of Fame
* Sysop-configurable gameplay options
* Smooth, low-bandwidth ANSI rendering with differential updates

## Requirements

* The Major BBS v10
* ANSI-capable terminal (users without ANSI cannot play)

## Installation

1. Copy the contents of the release package into your BBS directory.
2. Run the module installation as described in the release notes.
3. Configure options with the offline configuration editor (WGSCNF).

See the release package README.TXT for detailed steps.

## Configuration

All options are available in the offline configuration editor under the
BloX option set, including starting level, hard drop, ghost piece,
next-piece preview, sounds, and leaderboard settings.

## Version

BloX v1.0.0 — initial release.

See CHANGELOG.MD for version history.

## License

MIT License — see the LICENSE file.

Copyright (c) Sysop Network

## Support

BloX is developed and maintained by Mark Laudenbach at Sysop Network.
