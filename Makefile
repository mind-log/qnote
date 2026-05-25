CC = gcc
CFLAGS = -Wall -O3 -I/usr/include/freetype2 -I/usr/include/libpng16
LIBS = -lXft -lX11
TARGET = qnote
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LIBS)

clean:
	rm -f $(TARGET)

install: all
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -Dm644 qnote.desktop $(DESTDIR)$(PREFIX)/share/applications/qnote.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/qnote.desktop

.PHONY: all clean install uninstall
