# Board Game - PSL1GHT / Tiny3D
# Target: PS3 firmware 4.92 + HEN

TARGET		:= boardgame
BUILD		:= build
SOURCES		:= src
INCLUDES	:= include
DATA		:= assets

PS3DEV		?= /usr/local/ps3dev
PSL1GHT		?= $(PS3DEV)
PORTLIBS	?= $(PS3DEV)/portlibs/ppu

include $(PSL1GHT)/ppu_rules

INCLUDE		:= -I$(INCLUDES) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include

CFLAGS		:= -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS	:= $(CFLAGS)
LDFLAGS		:= $(MACHDEP) -L$(PORTLIBS)/lib -ltiny3d -lrsx -lgcm_sys -lio -lsysutil -laudio -lrt -llv2 -lm -lsysmodule -lc

# Expand compressed game.c when z64 is present (sound overhaul)
ifneq ($(wildcard $(SOURCES)/game.c.z64),)
$(SOURCES)/game.c: $(SOURCES)/game.c.z64
	python3 -c "import zlib,base64; open('$@','wb').write(zlib.decompress(base64.b64decode(open('$<').read())))"
endif

CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES		:= $(addprefix $(BUILD)/,$(CFILES:.c=.o) $(CPPFILES:.cpp=.o))

.PHONY: all clean install

all: $(SOURCES)/game.c $(BUILD)/$(TARGET).elf

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	$(CC) $(OFILES) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD)

install: all
	cp $(BUILD)/$(TARGET).elf .
