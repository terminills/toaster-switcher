# Video Toaster switcher for AROS
#   make            AROS x86_64 (build/aros-x86_64/Switcher, PluginInfo and Plugins/)
#   make aros-i386  AROS i386
#   make ffmpeg-x86_64, make ffmpeg-i386   FFmpeg for the file streams and the recorder, once, before the above
#   make release    build/release/ToasterSwitcher-<version>.lha and .zip (tools/make-release.sh)
#   make host       host check tool build/host/swpng (renders the display to a PNG)
#   make check      the plug-ins' JPEG encoder against libjpeg, and CG pages as croutons
# SDK_<arch> is an AROS SDK (the AROS/Developer drawer of a build or of an installed system) and CROSS_<arch> the
# compiler prefix (.../x86_64-aros-). Give them on the command line or in local.mk, which is not in the repository.
-include local.mk
SDK_x86_64   ?=
SDK_i386     ?=
CROSS_x86_64 ?= x86_64-aros-
CROSS_i386   ?= i386-aros-
# FFmpeg: what tools/build-ffmpeg-aros.sh made for that arch (FFMPEG_TAR and FFMPEG_DIFF say from what). Without it
# the Switcher has no file streams and no recorder; FFMPEG_<arch>= leaves it out on purpose.
own_ffmpeg = $(if $(wildcard $(CURDIR)/build/ffmpeg-aros-$(1)/lib/libavcodec.a),$(CURDIR)/build/ffmpeg-aros-$(1))
FFMPEG_x86_64 ?= $(call own_ffmpeg,x86_64)
FFMPEG_i386 ?= $(call own_ffmpeg,i386)
# What a release carries for USB cameras while AROS's own uvc.class has no VHI library for each camera yet: that
# class built with the change (UVC_CLASS_<arch>), and the change against AROS's source (UVC_DIFF).
UVC_CLASS_x86_64 ?=
UVC_CLASS_i386 ?=
UVC_DIFF ?=
FFLIBS = -lavformat -lavcodec -lswscale -lswresample -lavutil
FFDEPS_i386 = -latomic

