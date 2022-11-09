# bscr - screenshot utility
# Copyright (C) 2022 ArcNyxx
# see LICENCE file for licensing information

VERSION = 1.0.0

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

WPROFILE = -Wall -Wextra -Wstrict-prototypes -Wmissing-declarations -Wshadow \
-Wswitch-default -Wunreachable-code -Wcast-align -Wpointer-arith -Wcast-qual \
-Wbad-function-cast -Winline -Wundef -Wnested-externs -Wwrite-strings \
-Wno-unused-parameter -Wfloat-equal -Wpedantic
STD = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
LIB = -lpng -lxcb -lxcb-xkb -lxcb-shape -lxcb-xfixes -lxcb-xinerama \
	-lxkbcommon -lxkbcommon-x11

CFLAGS = $(WPROFILE) $(STD) -Os
LDFLAGS = $(LIB)
