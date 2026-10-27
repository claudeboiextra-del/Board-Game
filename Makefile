# Board Game - PSL1GHT / Tiny3D
TARGET		:= boardgame
BUILD		:= build
SOURCES		:= src
INCLUDES	:= include

PS3DEV		?= /usr/local/ps3dev
PSL1GHT		?= $(PS3DEV)
PORTLIBS	?= $(PS3DEV)/portlibs/ppu

include $(PSL1GHT)/ppu_rules

INCLUDE		:= -I$(INCLUDES) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include
CFLAGS		:= -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS		:= $(MACHDEP) -L$(PORTLIBS)/lib -ltiny3d -lrsx -lgcm_sys -lio -lsysutil -laudio -lrt -llv2 -lm -lsysmodule -lc

GAME_PARTS := $(SOURCES)/game_part_0.c $(SOURCES)/game_part_1.c $(SOURCES)/game_part_2.c $(SOURCES)/game_part_3.c

$(SOURCES)/game.c: $(GAME_PARTS)
	cat $(GAME_PARTS) > $@

CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CFILES		:= $(filter-out game_part_%.c,$(CFILES))
OFILES		:= $(addprefix $(BUILD)/,$(CFILES:.c=.o))

.PHONY: all clean

all: $(SOURCES)/game.c $(BUILD)/$(TARGET).elf

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	$(CC) $(OFILES) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD)
