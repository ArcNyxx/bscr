# bscr - screenshot utility
# Copyright (C) 2022 ArcNyxx
# see LICENCE file for licensing information

.POSIX:

include config.mk

SRC = bscr.c
OBJ = $(SRC:.c=.o)

all: bscr

$(OBJ): config.mk

.c.o:
	$(CC) $(CFLAGS) -c $<

bscr: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

clean:
	rm -f bscr $(OBJ) bscr-$(VERSION).tar.gz

dist: clean
	mkdir -p bscr-$(VERSION)
	cp -R README LICENCE Makefile config.mk bscr.1 $(SRC) bscr-$(VERSION)
	tar -cf - bscr-$(VERSION) | gzip -c > bscr-$(VERSION).tar.gz
	rm -rf bscr-$(VERSION)

install: all
	mkdir -p $(PREFIX)/bin $(MANPREFIX)/man1
	cp -f bscr $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/bscr
	sed 's/VERSION/$(VERSION)/g' < bscr.1 > $(MANPREFIX)/man1/bscr.1
	chmod 644 $(MANPREFIX)/man1/bscr.1

uninstall:
	rm -f $(PREFIX)/bin/bscr $(MANPREFIX)/man1/bscr.1

.PHONY: all clean dist install uninstall

