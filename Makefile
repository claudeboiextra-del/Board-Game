# Board Game - PSL1GHT / Tiny3D
TARGET := boardgame
BUILD := build
SOURCES := src
INCLUDES := include
PS3DEV ?= /usr/local/ps3dev
PSL1GHT ?= $(PS3DEV)
PORTLIBS ?= $(PS3DEV)/portlibs/ppu
include $(PSL1GHT)/ppu_rules
INCLUDE := -I$(INCLUDES) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include
CFLAGS := -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS := $(MACHDEP) -L$(PORTLIBS)/lib -ltiny3d -lrsx -lgcm_sys -lio -lsysutil -laudio -lrt -llv2 -lm -lsysmodule -lc

.PHONY: all clean game_c

# Always expand compressed game before build
game_c:
	cat $(SOURCES)/game.c.z64.0 $(SOURCES)/game.c.z64.1 $(SOURCES)/game.c.z64.2 $(SOURCES)/game.c.z64.3 > $(SOURCES)/game.c.z64
	python3 -c "import zlib,base64; open('src/game.c','wb').write(zlib.decompress(base64.b64decode(open('src/game.c.z64').read())))"

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
OFILES := $(addprefix $(BUILD)/,$(CFILES:.c=.o))

all: game_c $(BUILD)/$(TARGET).elf

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD) game_c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	$(CC) $(OFILES) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD)
