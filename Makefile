all:
	g++ -g -o mpack madspack.cc font.cc sprite.cc unpack.cc util/dump.c util/tgaify.cc
	g++ -g -o mconcat madsconcat.cc util/dump.c util/tgaify.cc

clean:
	rm -f *.fab *.st1 *.tga
