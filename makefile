# ----------------------------
# Calculator (CE C toolchain)
# ----------------------------

NAME         = PCAS
COMPRESSED   = YES
ICON         = iconc.png
DESCRIPTION  = "PinaCAS"

CFLAGS       = -Wall -Oz -Ilib -DUSE_32BIT_WORDS
CXXFLAGS     = -Wall -Oz -Ilib -DUSE_32BIT_WORDS

EXTRA_CSOURCES = lib/imath/imath.c lib/imath/imrat.c

$(shell scripts/version.sh)

CEDEV_MAKEFILE := $(shell cedev-config --makefile 2>/dev/null)

ifneq ($(CEDEV_MAKEFILE),)
.DEFAULT_GOAL := build
include $(CEDEV_MAKEFILE)
else
.PHONY: build clean
build:
	$(error The CE C toolchain was not found. Install it with scripts/install-toolchain.sh, or build for PC with "make pc")
clean:
	rm -rf obj bin
endif

# ----------------------------
# PC
# ----------------------------

PC_TARGET  = bin/pinacas
PC_CC      = gcc
PC_CFLAGS  = -std=c17 -pedantic -g -DCOMPILE_PC -DDEBUG -DUSE_32BIT_WORDS -Wall -MMD -MP -I. -Ilib
PC_LFLAGS  = -lm
PC_OBJDIR  = obj/pc
PC_SOURCES := $(wildcard src/*.c src/*/*.c) lib/imath/imath.c lib/imath/imrat.c
PC_OBJECTS := $(PC_SOURCES:%.c=$(PC_OBJDIR)/%.o)

.PHONY: pc check format

pc: $(PC_TARGET)

$(PC_TARGET): $(PC_OBJECTS)
	@mkdir -p $(@D)
	@$(PC_CC) $(PC_OBJECTS) $(PC_LFLAGS) -o $@
	@echo "Linked $@"

$(PC_OBJECTS): $(PC_OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	@$(PC_CC) $(PC_CFLAGS) -c $< -o $@
	@echo "Compiled $<"

-include $(PC_OBJECTS:.o=.d)

check: $(PC_TARGET)
	$(PC_TARGET) test tests.txt

format:
	clang-format -i $(wildcard src/*.[ch] src/*/*.[ch])
