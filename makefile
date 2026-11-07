# ----------------------------
# Makefile Options
# ----------------------------

NAME = ISAAC
ICON = icon.png
DESCRIPTION = "Mini Binding of Isaac (TI-83 Premium CE)"

COMPRESSED = YES

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
