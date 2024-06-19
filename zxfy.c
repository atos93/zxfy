/*
 * Copyright (c) 2008-2024, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#define PNG_DEBUG 3
#include <png.h>
#include <SDL.h>

#define ZX_VMEM_SIZE 6912 // ZX Spectrum video mem size. Bitmap + attributes.

/* SDL initialization function. */
static SDL_Texture *sdlInit(int width, int height, int fullscreen, SDL_Renderer **rp) {
    int flags = SDL_WINDOW_OPENGL;
    SDL_Window *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    if (SDL_Init(SDL_INIT_VIDEO) == -1) {
        fprintf(stderr, "SDL Init error: %s\n", SDL_GetError());
        return NULL;
    }
    atexit(SDL_Quit);
    screen = SDL_CreateWindow("Shapeme",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              width,height,flags);
    if (!screen) {
        fprintf(stderr, "Can't create SDL window: %s\n", SDL_GetError());
        return NULL;
    }

    renderer = SDL_CreateRenderer(screen,-1,0);
    if (!renderer) {
        fprintf(stderr, "Can't create SDL renderer: %s\n", SDL_GetError());
        return NULL;
    }

    texture = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB24,
                                SDL_TEXTUREACCESS_STREAMING,
                                width,height);
    if (!texture) {
        fprintf(stderr, "Can't create SDL texture: %s\n", SDL_GetError());
        return NULL;
    }
    *rp = renderer;
    return texture;
}

/* Show a raw RGB image on the SDL screen. */
static void sdlShowRgb(SDL_Texture *texture, SDL_Renderer *renderer, unsigned char *fb, int width,
        int height)
{
    (void)height;
    SDL_UpdateTexture(texture,NULL,fb,width*3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/* Minimal SDL event processing, just a few keys to exit the program. */
static void processSdlEvents(void) {
    SDL_Event event;

    while(SDL_PollEvent(&event)) {
        switch(event.type) {
        case SDL_KEYDOWN:
            switch(event.key.keysym.sym) {
            case SDLK_q:
            case SDLK_ESCAPE:
                exit(0);
                break;
            default: break;
            }
        }
    }
}

/* Write a PNG file. The image is passed with row_pointers as an RGB image. */
int PngWrite(FILE *fp, int width, int height, png_bytep *row_pointers)
{
    png_structp png_ptr;
    png_infop info_ptr;
    int bit_depth = 8;

    /* Initialization */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                    NULL, NULL, NULL);
    if (!png_ptr) return 1;
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) return 1;
    if (setjmp(png_jmpbuf(png_ptr))) return 1;
    png_init_io(png_ptr, fp);

    /* Write the header */
    if (setjmp(png_jmpbuf(png_ptr))) return 1;
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 bit_depth, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    /* Write data */
    if (setjmp(png_jmpbuf(png_ptr))) return 1;
    png_write_image(png_ptr, row_pointers);

    /* End */
    if (setjmp(png_jmpbuf(png_ptr))) return 1;
    png_write_end(png_ptr, NULL);
    return 0;
}

/* Load a PNG and returns it as a raw RGB representation, as an array of bytes.
 * As a side effect the function populates widthptr, heigthptr with the
 * size of the image in pixel. The integer pointed by alphaptr is set to one.
 * if the image is of type RGB_ALPHA, otherwise it's set to zero.
 *
 * This function is able to load both RGB and RGBA images, but it will always
 * return data as RGB, discarding the alpha channel. */
#define PNG_BYTES_TO_CHECK 8
unsigned char *PngLoad(FILE *fp, int *widthptr, int *heightptr, int *alphaptr) {
    unsigned char buf[PNG_BYTES_TO_CHECK];
    png_structp png_ptr;
    png_infop info_ptr;
    png_uint_32 width, height, j;
    int color_type, row_bytes;
    unsigned char **imageData, *rgb;

    /* Check signature */
    if (fread(buf, 1, PNG_BYTES_TO_CHECK, fp) != PNG_BYTES_TO_CHECK)
        return NULL;
    if (png_sig_cmp(buf, (png_size_t)0, PNG_BYTES_TO_CHECK))
        return NULL; /* Not a PNG image */

    /* Initialize data structures */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
        NULL,NULL,NULL);
    if (png_ptr == NULL) {
        return NULL; /* Out of memory */
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return NULL;
    }

    /* Error handling code */
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    /* Set the I/O method */
    png_init_io(png_ptr, fp);

    /* Undo the fact that we read some data to detect the PNG file */
    png_set_sig_bytes(png_ptr, PNG_BYTES_TO_CHECK);

    /* Read the PNG in memory at once */
    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    /* Get image info */
    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    if (color_type != PNG_COLOR_TYPE_RGB &&
        color_type != PNG_COLOR_TYPE_RGB_ALPHA) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    /* Get the image data */
    imageData = png_get_rows(png_ptr, info_ptr);
    row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    rgb = malloc(row_bytes*height);
    if (!rgb) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    for (j = 0; j < height; j++) {
        unsigned char *dst = rgb+(j*width*3);
        unsigned char *src = imageData[j];
        unsigned int i;

        for (i = 0; i < width; i++) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst += 3;
            src += (color_type == PNG_COLOR_TYPE_RGB_ALPHA) ? 4 : 3;
        }
    }

    /* Free the image and resources and return */
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    *widthptr = width;
    *heightptr = height;
    *alphaptr = (color_type == PNG_COLOR_TYPE_RGB_ALPHA);
    return rgb;
}

