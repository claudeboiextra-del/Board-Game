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

CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
OFILES		:= $(addprefix $(BUILD)/,$(CFILES:.c=.o))
HEADERS		:= $(wildcard $(INCLUDES)/*.h)

.PHONY: all clean

all: $(BUILD)/$(TARGET).elf

$(BUILD):
	@mkdir -p $@

# Compiler/linker output is echoed as usual; on failure the first errors are also
# emitted as GitHub Actions ::error:: annotations so they show up on the check run.
$(BUILD)/%.o: $(SOURCES)/%.c $(HEADERS) | $(BUILD)
	@echo "CC $<"; $(CC) $(CFLAGS) -c $< -o $@ > $@.log 2>&1; rc=$$?; cat $@.log; \
	if [ $$rc -ne 0 ]; then grep -E "error|No such file" $@.log | head -20 | sed 's/^/::error::/'; fi; exit $$rc

$(BUILD)/$(TARGET).elf: $(OFILES)
	@echo "LD $@"; $(CC) $(OFILES) $(LDFLAGS) -o $@ > $@.log 2>&1; rc=$$?; cat $@.log; \
	if [ $$rc -ne 0 ]; then grep -E "error|undefined reference|cannot find" $@.log | head -20 | sed 's/^/::error::/'; fi; exit $$rc

clean:
	rm -rf $(BUILD)
