CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags xft)
LIBS = $(shell pkg-config --libs xft) -lX11
TARGET = qnote
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LIBS)
	strip $(TARGET)

clean:
	rm -f $(TARGET)

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -Dm644 qnote.desktop $(DESTDIR)$(PREFIX)/share/applications/qnote.desktop
	for s in 16 32 48 64 128; do \
	  if [ -f qnote$$s.png ]; then \
	    install -Dm644 qnote$$s.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${s}x$${s}/apps/qnote.png; \
	  fi \
	done
	if [ -f qnote.png ]; then \
	  install -Dm644 qnote.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/qnote.png; \
	fi

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/qnote.desktop
	for s in 16 32 48 64 128 256; do \
	  rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${s}x$${s}/apps/qnote.png; \
	done

.PHONY: all clean install uninstall
