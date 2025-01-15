
# vim: ft=make noexpandtab

ifeq ($(BUILD),$(DEBUG))
CFLAGS := -g
else
CFLAGS := -O2 -s -DNDEBUG
endif

showorb: show.c
	$(CC) $(CFLAGS) -o $@ $< -lmosquitto

clean:
	rm -f showorb

