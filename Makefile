# Sky Circuit - PSL1GHT / Tiny3D Board Game
# Target: PS3 firmware 4.92 + HEN

TARGET		:= skycircuit
BUILD		:= build
SOURCES		:= src
INCLUDES	:= include
DATA		:= assets

# PS3 toolchain
include $(PSL1GHT)/ppu_rules

CFLAGS		:= -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE) -I$(PORTLIBS)/include
CXXFLAGS	:= $(CFLAGS)
LDFLAGS		:= $(MACHDEP) -L$(PORTLIBS)/lib -ltiny3d -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm -lsysmodule -lc

CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES		:= $(addprefix $(BUILD)/,$(CFILES:.c=.o) $(CPPFILES:.cpp=.o))

INCLUDE		:= -I$(INCLUDES) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include

.PHONY: all clean

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).self

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	$(CC) $(OFILES) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).self: $(BUILD)/$(TARGET).elf
	$(SPRXLINKER) $< $@

clean:
	rm -rf $(BUILD)

install: all
	cp $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).self .
