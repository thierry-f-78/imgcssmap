/*
 * Copyright (c) 2011-2012 Thierry FOURNIER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#include <png.h>
#include <jpeglib.h>

char color_mask[6] = {
	0xe0, /* 1 -> 3 bits */
	0xf0, /* 2 -> 4 bits */
	0xf8, /* 3 -> 5 bits */
	0xfc, /* 4 -> 6 bits */
	0xfe, /* 5 -> 7 bits */
	0xff, /* 6 -> 8 bits */
};

struct general {
	unsigned int hash;
	const char *output;
};

struct node {
	png_uint_32 width;
	png_uint_32 height;

	png_uint_32 surface;

	png_uint_32 dest_x;
	png_uint_32 dest_y;

	png_bytep *row_pointers;

	char *name;
	char *azname;
};

struct color {
	unsigned char r;
	unsigned char g;
	unsigned char b;
};

struct surface {
	unsigned char used;
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
};

enum template_elem_type {
	ELEM_STRING,

	ELEM_WIDTH,
	ELEM_HEIGHT,
	ELEM_OFFSETX,
	ELEM_OFFSETY,
	ELEM_NAME,
	ELEM_AZNAME,
	ELEM_HASH,
	ELEM_OUTPUT,
	ELEM_ID,
};

struct template_elem {
	enum template_elem_type type;
	char *string;
};

struct template {
	struct template *next;

	int nb[3];
	struct template_elem *elems[3];

	FILE *fh;
};

#define VAR_WIDTH   "$(width)"
#define VAR_HEIGHT  "$(height)"
#define VAR_OFFSETX "$(offsetx)"
#define VAR_OFFSETY "$(offsety)"
#define VAR_NAME    "$(name)"
#define VAR_AZNAME  "$(azname)"
#define VAR_HASH    "$(hash)"
#define VAR_OUTPUT  "$(output)"
#define VAR_ID      "$(id)"

void usage()
{
	fprintf(stderr, 
	"\n"
/*	 12345678901234567890123456789012345678901234567890123456789012345678901234567890 */
	"imgcssmap [-t in_file out_file [-t in[:hdr:foot] out [...]]] [-q 1-6] [-i]\n"
	"          [-na rrggbb] [-c] -o output_image input_file [...]\n"
	"\n"
	"   -t in[:hdr:foot] out  'in' containing the template (typically CSS) 'out'\n"
	"                         file generated by the template. The optional 'hdr'\n"
	"                         and 'foot' files can be included after and before\n"
	"                         the in template\n"
	"   -q 1-6                quality of colours. 6 is 8 bits per chanel quality\n"
	"                         5 is 7 bits, 4 is 6 bits, 3 is 5 bits, 2 is 4 bits\n"
	"                         and 1 is 3 bits\n"
	"   -na rrggbb            remove alpha channel and replace it by the rrggbb\n"
	"                         rrggbb is hexadecimal representation of the color\n"
	"   -i                    interlace png output image\n"
	"   -c                    crop unused alpha space into input file\n"
	"   -o output_image       image builded. The name can contain 8 x 'X'. These\n"
	"                         XXXXXXXX must be replaced by the imgcssmap hash.\n"
	"\n"
	"the template may contain this variables:\n"
	"   $(width)   the image width\n"
	"   $(height)  the image height\n"
	"   $(offsetx) the x offset of the image\n"
	"   $(offsety) the y offset of the image\n"
	"   $(name)    the image name without extension\n"
	"   $(azname)  the name only with this characters: 'a'-'z' '0'-'9' '_'\n"
	"   $(id)      the index after sorting. first image is 0.\n"
	"\n"
	);
}

