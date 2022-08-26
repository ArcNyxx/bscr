# lynx - screenshot utility
# Copyright (C) 2022 ArcNyxx
# see LICENCE file for licensing information

.POSIX:

include config.mk

SRC = lynx.c
OBJ = $(SRC:.c=.o)

all: lynx

$(OBJ): config.mk

.c.o:
	$(CC) $(CFLAGS) -c $<

lynx: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

clean:
	rm -f lynx $(OBJ) lynx-$(VERSION).tar.gz

dist: clean
	mkdir -p lynx-$(VERSION)
	cp -R README LICENCE Makefile config.mk lynx.1 $(SRC) lynx-$(VERSION)
	tar -cf - lynx-$(VERSION) | gzip -c > lynx-$(VERSION).tar.gz
	rm -rf lynx-$(VERSION)

install: all
	mkdir -p $(PREFIX)/bin $(MANPREFIX)/man1
	cp -f lynx $(PREFIX)/bin
	chmod 754 $(PREFIX)/bin/lynx
	sed 's/VERSION/$(VERSION)/g' < lynx.1 > $(MANPREFIX)/man1/lynx.1
	chmod 644 $(MANPREFIX)/man1/lynx.1

uninstall:
	rm -f $(PREFIX)/bin/lynx $(MANPREFIX)/man1/lynx.1

.PHONY: all clean dist install uninstall

