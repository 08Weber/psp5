# psp5 – made by Mille Weber
TARGET = psp5
OBJS = src/main.o src/scan.o src/sfo.o src/iso.o src/launch.o \
       src/gfx.o src/image.o src/art.o src/loader.o src/ui.o \
       src/arkcfg.o src/plugins.o src/system.o src/settings.o src/testrun.o src/sound.o src/lang.o src/config.o src/boot.o src/music.o src/umd.o src/ark4stub.o

CFLAGS = -O2 -G0 -Wall -Wno-unused-parameter -Wno-stringop-truncation -Wno-format-truncation $(EXTRA) $(ARKFLAGS)
ASFLAGS = $(CFLAGS)

# ARK=4 bygger ARK-4-versionen: utan ARK-5:s ArkCtrl-bibliotek (finns inte i ARK-4)
ifeq ($(ARK),4)
ARKFLAGS = -DPSP5_ARK4
ARKCTRL =
else
ARKFLAGS =
ARKCTRL = -larkctrl
endif

LIBS = -lintrafont -lpng -lcisoread -llz4 -lz -lm \
       -lpspmp3 -lpspgu -lpspumd -lpsprtc -lpsppower -lpsputility -lpspusb -lpspusbstor -lpspaudio \
       -lpspsystemctrl_user $(ARKCTRL) -lpspkubridge

BUILD_PRX = 1
PSP_FW_VERSION = 660

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = psp5
PSP_EBOOT_ICON = res/ICON0.PNG
PSP_EBOOT_PIC1 = res/PIC1.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