CORE   = src/hunkimg.c src/fgrender.c src/effect.c src/gfxfile.c src/skin.c src/editbar.c src/algofx.c src/framestore.c src/croutontags.c src/cgpage.c
AROS_SRC = src/aros/main.c src/aros/project.c src/aros/rexxhost.c src/aros/rexxcmd.c src/aros/rexxedit.c src/aros/rexxreq.c src/aros/cgprog.c src/aros/inputs.c src/aros/inputswin.c src/aros/controlswin.c src/aros/audioout.c src/aros/sink.c src/aros/record.c src/aros/outputs.c src/aros/plugins.c src/input_vhi.c
HDRS   = $(wildcard include/*.h)
CFLAGS_AROS = -std=gnu99 -O2 -Wall -Wextra -Werror -Iinclude
# what is built is stripped the way AROS strips its own programs
STRIPFLAGS = --strip-unneeded -R.comment

.PHONY: all aros-x86_64 aros-i386 ffmpeg-x86_64 ffmpeg-i386 test-plugins-x86_64 test-plugins-i386 release host check clean
all: aros-x86_64

# The plug-ins: each a library of its own for Toaster:Plugins, between the two halves of the frame
PLUGINS = bars matte netstream
PLUGIN_EXTRA_netstream = plugins/jpegenc.c

# A plug-in runs on the Switcher's C library (tsplugin.h): one that would open its own is not kept.
plugin_check = @if $(1)nm $(2) | grep -q '__aros_libset_\(StdC\|StdCIO\|PosixC\)Base'; then \
	echo "$(2): opens a C library of its own"; rm -f $(2); exit 1; fi

define aros_target
aros-$(1): build/aros-$(1)/Switcher build/aros-$(1)/PluginInfo $(foreach p,$(PLUGINS),build/aros-$(1)/Plugins/$(p).tsplugin)
build/aros-$(1)/Plugins/%.tsplugin: plugins/%.c plugins/tsplugin_lib.c plugins/tsplugin_end.c $(wildcard plugins/*.[ch]) $(HDRS)
	@mkdir -p $$(@D)
	$(CROSS_$(1))gcc --sysroot=$(SDK_$(1)) $(CFLAGS_AROS) -nostartfiles -o $$@ plugins/tsplugin_lib.c $$< $$(PLUGIN_EXTRA_$$*) plugins/tsplugin_end.c -lautoinit -llibinit
	$$(call plugin_check,$(CROSS_$(1)),$$@)
	$(CROSS_$(1))strip $(STRIPFLAGS) $$@
# plug-ins only the tests use: more streams than the list shows at once, and Colour bars as a later version
test-plugins-$(1): build/aros-$(1)/TestPlugins/shades.tsplugin build/aros-$(1)/TestPlugins/bars.tsplugin
build/aros-$(1)/TestPlugins/shades.tsplugin: tests/plugins/shades.c plugins/tsplugin_lib.c plugins/tsplugin_end.c $(HDRS)
	@mkdir -p $$(@D)
	$(CROSS_$(1))gcc --sysroot=$(SDK_$(1)) $(CFLAGS_AROS) -nostartfiles -o $$@ plugins/tsplugin_lib.c $$< plugins/tsplugin_end.c -lautoinit -llibinit
	$$(call plugin_check,$(CROSS_$(1)),$$@)
	$(CROSS_$(1))strip $(STRIPFLAGS) $$@
build/aros-$(1)/TestPlugins/bars.tsplugin: plugins/bars.c plugins/tsplugin_lib.c plugins/tsplugin_end.c $(HDRS)
	@mkdir -p $$(@D)
	sed -e 's/"bars.tsplugin", 1, 0/"bars.tsplugin", 1, 1/' -e 's/"Colour bars", len/"Colour bars 1.1", len/' $$< > $$(@D)/bars.c
	$(CROSS_$(1))gcc --sysroot=$(SDK_$(1)) $(CFLAGS_AROS) -nostartfiles -o $$@ plugins/tsplugin_lib.c $$(@D)/bars.c plugins/tsplugin_end.c -lautoinit -llibinit
	$$(call plugin_check,$(CROSS_$(1)),$$@)
	$(CROSS_$(1))strip $(STRIPFLAGS) $$@
build/aros-$(1)/PluginInfo: tools/aros/plugininfo.c $(HDRS)
	@mkdir -p $$(@D)
	$(CROSS_$(1))gcc --sysroot=$(SDK_$(1)) $(CFLAGS_AROS) -o $$@ $$< -lpthread
	$(CROSS_$(1))strip $(STRIPFLAGS) $$@
build/aros-$(1)/Switcher: $(AROS_SRC) $(CORE) $(HDRS) src/input_file.c src/output_file.c
	@test -e $(SDK_$(1))/include/exec/types.h || { echo "SDK_$(1) is not an AROS SDK: '$(SDK_$(1))'"; exit 1; }
	@mkdir -p $$(@D)
	$(CROSS_$(1))gcc --sysroot=$(SDK_$(1)) $(CFLAGS_AROS) -o $$@ $(AROS_SRC) $(CORE) $(if $(FFMPEG_$(1)),-DHAVE_FFMPEG -idirafter $(FFMPEG_$(1))/include src/input_file.c src/output_file.c) -lcybergraphics -lgadtools -lreqtoolsstubs -lrexxsyslib $(if $(FFMPEG_$(1)),-L$(FFMPEG_$(1))/lib $(FFLIBS) $(FFDEPS_$(1))) -lpthread -lm
	$(CROSS_$(1))strip $(STRIPFLAGS) $$@
ffmpeg-$(1):
	SDK_$(1)=$(SDK_$(1)) CROSS_$(1)=$(CROSS_$(1)) FFMPEG_TAR=$(FFMPEG_TAR) FFMPEG_DIFF=$(FFMPEG_DIFF) tools/build-ffmpeg-aros.sh $(1)
endef
$(eval $(call aros_target,x86_64))
$(eval $(call aros_target,i386))

release:
	UVC_CLASS_x86_64=$(UVC_CLASS_x86_64) UVC_CLASS_i386=$(UVC_CLASS_i386) UVC_DIFF=$(UVC_DIFF) tools/make-release.sh

host: build/host/swpng
build/host/swpng: tools/swpng.c $(CORE) $(HDRS)
	@mkdir -p $(@D)
	cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude -o $@ tools/swpng.c $(CORE) -lpng -lm

# the plug-ins' JPEG encoder against libjpeg (needs python3 with Pillow), and CG pages as croutons
check: build/host/jpegtest build/host/pagetest
	rm -rf build/host/jpeg && mkdir -p build/host/jpeg
	build/host/jpegtest build/host/jpeg
	python3 tests/jpegcheck.py build/host/jpeg
	build/host/pagetest
build/host/pagetest: tests/pagetest.c $(CORE) $(HDRS)
	@mkdir -p $(@D)
	cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -o $@ tests/pagetest.c $(CORE) -lm
build/host/jpegtest: tests/jpegtest.c plugins/jpegenc.c plugins/jpegenc.h
	@mkdir -p $(@D)
	cc -std=c99 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ tests/jpegtest.c plugins/jpegenc.c -lm

clean:
	rm -rf build