static inline
unsigned int hash(unsigned int in)
{
  /* 4-byte integer hash, full avalanche
   * http://burtleburtle.net/bob/hash/integer.html
   */
  in = (in+0x7ed55d16) + (in<<12);
  in = (in^0xc761c23c) ^ (in>>19);
  in = (in+0x165667b1) + (in<<5);
  in = (in+0xd3a2646c) ^ (in<<9);
  in = (in+0xfd7046c5) + (in<<3);
  in = (in^0xb55a4f09) ^ (in>>16);

  return in;
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
		fprintf(stderr, "cannot open file \"%s\": %s\n",
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

	/* positionne le handler du fichier qui sera utilis� pour la lecture */
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

void crop(struct node *n)
{
	int x;
	int y;
	int yp;
	int do_crop;
	int rem;

	/*
	 *
	 * remove unused top ligne
	 *
	 */
	rem = 0;
	for (y=0; y<n->height; y++) {
		do_crop = 1;
		for (x=0; x<n->width; x++) {
			if (n->row_pointers[y][(x*4)+3] > 0x00) {
				do_crop = 0;
				break;
			}
		}
		if (do_crop)
			rem++;
		else
			break;
	}
	yp = 0;
	for (y=rem; y<n->height; y++) {
		n->row_pointers[yp] = n->row_pointers[y];
		yp++;
	}
	n->height -= rem;

	/*
	 *
	 * remove unused bottom lines
	 *
	 */
	rem = 0;
	for (y=n->height-1; y>=0; y--) {
		do_crop = 1;
		for (x=0; x<n->width; x++) {
			if (n->row_pointers[y][(x*4)+3] > 0x00) {
				do_crop = 0;
				break;
			}
		}
		if (do_crop)
			rem++;
		else
			break;
	}
	n->height -= rem;

	/*
	 *
	 * remove unused left columns 
	 *
	 */
	rem = 0;
	for (x=0; x<n->width; x++) {
		do_crop = 1;
		for(y=0; y<n->height; y++) {
			if (n->row_pointers[y][(x*4)+3] > 0x00) {
				do_crop = 0;
				break;
			}
		}
		if (do_crop)
			rem++;
		else
			break;
	}
	for(y=0; y<n->height; y++)
		n->row_pointers[y] = n->row_pointers[y] + ( rem * 4 );
	n->width -= rem;

	/*
	 *
	 * remove unused right columns
	 *
	 */
	rem = 0;
	for (x=n->width-1; x>=0; x++) {
		do_crop = 1;
		for(y=0; y<n->height; y++) {
			if (n->row_pointers[y][(x*4)+3] > 0x00) {
				do_crop = 0;
				break;
			}
		}
		if (do_crop)
			rem++;
		else
			break;
	}
	n->width -= rem;

	/* update surface */
	n->surface = n->height * n->width;
}

#define appli_alpha(__c, __b, __a) \
	( ( (__c * __a) + ( (__b * (255 - __a) ) ) ) / 255)

void drawpng(struct surface *buffer, int width, int height, int qual, int interlace,
             struct color *alpha, const char *name)
{
	FILE *fp;
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep row;
	int basex;
	int basey;
	int x;
	int y;
	int passes;
	int n;

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
	             8,
	             alpha ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGB_ALPHA,
	             interlace ?  PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, 
	             PNG_COMPRESSION_TYPE_BASE,
	             PNG_FILTER_TYPE_BASE);

	/* write png info into file */
	png_write_info(png_ptr, info_ptr);

	/* Allocate memory for one row (3 bytes per pixel - RGB) */
	row = (png_bytep) malloc(4 * width * sizeof(png_byte));

	/* number of passes */
	if (interlace)
		passes = png_set_interlace_handling(png_ptr);
	else
		passes = 1;

	/* Write image data */
	for(n=0; n<passes; n++) {
		for (y=0 ; y<height ; y++) {
			basey = y * width;
			for (x=0 ; x<width ; x++) {

				if (alpha != NULL)
					basex = x * 3;
				else
					basex = x * 4;

				/* unused pixel */
				if (buffer[y*width + x].used == 0) {
					row[basex+0] = 0x00;
					row[basex+1] = 0x00;
					row[basex+2] = 0x00;
					row[basex+3] = 0x00;
				}

				/* compute pixel color with background color and alpha channel */
				else if (alpha != NULL) {
					unsigned char a = buffer[basey+x].a;
					unsigned char r = buffer[basey+x].r;
					unsigned char g = buffer[basey+x].g;
					unsigned char b = buffer[basey+x].b;

					row[basex+0] = appli_alpha(r, alpha->r, a) & color_mask[qual];
					row[basex+1] = appli_alpha(g, alpha->g, a) & color_mask[qual];
					row[basex+2] = appli_alpha(b, alpha->b, a) & color_mask[qual];
					row[basex+3] = 0xff;
				}

				/* copy pixel */
				else if (buffer[basey+x].a != 0x00) {
					row[basex+0] = buffer[basey+x].r & color_mask[qual];
					row[basex+1] = buffer[basey+x].g & color_mask[qual];
					row[basex+2] = buffer[basey+x].b & color_mask[qual];
					row[basex+3] = buffer[basey+x].a & color_mask[qual];
				}

				/* pixel is transparent, set to 0 */
				else {
					row[basex+0] = 0x00;
					row[basex+1] = 0x00;
					row[basex+2] = 0x00;
					row[basex+3] = 0x00;
				}
			}
			png_write_row(png_ptr, row);
		}
	}

	/* End write */
	png_write_end(png_ptr, NULL);
	fclose(fp);
	png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
	png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	free(row);
}

char *load_file(const char *in_file)
{
	struct stat buf;
	int fd;
	char *bloc;

	/* get size */
	if (stat(in_file, &buf) < 0) {
		fprintf(stderr, "cannot open file \"%s\": %s\n",
		        in_file, strerror(errno));
		exit(1);
	}

	/* open input template file */
	fd = open(in_file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "cannot open file \"%s\": %s\n",
		        in_file, strerror(errno));
		exit(1);
	}

	/* memory for data */
	bloc = malloc(buf.st_size + 1);
	if (bloc == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* load data */
	if (read(fd, bloc, buf.st_size) != buf.st_size) {
		fprintf(stderr, "cannot read file \"%s\": %s\n",
		        in_file, strerror(errno));
		exit(1);
	}
	bloc[buf.st_size] = '\0';

	/* close inpout template file */
	close(fd);

	return bloc;
}

struct template_elem *parse_tpl(const char *bloc, int *outnb)
{
	enum template_elem_type type;
	struct template_elem *elems = NULL;
	int nb = 0;
	const char *p;
	const char *var;
	const char *nvar;
	const char *cont;

	p = bloc;
	while (1) {

		/* on recherche la premiere occurrence de la premiere variable */
		var = strstr(p, VAR_WIDTH);
		if (var !=  NULL) {
			type = ELEM_WIDTH;
			cont = var + strlen(VAR_WIDTH);
		}
		nvar = strstr(p, VAR_HEIGHT);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_HEIGHT;
			cont = var + strlen(VAR_HEIGHT);
		}
		nvar = strstr(p, VAR_OFFSETX);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_OFFSETX;
			cont = var + strlen(VAR_OFFSETX);
		}
		nvar = strstr(p, VAR_OFFSETY);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_OFFSETY;
			cont = var + strlen(VAR_OFFSETY);
		}
		nvar = strstr(p, VAR_NAME);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_NAME;
			cont = var + strlen(VAR_NAME);
		}
		nvar = strstr(p, VAR_AZNAME);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_AZNAME;
			cont = var + strlen(VAR_AZNAME);
		}
		nvar = strstr(p, VAR_HASH);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_HASH;
			cont = var + strlen(VAR_HASH);
		}
		nvar = strstr(p, VAR_OUTPUT);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_OUTPUT;
			cont = var + strlen(VAR_OUTPUT);
		}
		nvar = strstr(p, VAR_ID);
		if (var == NULL || (nvar != NULL && nvar < var)) {
			var = nvar;
			type = ELEM_ID;
			cont = var + strlen(VAR_ID);
		}

		/* copy string if is not empty */
		if (p != var) {
			nb++;
			elems = realloc(elems, sizeof(struct template_elem) * nb);
			if (elems == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			if (!var) {
				elems[nb-1].string = strdup(p);
			}
			else {
				elems[nb-1].string = malloc(var - p + 1);
				memcpy(elems[nb-1].string, p, var - p);
				elems[nb-1].string[var - p] = '\0';
			}
			elems[nb-1].type = ELEM_STRING;
		}

		/* copy variable element */
		if (var != NULL) {
			nb++;
			elems = realloc(elems, sizeof(struct template_elem) * nb);
			if (elems == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			elems[nb-1].string = NULL;
			elems[nb-1].type = type;
		}

		/* is the end of parsing */
		else
			break;
		
		/* continue parsing */
		p = cont;
	}

	*outnb = nb;
	return elems;
}

