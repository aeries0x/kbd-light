PREFIX ?= /usr
UNITDIR ?= $(PREFIX)/lib/systemd/system
USERUNITDIR ?= $(PREFIX)/lib/systemd/user
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -std=gnu11
SYSTEMD_LIBS ?= $(shell pkg-config --libs libsystemd 2>/dev/null || echo -lsystemd)

DL = display-light
DL_MODEL = $(DL)/model.c $(DL)/settings.c
DL_HEADERS = $(DL)/model.h $(DL)/settings.h $(DL)/panel.h $(DL)/profiles.h

all: aurora-kbd-light aurora-display-light

aurora-kbd-light: aurora-kbd-light.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< -lm

aurora-display-light: $(DL)/main.c $(DL)/panel.c $(DL_MODEL) $(DL_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(DL)/main.c $(DL)/panel.c $(DL_MODEL) \
		$(SYSTEMD_LIBS) -lm

display-light-test: $(DL)/test.c $(DL_MODEL) $(DL_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(DL)/test.c $(DL_MODEL) -lm

check: display-light-test
	./display-light-test $(DL)/fixtures

install: all
	install -Dm0755 aurora-kbd-light $(DESTDIR)$(PREFIX)/bin/aurora-kbd-light
	install -Dm0644 aurora-kbd-light.service $(DESTDIR)$(UNITDIR)/aurora-kbd-light.service
	install -Dm0755 aurora-display-light $(DESTDIR)$(PREFIX)/bin/aurora-display-light
	install -Dm0644 $(DL)/aurora-display-light.service \
		$(DESTDIR)$(USERUNITDIR)/aurora-display-light.service

clean:
	rm -f aurora-kbd-light aurora-display-light display-light-test

.PHONY: all check install clean