/* Compute the difference between two RGB frame buffers.
 * The differece is the sum of the differences of every pixel at the same
 * coordinates in the two images.
 *
 * A single pixel difference is computed as spacial distance between the RGB
 * color space. */
long long computeDiff(unsigned char *a, unsigned char *b, int width, int height) {
    int j;
    long long d = 0;
    long long dr, dg, db;

    for (j = 0; j < width*height*3; j+=3) {
        dr = (int)a[j]-(int)b[j];
        dg = (int)a[j+1]-(int)b[j+1];
        db = (int)a[j+2]-(int)b[j+2];
        d += sqrt(dr*dr+dg*dg+db*db);
    }
    return d;
}

void showHelp(char *progname) {
    fprintf(stderr,
        "Usage: %s <filename.png>\n" ,progname);
    exit(1);
}

int main(int argc, char **argv)
{
    FILE *fp;
    int width, height, alpha;
    unsigned char *image, *fb;
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    long long diff;
    float percdiff, bestdiff;

    /* Initialization */
    srand(time(NULL));
    state.temperature = 0.10;
    state.generation = 0;
    state.absbestdiff = 100; /* 100% is worst diff possible. */

    /* Check arity and parse additional args if any. */
    if (argc != 2) {
        showHelp(argv[0]);
        exit(1);
    }

    /* Load the PNG in memory. */
    fp = fopen(argv[1],"rb");
    if (!fp) {
        perror("Opening PNG file");
        exit(1);
    }
    if ((image = PngLoad(fp,&width,&height,&alpha)) == NULL) {
        printf("Can't load the specified image.");
        exit(1);
    }

    printf("Image %d %d, alpha:%d at %p\n", width, height, alpha, image);

    if (width != 256 && height != 192) {
        printf("The image must match exactly the ZX Spectrum resolution: 256x192\n");
        exit(1);
    }

    fclose(fp);

    /* Initialize SDL and allocate our arrays of triangles. */
    texture = sdlInit(width,height,0,&renderer);
    fb = malloc(width*height*3);
    best = malloc(ZX_VMEM_SIZE);
    new = malloc(ZX_VMEM_SIZE);

    /* Show the current evolved image and the real image for one second each. */
    zx2rgb(fb,best);
    sdlShowRgb(texture,renderer,fb,width,height);
    sleep(1);
    sdlShowRgb(texture,renderer,image,width,height);
    sleep(1);

    /* Evolve the current solution using simulated annealing. */
    uint64_t generation = 0;
    uint64_t temperature = 64; // Bits mutated per iteration.
    while(1) {
        generation++;
        if (temperature > 1 && !(state.generation % 1000))
            temperature--;

        /* Copy what is currenly the best solution, and mutate it. */
        memcpy(new,best,sizeof(new));
        mutate(new);

        /* Draw the mutated solution, and check what is its fitness.
         * In our case the fitness is the difference bewteen the target
         * image and our image. */
        zx2rgb(fb,new);
        diff = computeDiff(image,fb,width,height);

        /* The percentage of pixels difference is calculate taking the ratio
         * between the maximum possible pixel difference and the current
         * difference.
         * The magic constant 422 is actually the max difference between
         * two pixels as r,g,b coordinates in the space, so sqrt(255^2*3). */
        percdiff = (float)diff/(width*height*442)*100;
        if (percdiff < bestdiff) {
            /* Save what is currently our "best" solution, even if actually
             * this may be a jump backward depending on the temperature.
             * It will be used as a base of the next iteration. */
            memcpy(best,new,sizeof(new));

            bestdiff = percdiff;
            sdlShowRgb(texture,renderer,fb,width,height);

            if (state.generation % 10 == 0)
                printf("%llu: %f%%\n", generation, percdiff);
        }
        processSdlEvents();
    }
    return 0;
}
