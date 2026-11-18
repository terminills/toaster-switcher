#!/bin/bash
# FFmpeg for the switcher's file streams on AROS: 8.1.2 with contrib's AROS patch, static, only the common
# video/audio decoders, parsers and demuxers and the file protocol (the contrib build has everything: 13 MB of code),
# and what a recording of Program is written with: Motion JPEG in an AVI.
# build-ffmpeg-aros.sh [x86_64|i386] -> build/ffmpeg-aros-<arch>/{include,lib}, which the Makefile picks up. The one
# that is there stays until the new one is complete.
# It wants what the Makefile wants, SDK_<arch> (an AROS SDK: .../AROS/Developer) and CROSS_<arch> (the compiler prefix:
# .../x86_64-aros-), and FFMPEG_TAR (ffmpeg-8.1.2.tar.xz from ffmpeg.org) and FFMPEG_DIFF (ffmpeg-8.1.2-aros.diff from
# AROS contrib, MultiMedia/libs/ffmpeg).
set -u
R=$(cd "$(dirname "$0")/.." && pwd)
A=${1:-x86_64}
case "$A" in
  x86_64) SDK=${SDK_x86_64:-} T=${CROSS_x86_64:-} ARCH=x86_64 ;;
  i386) SDK=${SDK_i386:-} T=${CROSS_i386:-} ARCH=x86_32 ;;
  *) echo "build-ffmpeg-aros.sh [x86_64|i386]"; exit 1 ;;
esac
TAR=${FFMPEG_TAR:-} DIFF=${FFMPEG_DIFF:-}
for v in "$SDK/include/exec/types.h" "${T}gcc" "$TAR" "$DIFF"; do
  [ -e "$v" ] || { echo "not there: '$v' (SDK_$A, CROSS_$A, FFMPEG_TAR and FFMPEG_DIFF say where)"; exit 1; }
done
CC="${T}gcc --sysroot=$SDK" AR=${T}ar RANLIB=${T}ranlib NM=${T}nm
S=$R/build/ffmpeg-src-$A B=$R/build/ffmpeg-obj-$A D=$R/build/ffmpeg-aros-$A P=$R/build/ffmpeg-aros-$A.new
L=$R/build/ffmpeg-logs-$A
rm -rf "$S" "$B" "$P"; mkdir -p "$S" "$B" "$L"
tar -xJf "$TAR" -C "$S" --strip-components=1 || exit 1
(cd "$S" && patch -p1 < "$DIFF" > "$L/patch.log" 2>&1) || { echo "patch failed"; exit 1; }
VDEC=h264,hevc,mpeg4,h263,mpeg2video,mpeg1video,mjpeg,prores,vp8,vp9,dvvideo,rawvideo,theora,png
ADEC=aac,aac_latm,mp3,mp3float,mp2,ac3,eac3,vorbis,opus,flac,alac,pcm_s16le,pcm_s16be,pcm_s24le,pcm_f32le,pcm_u8
DEMUX=mov,matroska,avi,mpegts,mpegps,flv,ogg,wav,mp3,aac,h264,hevc,m4v,mjpeg,dv,ivf
PARSE=h264,hevc,mpeg4video,mpegvideo,mjpeg,vp8,vp9,aac,aac_latm,mpegaudio,ac3,vorbis,opus,flac,dvbsub
ln -s "../ffmpeg-src-$A" "$B/src"        # configure takes the source as "src" then, and no path of this machine
(cd "$B" && src/configure --prefix="$P" --enable-cross-compile --arch=$ARCH --target-os=aros \
  --cc="$CC" --ar="$AR" --ranlib="$RANLIB" --nm="$NM" \
  --extra-cflags="-D__BSD_VISIBLE=1 -D_XOPEN_SOURCE=500 -D_GNU_SOURCE -Os" \
  --enable-static --disable-shared --disable-pic --disable-rpath --disable-x86asm --disable-doc --disable-debug \
  --disable-programs --disable-network --disable-iconv --disable-lzma --disable-bzlib --disable-zlib \
  --disable-vaapi --disable-vdpau --disable-xlib --disable-sdl2 --disable-libdrm --disable-avdevice --disable-avfilter \
  --disable-everything --enable-protocol=file --enable-decoder=$VDEC,$ADEC --enable-demuxer=$DEMUX \
  --enable-parser=$PARSE --enable-encoder=mjpeg --enable-muxer=avi --enable-swscale --enable-swresample > "$L/configure.log" 2>&1) || { echo "configure failed"; exit 1; }
(cd "$B" && make -j24 > "$L/make.log" 2>&1 && make install > "$L/install.log" 2>&1) || { echo "make failed"; exit 1; }
cp "$DIFF" "$P/ffmpeg-aros.diff" && cp "$S/COPYING.LGPLv2.1" "$P/" || exit 1      # what a release has to carry
rm -rf "$D" && mv "$P" "$D" || exit 1
ls -la "$D/lib"
echo FFMPEG-AROS-DONE
