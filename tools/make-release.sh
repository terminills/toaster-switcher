#!/bin/bash
# Release archives: build/release/ToasterSwitcher-<version>.lha (stored, tools/mklha.py) and .zip with, for each
# architecture, the Switcher, PluginInfo and the Plugins drawer; the README and prefs example from dist/; and an LGPL
# drawer (FFmpeg's licence, build configuration and AROS patch, and the switcher's object files with their link
# commands, for relinking against another FFmpeg). Needs make ffmpeg-x86_64 and make ffmpeg-i386 done before.
# Nothing of a Video Toaster installation goes in: whoever runs the Switcher brings their own.
set -eu
R=$(cd "$(dirname "$0")/.." && pwd)
cd "$R"
V=$(sed -n 's/.*\$VER: Switcher \([0-9.]*\) .*/\1/p' src/aros/main.c)
D=build/release/ToasterSwitcher
rm -rf build/release; mkdir -p $D/LGPL
for A in x86_64 i386; do
  [ -e build/ffmpeg-aros-$A/ffmpeg-aros.diff ] || { echo "no FFmpeg for $A: make ffmpeg-$A"; exit 1; }
done
rm -f build/aros-x86_64/Switcher build/aros-i386/Switcher
make aros-x86_64 aros-i386 > build/release-make.log 2>&1
for A in x86_64 i386; do
  mkdir -p $D/$A/Plugins $D/LGPL/objects-$A
  cp build/aros-$A/Switcher build/aros-$A/PluginInfo $D/$A/
  cp build/aros-$A/Plugins/*.tsplugin $D/$A/Plugins/
  # the same compile as the Makefile, one object per source, and the link that makes Switcher from them
  CMD=$(make -n -B aros-$A | grep -- '-o build/aros-'$A'/Switcher')
  SRCS=$(echo "$CMD" | tr ' ' '\n' | grep '\.c$')
  FLAGS=$(echo "$CMD" | sed 's/ -o build[^ ]*Switcher / /' | tr ' ' '\n' | grep -v '\.c$' | grep -v -- '^-[lL]' | tr '\n' ' ')
  LIBS=$(echo "$CMD" | tr ' ' '\n' | grep -- '^-[lL]' | tr '\n' ' ')
  CC=$(echo "$FLAGS" | cut -d' ' -f1)
  OBJS=""
  for S in $SRCS; do
    O=$D/LGPL/objects-$A/$(basename ${S%.c}).o
    $FLAGS -c -o $O $S
    OBJS="$OBJS $(basename $O)"
  done
  SDK=$(echo "$FLAGS" | tr ' ' '\n' | sed -n 's/^--sysroot=//p')
  echo "$(basename $CC) --sysroot=$SDK -o Switcher$OBJS $LIBS" | sed -e 's/  */ /g' -e "s#$SDK#<AROS SDK>#g" \
      -e "s#$R/build/ffmpeg-aros-$A#<FFmpeg>#g" > $D/LGPL/objects-$A/link.txt
  CROSS=${CC%gcc}
  grep -h 'define FFMPEG_CONFIGURATION' build/ffmpeg-obj-$A/config.h | sed -e 's/#define FFMPEG_CONFIGURATION //' \
      -e "s#$SDK#<AROS SDK>#g" -e "s#$CROSS#$(basename $CROSS)#g" -e "s#$R/build/ffmpeg-aros-$A.new#<FFmpeg>#g" > $D/LGPL/ffmpeg-configure-$A.txt
done
cp build/ffmpeg-aros-i386/COPYING.LGPLv2.1 $D/LGPL/
cp build/ffmpeg-aros-i386/ffmpeg-aros.diff $D/LGPL/ffmpeg-8.1.2-aros.diff
cp tools/build-ffmpeg-aros.sh $D/LGPL/
cat > $D/LGPL/README <<EOT
FFmpeg 8.1.2 (https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz) is linked statically into both Switchers under the
GNU Lesser General Public License 2.1 or later (COPYING.LGPLv2.1). It was built with build-ffmpeg-aros.sh:
FFmpeg's source with AROS contrib's patch (ffmpeg-8.1.2-aros.diff) and the configuration in ffmpeg-configure-<arch>.txt.
objects-<arch>/ holds the switcher's own object files; link.txt is the command that linked them with the AROS SDK and
FFmpeg's libraries into Switcher (put your own SDK and FFmpeg where it says <AROS SDK> and <FFmpeg>), so it can be
relinked against a modified FFmpeg.
EOT
cp dist/README dist/Inputs.example LICENSE $D/
(cd build/release && python3 "$R/tools/mklha.py" ToasterSwitcher-$V.lha ToasterSwitcher && zip -qr ToasterSwitcher-$V.zip ToasterSwitcher)
ls -la build/release/ToasterSwitcher-$V.lha build/release/ToasterSwitcher-$V.zip
