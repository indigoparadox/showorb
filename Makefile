
# vim: ft=make noexpandtab

CFLAGS := $(shell pkg-config libmosquitto --cflags)
LDFLAGS := $(shell pkg-config libmosquitto --libs)
ifeq ($(BUILD),$(DEBUG))
CFLAGS += -g
else
CFLAGS += -O2 -s -DNDEBUG
endif
INSTALLDIR :=
PREFIX := /usr/local
ETC := /etc
SYSTEMD := /etc/systemd/system
VER := 1.0

showorb: show.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: showorb
	mkdir -p $(INSTALLDIR)$(PREFIX)/share/showorb/icons
	install icons/* $(INSTALLDIR)$(PREFIX)/share/showorb/icons
	install -s showorb $(INSTALLDIR)$(PREFIX)/bin/showorb
	install -b showorb.conf.dist $(INSTALLDIR)$(ETC)/showorb.conf
	if [ "$(SYSTEMD)" != "" ]; then sed "s|^ExecStart=|ExecStart=$(PREFIX)/bin/showorb|g" showorb.service > $(INSTALLDIR)$(SYSTEMD)/showorb.service; fi

deb:
	make clean
	mkdir -p showorb-$(VER)/usr/bin
	mkdir -p showorb-$(VER)/etc/systemd/system
	mkdir -p showorb-$(VER)/DEBIAN
	make INSTALLDIR=showorb-$(VER) PREFIX=/usr install
	sed "s|^Version: 1.0|Version: $(VER)|g" control > showorb-$(VER)/DEBIAN/control
	dpkg-deb --build --root-owner-group showorb-$(VER)

clean:
	rm -rf showorb showorb-$(VER) showorb-$(VER).deb

