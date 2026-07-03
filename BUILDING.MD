# Building BloX

## Requirements

* Microsoft Visual Studio 2022 (or the 2022 Build Tools) with the
  **Desktop development with C++** workload and the **v143** toolset.
* The MBBS v10 module SDK. Point the `MBBS10SDK` variable at the SDK root
  (the folder containing `inc` and `lib`).
* A local test system of The Major BBS v10 (default `C:\BBSV10`) for
  deployment and testing.

BloX modules are **32-bit (x86)**. The Major BBS v10 does not load 64-bit
modules.

## Building the module DLL

From a Developer Command Prompt (or after running `vcvarsall.bat x86`):

```
msbuild SRC\SNBLOX.vcxproj /p:Configuration=Release /p:Platform=Win32 ^
        "/p:MBBS10SDK=<path to MBBS v10 SDK>"
```

The build produces `SRC\Release\SNBLOX.dll`. The project links
`wgserver_lib.lib` and `galgsbl_lib.lib` from the SDK, compiles with `/J`
(unsigned char) and 8-byte struct alignment, and exports `init__snblox`
through `SNBLOX_EXP.DEF`. Version information is compiled in from
`SNBLOX.RC`.

## Compiling the message file

The `.MSG` source lives in `SRC\SNBLOX.MSG` and already uses CRLF line
endings with real ESC (0x1B) bytes in its ANSI sequences. Compile it with
the BBS tool `WGSMSX`, run from the BBS root:

```
WGSMSX.exe SNBLOX.MSG SNBLOX.MCV SNBLOX.H
```

This emits `SNBLOX.MCV` (the runtime message file) and a `SNBLOX.H`
message-number header. The committed `SRC\SNBLOX.H` must stay in step with
that generated header; regenerate and compare after editing the `.MSG`.

## Running the core tests

The game engine and renderer are free of BBS dependencies and have
console test harnesses:

```
cl /nologo /J /W3 /EHsc /ISRC TESTS\CORETEST.CPP SRC\BLXCORE.CPP
CORETEST.EXE

cl /nologo /J /W3 /EHsc /ISRC TESTS\ANSITEST.CPP SRC\BLXCORE.CPP SRC\BLXANSI.CPP
ANSITEST.EXE
```

Both print `PASS` and exit 0 on success.

## Deploying to a test BBS

1. Shut the server down completely. The running server holds its data
   files open; make sure both `wgserver` and the Btrieve engine
   `W32MKDE` have exited before copying files.
2. Copy `SNBLOX.DLL`, `SNBLOX.MDF`, and `SNBLOX.MSG` into the BBS
   directory (`C:\BBSV10`).
3. Compile the message file with `WGSMSX` as shown above.
4. Add a module page for BloX in the offline **Menu Tree** editor
   (`wgsmtree`), pointing a selection at module `BloX`.
5. Start the server with `wgsappgo.EXE`. BloX creates its three Btrieve
   data files (`BLXPLYR.DAT`, `BLXSCOR.DAT`, `BLXHALL.DAT`) on first run;
   no separate database preparation is needed.

## Packaging a release

Assemble `DIST\<version>\` with the deployment files only, using uppercase
names: `SNBLOX.DLL`, `SNBLOX.MDF`, `SNBLOX.MSG`, plus `README.TXT` and any
release notes. Do not include source, project files, or internal notes.
