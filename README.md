# Video Toaster switcher for AROS

Native AROS port of the NewTek Video Toaster 4.2 switcher (Switcher.main 4.06), by converting its 68k assembler to C.

Sources:
- NewTek's released source (DiscreetFX "AmigaVideoToasterComplete.zip" = github ianhan/OpenVideoToaster): 24 of the ~136
  switcher modules (its `switcher` drawer).
- The shipped binaries: emufan's Toasterless build (Aminet gfx/edit/VT42-Toasterless-0.1) and NewTek's 4.06 of 02-Apr-97
  (Flyer CD 4.2). The reference build is Toasterless `Switcher.main.AA`.

Nothing of NewTek's is in this repository or in a release: the Switcher draws its panel from the user's own Video Toaster
installation (see `dist/README`), and the reference binaries, their listings and the reference machine's disk stay local.

Building (on Linux, with an AROS cross compiler and SDK):
- Say where they are in `local.mk` (not in the repository) or on the command line:
  `SDK_x86_64 = <AROS build>/bin/pc-x86_64/AROS/Developer`, `CROSS_x86_64 = <crosstools>/x86_64-aros-`, and the same
  with `i386`. For the file streams and the recorder also `FFMPEG_TAR = <ffmpeg-8.1.2.tar.xz>` and
  `FFMPEG_DIFF = <AROS contrib>/MultiMedia/libs/ffmpeg/ffmpeg-8.1.2-aros.diff`.
- `make ffmpeg-x86_64` once (FFmpeg as a static library, LGPL parts only; without it the Switcher is built without file
  streams), then `make` for `build/aros-x86_64/`: `Switcher`, `PluginInfo` and `Plugins/*.tsplugin`. `make ffmpeg-i386`
  and `make aros-i386` for i386. `make release` packs both into `build/release/`.
- `make check` runs, on the host, the plug-ins' JPEG encoder against libjpeg and the reader and player of CG pages
  (`tests/pagetest.c`); `make host` builds `build/host/swpng`, which renders the panel to a PNG without AROS.

Plug-ins: inputs and outputs can be added as small libraries in `Toaster:Plugins`, loaded and replaced while the Switcher
runs. `include/tsplugin.h` is the interface (with `tsinput.h` and `tsoutput.h`), `plugins/bars.c` the shortest example,
and the Makefile's `.tsplugin` rule how one is built.

CG pages: the Toaster's character generator is a program of its own,
[ToasterCG for AROS](https://github.com/terminills/toastercg-aros); the pages its Save Page writes are croutons here
(`src/cgpage.c`).

Layout:
- `src/` the switcher (`src/aros/` what is AROS only: windows, inputs, outputs, sound, ARexx), `include/`, `plugins/`,
  `tests/`, `dist/` what a release carries besides the programs.
- `rig/` (local) FS-UAE reference machine (A4000/040 AGA NTSC, KS 3.1, WB 3.1, Toaster 4.2). `rig/vm.sh start rig/vt-a4000.fs-uae`,
  `rig/vm.sh shot <name>`, `rig/vm.sh stop`.
- `tools/hunk.py` hunk executable parser; `tools/dis.py` per-hunk disassembly with relocation symbols (H<hunk>_<offset>);
  `tools/srcparse.py` GenAm source parser; `tools/align.py` maps source labels to binary addresses;
  `tools/equates.py` recovers struct offsets/constants of the missing include files.
- `dis/aa/` (local, generated) results for Switcher.main.AA: `h00.s` (main code), `jumptable.tsv`, `lvo.tsv` (ToasterBase library: LVO, name, address),
  `labels.tsv` (source label -> address), `labels.tsv.pairs` (matched instructions), `equates.tsv` (symbol, value, votes, conflicts).

Regenerate (`M68K_OBJDUMP` names an objdump for m68k): `python3 tools/dis.py bin/Switcher.main.AA.toasterless dis/aa && python3 tools/align.py dis/aa/insns.tsv dis/aa/labels.tsv <src>/*.a && python3 tools/equates.py dis/aa/labels.tsv.pairs dis/aa/equates.tsv`

Licence: the code of this repository is under the AROS Public License 1.1 (`LICENSE`); the Initial Developer is terminills,
copyright (C) 2026. The switcher's logic was converted from NewTek's switcher (the Open Video Toaster source and the shipped
program); what is NewTek's in it stays NewTek's, on the terms of that release. FFmpeg, linked into a release's Switcher, is
under the LGPL 2.1 or later (a release's `LGPL` drawer has what that needs).
