# ----------------------------
# Calculator app (CE C toolchain)
# ----------------------------

VERSION      := $(shell scripts/version.sh)

NAME         = PinaCAS
ICON         = iconc.png
DESCRIPTION  = "PinaCAS v$(VERSION)"
APPLICATION  = YES
ALLOCATOR    = CUSTOM

CFLAGS       = -std=c23 -Wall -Oz -Ilib -DUSE_32BIT_WORDS
CXXFLAGS     = -std=c++23 -fno-rtti -Wall -Oz -Ilib -DUSE_32BIT_WORDS
CPP_EXTENSION = cxx

EXTRA_CSOURCES = lib/imath/imath.c lib/imath/imrat.c

ifneq ($(filter debug,$(MAKECMDGOALS)),)
OBJDIR       = obj/debug
BINDIR       = bin/debug
endif

CEDEV_MAKEFILE := $(shell cedev-config --makefile 2>/dev/null)

ifneq ($(CEDEV_MAKEFILE),)
.DEFAULT_GOAL := package
include $(CEDEV_MAKEFILE)

# The description holds the version, which the installer shows when updating
$(OBJDIR)/icon.obj: src/version.h

# The app is installed on the calculator by running the installer program, which reads the app from AppVars
INSTALLER    = PINACAS
APPVAR_SIZE  = 65200

.PHONY: package
package: $(BINDIR)/$(NAME).b84
debug: package

$(BINDIR)/$(INSTALLER).8xp: $(wildcard lib/app_tools/installer/src/*)
	$(Q)$(MAKE) -C lib/app_tools/installer MAKEFLAGS= NAME=$(INSTALLER) APPVAR_PREFIX='"$(NAME)"' \
		APPVAR_SPLIT_SIZE=$(APPVAR_SIZE)
	$(Q)cp lib/app_tools/installer/bin/$(INSTALLER).8xp $@

$(BINDIR)/$(NAME).b84: $(BINDIR)/$(TARGET) $(BINDIR)/$(INSTALLER).8xp
	$(Q)rm -f $(BINDIR)/$(NAME).*.8xv
	$(Q)$(CONVBIN) --iformat 8ek --input $< --oformat 8xv-split --maxvarsize $(APPVAR_SIZE) \
		--output $(BINDIR)/$(NAME).8xv --name $(NAME)
	$(Q)$(CONVBIN) --iformat 8x $$(for f in $(BINDIR)/$(INSTALLER).8xp $(BINDIR)/$(NAME).*.8xv; do printf -- '--input %s ' "$$f"; done) \
		--oformat b84 --output $@
else
.PHONY: package clean
package:
	$(error The CE C toolchain was not found. Install it with scripts/install-toolchain.sh, or build for PC with "make pc")
clean:
	rm -rf obj bin
endif

# ----------------------------
# PC
# ----------------------------

PC_TARGET   = bin/pinacas
PC_CC      := $(if $(shell command -v clang 2>/dev/null),clang,gcc)
PC_CXX     := $(if $(shell command -v clang++ 2>/dev/null),clang++,g++)
PC_FLAGS    = -pedantic -g -DCOMPILE_PC -DDEBUG -DUSE_32BIT_WORDS -Wall -MMD -MP -I. -Ilib
PC_CFLAGS   = -std=c23 $(PC_FLAGS)
PC_CXXFLAGS = -std=c++23 -fno-rtti -fno-exceptions $(PC_FLAGS)
PC_LFLAGS   = -lm
PC_OBJDIR   = obj/pc
PC_SOURCES := $(wildcard src/*.cxx src/*/*.cxx src/*/*/*.cxx)
PC_LIBS    := lib/imath/imath.c lib/imath/imrat.c
PC_OBJECTS := $(PC_SOURCES:%.cxx=$(PC_OBJDIR)/%.o) $(PC_LIBS:%.c=$(PC_OBJDIR)/%.o)

.PHONY: pc check format

pc: $(PC_TARGET)

$(PC_TARGET): $(PC_OBJECTS)
	@mkdir -p $(@D)
	@$(PC_CXX) $(PC_OBJECTS) $(PC_LFLAGS) -o $@
	@echo "Linked $@"

$(PC_OBJDIR)/%.o: %.cxx
	@mkdir -p $(@D)
	@$(PC_CXX) $(PC_CXXFLAGS) -c $< -o $@
	@echo "Compiled $<"

$(PC_OBJDIR)/%.o: %.c
	@mkdir -p $(@D)
	@$(PC_CC) $(PC_CFLAGS) -c $< -o $@
	@echo "Compiled $<"

-include $(PC_OBJECTS:.o=.d)

check: $(PC_TARGET)
	$(PC_TARGET) test tests.txt

format:
	clang-format -i $(wildcard src/*.[ch] src/*/*.[ch] src/*/*/*.[ch] src/*.[ch]xx src/*/*.[ch]xx src/*/*/*.[ch]xx)
