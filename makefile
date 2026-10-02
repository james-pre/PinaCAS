# ----------------------------
# Makefile Options
# ----------------------------

NAME         = PCAS
COMPRESSED   = YES
ICON         = iconc.png
DESCRIPTION  = "PinaCAS"

CFLAGS       = -Wall -Oz -Ilib -DUSE_32BIT_WORDS
CXXFLAGS     = -Wall -Oz -Ilib -DUSE_32BIT_WORDS

EXTRA_CSOURCES = lib/imath/imath.c lib/imath/imrat.c

include $(shell cedev-config --makefile)
