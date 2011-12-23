CFLAGS = -g -Wall -Werror
LDFLAGS = -lpng -ljpeg

all: imgcssmap

imgcssmap: imgcssmap.o

imgcssmap.o: imgcssmap.c

clean:
	rm -f imgcssmap.o imgcssmap
