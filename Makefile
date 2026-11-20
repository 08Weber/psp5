# PSP5 REMAKE – fork by medhatk (based on psp5 by 08Weber)
TARGET = psp5
OBJS = src/main.o src/scan.o src/sfo.o src/iso.o src/launch.o \
       src/gfx.o src/image.o src/art.o src/loader.o src/ui.o \
       src/arkcfg.o src/plugins.o src/system.o src/settings.o src/testrun.o src/sound.o src/lang.o src/config.o src/boot.o src/music.o src/umd.o src/theme.o src/osd.o src/files.o src/preview.o src/m4a.o src/datetime.o src/info.o src/lastplayed.o src/savedata.o src/ark4stub.o \
       src/video/video.o src/video/mp4.o src/video/mpegps.o src/video/h264sei.o src/video/unit.o src/video/aac.o src/video/downmix.o src/video/tscale.o \
       src/video/h264dec.o src/video/h264rec.o src/video/h264lf.o src/video/meboot.o src/video/hwavc.o src/video/hwpipe.o src/video/mpegvsh.o src/video/videomem.o src/video/logs.o src/video/popup.o src/video/videoplay.o

CFLAGS = -O2 -G0 -Wall -Wno-unused-parameter -Wno-stringop-truncation -Wno-format-truncation $(EXTRA) $(ARKFLAGS)
ASFLAGS = $(CFLAGS)

# videoplay.c includes the application language header from src/.
src/video/videoplay.o: CFLAGS += -Isrc

# ARK=4 bygger ARK-4-versionen: utan ARK-5:s ArkCtrl-bibliotek (finns inte i ARK-4)
ifeq ($(ARK),4)
ARKFLAGS = -DPSP5_ARK4
ARKCTRL =
else
ARKFLAGS =
ARKCTRL = -larkctrl
endif

LIBS = -lintrafont -lpng -ljpeg -lcisoread -llz4 -lz -lm \
       -lpspmp3 -lpspaudiocodec -lpspmpeg -lpspmpegbase -lpspatrac3 -lpspgu -lpspumd -lpsprtc -lpsppower -lpsputility -lpspusb -lpspusbstor -lpspaudio -lpspwlan -lpspreg \
       -lpspsystemctrl_user $(ARKCTRL) -lpspkubridge

BUILD_PRX = 1
PSP_FW_VERSION = 660

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP5 REMAKE
PSP_EBOOT_ICON = res/ICON0.PNG
PSP_EBOOT_PIC1 = res/PIC1.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
