CFLAGS = -g -Wall -Werror
LDFLAGS = -lpng -ljpeg

all: imgcssmap

imgcssmap: imgcssmap.o

imgcssmap.o: imgcssmap.c

test: imgcssmap
	./imgcssmap -q 4 \
		-o a.png \
		-t test_files/a.css.tpl a.css \
		-t test_files/a.html.tpl a.html \
		-t test_files/test.txt.tpl test.txt \
		test_images/credit_card_icons/*.png \
		test_images/glyphicons/*.png \
		test_images/plastic_new_year/*/*.png \
		test_images/woody_social_icons/*.png
	H="$$(echo "<link rel='stylesheet' href='a.css' type='text/css'>"; cat a.html)" && \
	echo "$$H" > a.html

clean:
	rm -f imgcssmap.o imgcssmap a.css a.html a.png test.txt
