
# vim: ft=make noexpandtab

showorb: show.c
	$(CC) -o $@ $< -lmosquitto

clean:
	rm -f showorb

