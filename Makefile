all: gpasted gpaste

gpaste: src/gpaste.vala
	valac-0.12 --pkg gio-2.0 --pkg posix src/gpaste.vala -o gpaste

gpasted: src/gpasted/gpasted.vala src/gpasted/history.vala src/gpasted/clipboard.vala
	valac-0.12 --pkg gtk+-3.0 --pkg gio-2.0 --pkg posix src/gpasted/gpasted.vala src/gpasted/history.vala src/gpasted/clipboard.vala -o gpasted

clean:
	rm -f gpasted gpastec
