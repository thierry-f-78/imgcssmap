#include <sys/time.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <png.h>
#include <jpeglib.h>

struct node {
	png_uint_32 width;
	png_uint_32 height;

	png_uint_32 surface;

	png_uint_32 dest_x;
	png_uint_32 dest_y;

	png_bytep *row_pointers;
};

struct surface {
	unsigned char used;
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
};

void usage()
{
	fprintf(stderr, 
	"\n"
	"imgcssmap -o output_image input_file [...]\n"
	"\n"
	"   -o output_image     image builded\n"
	"\n"
	);
}

static inline
void image_memory(struct node *n)
{
	int i;

	/* de la memoire pour charger l'image */
	n->row_pointers = calloc(sizeof(png_bytep), n->height);
	if (n->row_pointers == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	for (i=0; i<n->height; i++) {
		n->row_pointers[i] = calloc(n->width, 4);
		if (n->row_pointers[i] == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
	}
}

struct node *openjpg(const char *filename)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer;
	FILE *infile;
	unsigned long location = 0;
	int i = 0;
	int x;
	struct node *n;

	infile = fopen(filename, "r");
	if (infile == NULL) {
		fprintf(stderr, "Error opening jpeg file %s\n!", filename);
		return NULL;
	}

	/* on fabrique le noeud qui va contenir l'image */
	n = malloc(sizeof(struct node));
	if (n == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* here we set up the standard libjpeg error handler */
	cinfo.err = jpeg_std_error(&jerr);

	/* setup decompression process and source, then read JPEG header */
	jpeg_create_decompress(&cinfo);

	/* this makes the library read from infile */
	jpeg_stdio_src(&cinfo, infile);

	/* reading the image header which contains image information */
	jpeg_read_header(&cinfo, TRUE);

#if 0
	printf( "Color components per pixel: %d.\n", cinfo.num_components );
	printf( "Color space: %d.\n", cinfo.jpeg_color_space );
#endif

	/* Uncomment the following to output image information, if needed. */
	n->width = cinfo.image_width;
	n->height = cinfo.image_height;
	n->surface = n->width * n->height;
 
	/* Start decompression jpeg here */
	jpeg_start_decompress(&cinfo);

	/* allocate memory to hold the uncompressed image */
	image_memory(n);

	/* now actually read the jpeg into the raw buffer */
	row_pointer = malloc(cinfo.output_width * cinfo.num_components);

	/* read one scan line at a time */
	location = 0;
	while (cinfo.output_scanline < cinfo.image_height) {

		/* read one line */
		jpeg_read_scanlines(&cinfo, &row_pointer, 1);

		/* copy RGB line */
		if (cinfo.jpeg_color_space == JCS_RGB || 
		    cinfo.jpeg_color_space == JCS_YCbCr) {
			x = 0;
			for (i=0; i<n->width*3; i+=3) {
				n->row_pointers[location][x+0] = row_pointer[i+0];
				n->row_pointers[location][x+1] = row_pointer[i+1];
				n->row_pointers[location][x+2] = row_pointer[i+2];
				n->row_pointers[location][x+3] = 0xff;
				x += 4;
			}
		}

		/* copy yuv line */
#if 0
		if (cinfo.jpeg_color_space == JCS_YCbCr) {
			double y;
			double u;
			double v;
			double r;
			double g;
			double b;
			x = 0;
			for (i=0; i<n->width*3; i+=3) {

				y = row_pointer[i+0];
				u = row_pointer[i+1];
				v = row_pointer[i+2];

				r = ( 1.164 * (y - 16) ) + ( 1.596 * (v - 128) );
				g = ( 1.164 * (y - 16) ) - ( 0.813 * (v - 128) ) - ( 0.391* (u - 128) );
				b = ( 1.164 * (y - 16) ) + ( 2.018 * (u - 128) );

				n->row_pointers[location][x+0] = r;
				n->row_pointers[location][x+1] = g;
				n->row_pointers[location][x+2] = b;
				n->row_pointers[location][x+3] = 0xff;

				x += 4;
			}
		}
#endif

		/* copy and convert grayscale line */
		if (cinfo.jpeg_color_space == JCS_GRAYSCALE) {	
			x = 0;
			for (i=0; i<n->width; i++) {
				n->row_pointers[location][x+0] = row_pointer[i+0];
				n->row_pointers[location][x+1] = row_pointer[i+0];
				n->row_pointers[location][x+2] = row_pointer[i+0];
				n->row_pointers[location][x+3] = 0xff;
				x += 4;
			}
		}

		/* next line */
		location++;
	}

	/* wrap up decompression, destroy objects, free pointers and close open files */
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	free(row_pointer);
	fclose(infile);

	/* yup, we succeeded! */
	return n;
}

struct node *openpng(const char *name)
{
	unsigned char sig[8];
	struct node *n;
	FILE *fh;
	png_structp png_ptr;
	png_infop info_ptr;
	int bit_depth;
	int color_type;

	/* ouverture du fichier */
	fh = fopen(name, "r");
	if (fh == NULL) {
		fprintf(stderr, "cant not open file \"%s\": %s\n",
		        name, strerror(errno));
		exit(1);
	}

	/* on verifie la signature */
	fread(sig, 1, 8, fh);
	if (!png_check_sig(sig, 8)) {
		fprintf(stderr, "bad png signature \"%s\"\n", name);
		exit(1);
	}

	/* on fabrique le noeud qui va contenir l'image */
	n = malloc(sizeof(struct node));
	if (n == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* on fabrique la structure qui va recevoir l'image */
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* on fabrique la structure qui contient les infos sur l'image */
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* traitement des erreurs */
	if (setjmp(png_ptr->jmpbuf)) {
		fprintf(stderr, "png read \"%s\" error\n", name);
		exit(1);
	}

	/* positionne le handler du fichier qui sera utilisé pour la lecture */
	png_init_io(png_ptr, fh);

	/* do not check the signature */
	png_set_sig_bytes(png_ptr, 8);

	/* read the png file info */
	png_read_info(png_ptr, info_ptr);

	/* recupere les infos concernant l'image */
	png_get_IHDR(png_ptr, info_ptr,
	             &n->width, &n->height, &bit_depth, &color_type,
	             NULL, NULL, NULL);
	
	/* on convertit le "gray" en RGB */
	if ((color_type & PNG_COLOR_MASK_COLOR) == 0) {

		/* transform grayscale of less than 8 to 8 bits */
		if (bit_depth < 8)
			png_set_gray_1_2_4_to_8(png_ptr);

		png_set_gray_to_rgb(png_ptr);
	}

	/* changes paletted images to RGB */
	if ((color_type & PNG_COLOR_MASK_PALETTE) != 0)
		png_set_palette_to_rgb(png_ptr);
	
	/* PNG can have files with 16 bits per channel. If you only can handle 8 bits
	 * per channel, this will strip the pixels down to 8 bit.
	 */
	if (bit_depth == 16)
		png_set_strip_16(png_ptr);
	
	/* add alpha channel */
	if ((color_type & PNG_COLOR_MASK_ALPHA) == 0) {
		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
	}

	/* adds a full alpha channel if there is transparency information in a tRNS chunk */
	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);
	
	/* calcule la surface de l'image */
	n->surface = n->width * n->height;

	/* de la memoire pour charger l'image */
	image_memory(n);

	/* load image */
	png_read_image(png_ptr, n->row_pointers);

	fclose(fh);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	return n;
}

struct node *openimage(const char *name)
{
	const char *ext;

	/* get extension */
	ext = strrchr(name, '.');
	if (ext == NULL)
		return NULL;
	
	ext++;

	/* jpeg file */
	/**/ if (strcasecmp(ext, "jpg") == 0 ||
	         strcasecmp(ext, "jpeg") == 0)
		return openjpg(name);

	/* png file */
	else if (strcasecmp(ext, "png") == 0)
		return openpng(name);
	
	fprintf(stderr, "unmanaged file format \"%s\"\n", name);
	exit(1);
}

void drawpng(struct surface *buffer, int width, int height, const char *name)
{
	FILE *fp;
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep row;
	int basex;
	int basey;
	int x;
	int y;

	/* Open file for writing (binary mode) */
	fp = fopen(name, "wb");
	if (fp == NULL) {
		fprintf(stderr, "Could not open file %s for writing\n", name);
		exit(1);
	}

	/* Initialize write structure */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		fprintf(stderr, "Could not allocate write struct\n");
		exit(1);
	}

	/* Initialize info structure */
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		fprintf(stderr, "Could not allocate info struct\n");
		exit(1);
	}

	/* Setup Exception handling */
	if (setjmp(png_jmpbuf(png_ptr))) {
		fprintf(stderr, "Error during png creation\n");
		exit(1);
	}

	png_init_io(png_ptr, fp);

	/* Write header (8 bit colour depth + alpha) */
	png_set_IHDR(png_ptr, info_ptr, width, height,
	             8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
	             PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	/* write png info into file */
	png_write_info(png_ptr, info_ptr);

	/* Allocate memory for one row (3 bytes per pixel - RGB) */
	row = (png_bytep) malloc(4 * width * sizeof(png_byte));

	/* Write image data */
	for (y=0 ; y<height ; y++) {
		basey = y * width;
		for (x=0 ; x<width ; x++) {
			basex = x * 4;
			if (buffer[y*width + x].used != 0) {
				row[basex+0] = buffer[basey+x].r;
				row[basex+1] = buffer[basey+x].g;
				row[basex+2] = buffer[basey+x].b;
				row[basex+3] = buffer[basey+x].a;
			} else {
				row[basex+0] = 0x00;
				row[basex+1] = 0x00;
				row[basex+2] = 0x00;
				row[basex+3] = 0x00;
			}
		}
		png_write_row(png_ptr, row);
	}

	/* End write */
	png_write_end(png_ptr, NULL);
	fclose(fp);
	png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
	png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	free(row);
}

static inline
int check_size(struct surface *surf, int larg, int sx, int sy, int width, int height)
{
	int x;
	int y;
	int base;

	for (y=sy; y<sy+height; y++) {
		base = y * larg;
		for (x=sx; x<sx+width; x++)
			if (surf[base + x].used != 0)
				return 0;
	}
	return 1;
}

static inline
void fill(struct surface *surf, int larg, int sx, int sy, struct node *n)
{
	int x;
	int y;
	int base;
	int py;
	int px;

	py = 0;
	for (y=sy; y<sy+n->height; y++) {
		base = y * larg;
		px = 0;
		for (x=sx; x<sx+n->width; x++) {
			surf[base + x].used = 1;
			surf[base + x].r = n->row_pointers[py][px+0];
			surf[base + x].g = n->row_pointers[py][px+1];
			surf[base + x].b = n->row_pointers[py][px+2];
			surf[base + x].a = n->row_pointers[py][px+3];
			px += 4;
		}
		py++;
	}
}

int compar(const void *ia, const void *ib)
{
	const struct node * const *ia1 = ia;
	const struct node * const *ib1 = ib;
	const struct node *a = *ia1;
	const struct node *b = *ib1;

	return a->surface < b->surface;
}

int main(int argc, char *argv[])
{
	int smin = 0;
	int ymax = 0;
	int xmin = 0;
	int larg;
	int i;
	struct surface *surf;
	struct node **pool;
	struct node *node;
	int x;
	int y;
	int top = 0;
	int nb_img;
	int idx = 0;
	int do_break;
	const char *output_image = NULL;

	/* memoire pour le tri */
	pool = calloc(sizeof(struct node *), argc - 1);

	/* load options */
	for (i=1; i<argc; i++) {

		/*
		 *
		 * output image file
		 *
		 */
		/**/ if (strcmp(argv[i], "-o") == 0) {
			i++;
			if (i >= argc) {
				fprintf(stderr, "option -i expect file\n");
				usage();
				exit(1);
			}
			output_image = argv[i];
		}
		/*
		 * 
		 * end of option, now load images
		 *
		 */
		else
			break;
	}

	/* no input files */
	if (i >= argc) {
		fprintf(stderr, "no input files\n");
		usage();
		exit(1);
	}

	/* check configuration */
	if (output_image == NULL) {
		fprintf(stderr, "no outimage\n");
		usage();
		exit(1);
	}

	/* number of images */
	nb_img = argc - i;

	/* charge les images */
	for (; i<argc; i++) {

		/* open png image */
		node = openimage(argv[i]);
		if (node == NULL) {
			nb_img --;
			continue;
		}

		/* index png image */
		pool[idx] = node;
		idx++;

		/* calcul de la surface minimale */
		smin += node->surface;

		/* calcul de la largeur minimale */
		if (node->width > xmin)
			xmin = node->width;

		/* hauteur maximale */
		ymax += node->height;
	}

	/* nothing to do */
	if (nb_img == 0)
		exit(0);

	/* Calcule la largeur */
	larg = sqrt(smin) + 1;
	if (larg < xmin)
		larg = xmin;

	/* memoire pour la surface de placement */
	surf = calloc(sizeof(struct surface), larg * ymax);

	/* on ordone les images */
	qsort(pool, nb_img, sizeof(struct node *), compar);

	/* on va placer les locs par ordre de taille */
	for (i=0; i<nb_img; i++) {

		/* get node */
		node = pool[i];

		/* on place le noeud
		 * on scanne l'espace libre de gaucha a droite puis de haut en bas
		 */
		do_break = 0;
		for (y=0; y<ymax-node->height+1; y++) {
			for (x=0; x<larg-node->width+1; x++) {

				/* on verifie l'espace libre 
				 * on scanne uniquement la zone dans laquelle l'image
				 * peut potentiellement contenir
				 */
				if (check_size(surf, larg, x, y, node->width, node->height)) {
					fill(surf, larg, x, y, node);

					/* on met à jour la hauteur de l'image */
					if (top < y + node->height)
						top = y + node->height;

					/* on note les coordonnées de destination */
					node->dest_x = x;
					node->dest_y = y;

					/* fin du scan pour cette image */
					do_break = 1;
					break;
				}
			}
			if (do_break)
				break;
		}
	}

	/* draw png outpout image */
	drawpng(surf, larg, top, output_image);

	return 0;
}