struct template *load_tpl(const char *in_file, const char *hdr, const char *foot, const char *out_file)
{
	struct template *tpl;
	char *bloc;

	/* memory for the template */
	tpl = calloc(sizeof(struct template), 1);
	if (tpl == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* Load header file */
	if (hdr) {
		bloc = load_file(hdr);
		tpl->elems[0] = parse_tpl(bloc, &tpl->nb[0]);
	}

	/* Load "each line" template */
	bloc = load_file(in_file);
	tpl->elems[1] = parse_tpl(bloc, &tpl->nb[1]);

	/* Load footer file */
	if (foot)
		bloc = load_file(foot);
		tpl->elems[2] = parse_tpl(bloc, &tpl->nb[2]);

	/* open output template file */
	tpl->fh = fopen(out_file, "w");
	if (tpl->fh == NULL) {
		fprintf(stderr, "cannot open file \"%s\": %s\n",
		        out_file, strerror(errno));
		exit(1);
	}

	return tpl;
}

void exec_tpl(struct template *tpl, int idx, struct node *node, struct general *gen, int id)
{
	int i;

	for (i=0; i<tpl->nb[idx]; i++) {
		switch(tpl->elems[idx][i].type) {
		case ELEM_STRING:
			fprintf(tpl->fh, "%s", tpl->elems[idx][i].string);
			break;
		case ELEM_WIDTH:
			fprintf(tpl->fh, "%lu", node->width);
			break;
		case ELEM_HEIGHT:
			fprintf(tpl->fh, "%lu", node->height);
			break;
		case ELEM_OFFSETX:
			fprintf(tpl->fh, "%lu", node->dest_x);
			break;
		case ELEM_OFFSETY:
			fprintf(tpl->fh, "%lu", node->dest_y);
			break;
		case ELEM_NAME:
			fprintf(tpl->fh, "%s", node->name);
			break;
		case ELEM_AZNAME:
			fprintf(tpl->fh, "%s", node->azname);
			break;
		case ELEM_HASH:
			fprintf(tpl->fh, "%08x", gen->hash);
			break;
		case ELEM_OUTPUT:
			fprintf(tpl->fh, "%s", gen->output);
			break;
		case ELEM_ID:
			fprintf(tpl->fh, "%d", id);
			break;
		}
	}
}

void close_tpl(struct template *tpl, struct general *general)
{
	struct node stnode;

	stnode.width = 0;
	stnode.height = 0;
	stnode.dest_x = 0;
	stnode.dest_y = 0;
	stnode.name = "";
	stnode.azname = "";

	exec_tpl(tpl, 2, &stnode, general, 0);

	fclose(tpl->fh);
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

char *do_azname(const char *name)
{
	char *p;
	char *n;

	/* search path component */
	p = strchr(name, '/');
	if (p == NULL)
		p = (char *)name;
	else
		p++;

	/* copy name without path component */
	n = strdup(p);
	if (n == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	/* remove extension */
	p = strrchr(n, '.');
	if (p != NULL)
		*p = '\0';

	/* check each char */
	for (p = n;
	     *p != '\0';
	     p++) {

		/* lower case */
		if (*p >= 'A' && *p <= 'Z')
			*p = tolower(*p);

		/* replace bad char */
		if ( (*p < 'a' || *p > 'z') &&
		     (*p < '0' || *p > '9') &&
		     *p != '_')
			*p = '_';
	}

	return n;
}

static inline
int hex_conv(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static inline
int byte_conv(char *c)
{
	int res1;
	int res2;

	res1 = hex_conv(c[0]);
	res2 = hex_conv(c[1]);
	if (res1 < 0 || res2 < 0)
		return -1;
	return ( res1 << 4 ) + res2;
}

static inline
int color_conv(char *in, struct color *c)
{
	int r;
	int g;
	int b;

	r = byte_conv(&in[0]);
	g = byte_conv(&in[2]);
	b = byte_conv(&in[4]);

	if (r < 0 || g < 0 || b < 0)
		return -1;

	c->r = r;
	c->g = g;
	c->b = b;

	return 0;
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
	char *in = NULL;
	char *hdr = NULL;
	char *foot = NULL;
	const char *out;
	struct template *templates = NULL;
	struct template *tpl;
	char *error;
	int qual = 5;
	int interlace = 0;
	struct color _alpha;
	struct color *alpha = NULL;
	int do_crop = 0;
	struct node stnode;
	struct general gen;
	char *p;
	char hashstr[9];

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
			gen.output = strdup(argv[i]);
		}

		/*
		 *
		 * load template
		 *
		 */
		else if (strcmp(argv[i], "-t") == 0) {

			i++;
			if (i >= argc) {
				fprintf(stderr, "option -t expect input file\n");
				usage();
				exit(1);
			}
			in = argv[i];

			i++;
			if (i >= argc) {
				fprintf(stderr, "option -t expect output file\n");
				usage();
				exit(1);
			}
			out = argv[i];

			/* Split input template */
			if (in) {
				hdr = strchr(in, ':');
				if (hdr) {
					*hdr = '\0';
					hdr++;

					foot = strchr(hdr, ':');
					if (foot) {
						*foot = '\0';
						foot++;
					}
				}
			}

			tpl = load_tpl(in, hdr, foot, out);
			tpl->next = templates;
			templates = tpl;
		}

		/*
		 *
		 * image quality
		 *
		 */
		else if (strcmp(argv[i], "-q") == 0) {
			i++;
			if (i >= argc) {
				fprintf(stderr, "option -q expect a value from 1 to 6\n");
				usage();
				exit(1);
			}
			qual = strtol(argv[i], &error, 10);
			if (*error != '\0' || qual < 1 || qual > 6) {
				fprintf(stderr, "option -q expect a value from 1 to 6\n");
				usage();
				exit(1);
			}
			qual--;
		}

		/*
		 *
		 * interlace
		 *
		 */
		else if (strcmp(argv[i], "-i") == 0) {
			interlace = 1;
		}

		/*
		 *
		 * remove alpha chanel
		 *
		 */
		else if (strcmp(argv[i], "-na") == 0) {
			i++;
			if (i >= argc) {
				fprintf(stderr, "option -na expect a value rrggbb\n");
				usage();
				exit(1);
			}
			if (strlen(argv[i]) != 6) {
				fprintf(stderr, "option -na expect a value rrggbb\n");
				usage();
				exit(1);
			}
			alpha = &_alpha;
			if (color_conv(argv[i], alpha) < 0)  {
				fprintf(stderr, "option -na expect a value rrggbb\n");
				usage();
				exit(1);
			}
		}

		/*
		 *
		 * crop
		 *
		 */
		else if (strcmp(argv[i], "-c") == 0) {
			do_crop = 1;
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
	if (gen.output == NULL) {
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

		/* crop image */
		if (do_crop)
			crop(node);

		/* copy name */
		node->name = argv[i];
		node->azname = do_azname(argv[i]);

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

					/* on met � jour la hauteur de l'image */
					if (top < y + node->height)
						top = y + node->height;

					/* on note les coordonn�es de destination */
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

	/* img sign */
	gen.hash = 0;
	for (i = 0; i < larg * top; i++) {
		gen.hash ^= hash( surf[i].r       ) |
		                ( surf[i].g << 8  ) |
		                ( surf[i].b << 16 ) |
		                ( surf[i].a << 24 );
	}

	gen.hash ^= hash(larg);
	gen.hash ^= hash(top);
	gen.hash ^= hash(qual);
	if (alpha)
		gen.hash ^= hash(1);

	/* Apply hash on the output images */
	p = strstr(gen.output, "XXXXXXXX");
	if (p) {
		snprintf(hashstr, 9, "%08x", gen.hash);
		memcpy(p, hashstr, 8);
	}


	/* Dump header templates file */
	stnode.width = 0;
	stnode.height = 0;
	stnode.dest_x = 0;
	stnode.dest_y = 0;
	stnode.name = "";
	stnode.azname = "";
	for (tpl = templates;
	     tpl != NULL;
	     tpl = tpl->next)
		exec_tpl(tpl, 0, &stnode, &gen, 0);

	/* on parcours les images pour executer les templates */
	for (i=0; i<nb_img; i++) {
		for (tpl = templates;
		     tpl != NULL; 
		     tpl = tpl->next)
			exec_tpl(tpl, 1, pool[i], &gen, i);
	}

	/* close templates */
	for (tpl = templates;
	     tpl != NULL; 
	     tpl = tpl->next)
		close_tpl(tpl, &gen);

	/* draw png outpout image */
	drawpng(surf, larg, top, qual, interlace, alpha, gen.output);

	return 0;
}
