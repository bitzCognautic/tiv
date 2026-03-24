CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
LDFLAGS ?=

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
APPDIR ?= $(PREFIX)/share/applications
ICONDIR ?= $(PREFIX)/share/icons/hicolor/scalable/apps
DISTDIR ?= dist
VERSION ?= $(shell cat VERSION 2>/dev/null || echo 0.1.0)

SRC := src/main.c
BIN := tiv

.PHONY: all clean install uninstall dist

all: $(BIN)

GTK_CFLAGS := $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs gtk4 2>/dev/null)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -Isrc $(GTK_CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(GTK_LIBS)

clean:
	rm -f $(BIN)

install: $(BIN)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 "$(BIN)" "$(DESTDIR)$(BINDIR)/$(BIN)"
	install -d "$(DESTDIR)$(APPDIR)"
	install -m 0644 packaging/tiv.desktop "$(DESTDIR)$(APPDIR)/tiv.desktop"
	install -d "$(DESTDIR)$(ICONDIR)"
	install -m 0644 packaging/tiv.svg "$(DESTDIR)$(ICONDIR)/tiv.svg"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(BIN)"
	rm -f "$(DESTDIR)$(APPDIR)/tiv.desktop"
	rm -f "$(DESTDIR)$(ICONDIR)/tiv.svg"

dist:
	rm -rf "$(DISTDIR)"
	mkdir -p "$(DISTDIR)/tiv-$(VERSION)"
	cp -a Makefile README.md LICENSE VERSION src packaging "$(DISTDIR)/tiv-$(VERSION)/"
	tar -C "$(DISTDIR)" -czf "$(DISTDIR)/tiv-$(VERSION).tar.gz" "tiv-$(VERSION)"
