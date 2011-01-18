all: gpasted

gpasted: src/gpasted.vala
	valac-0.12 --pkg gtk+-3.0 --pkg posix src/gpasted.vala -o gpasted

clean:
	rm -f gpasted
