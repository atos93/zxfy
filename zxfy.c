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

static const uint32_t zxpalette[16] = {
    0x000000,     // std black
    0xD70000,     // std blue
    0x0000D7,     // std red
    0xD700D7,     // std magenta
    0x00D700,     // std green
    0xD7D700,     // std cyan
    0x00D7D7,     // std yellow
    0xD7D7D7,     // std white
    0x000000,     // bright black
    0xFF0000,     // bright blue
    0x0000FF,     // bright red
    0xFF00FF,     // bright magenta
    0x00FF00,     // bright green
    0xFFFF00,     // bright cyan
    0x00FFFF,     // bright yellow
    0xFFFFFF,     // bright white
};

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
    screen = SDL_CreateWindow("ZXfy",
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
    long long d = 0;
    long long dr, dg, db;

    for (int y = 0; y < height-1; y++) {
        for (int x = 0; x < width-1; x++) {
            int i = (y*width+x)*3;
            dr = (int)a[i]-(int)b[i];
            dg = (int)a[i+1]-(int)b[i+1];
            db = (int)a[i+2]-(int)b[i+2];

            i += 3;
            dr += (int)a[i]-(int)b[i];
            dg += (int)a[i+1]-(int)b[i+1];
            db += (int)a[i+2]-(int)b[i+2];

            i += width*3;
            dr += (int)a[i]-(int)b[i];
            dg += (int)a[i+1]-(int)b[i+1];
            db += (int)a[i+2]-(int)b[i+2];

            i -= 3;
            dr += (int)a[i]-(int)b[i];
            dg += (int)a[i+1]-(int)b[i+1];
            db += (int)a[i+2]-(int)b[i+2];

            dr /= 4;
            dg /= 4;
            db /= 4;

            d += sqrt(dr*dr+dg*dg+db*db);
        }
    }
    return d;
}

void showHelp(char *progname) {
    fprintf(stderr,
        "Usage: %s <filename.png>\n" ,progname);
    exit(1);
}

void mutate(unsigned char *zxmem, int count, int gen) {
    for (int j = 0; j < count; j++) {
        uint32_t byte = rand() % ZX_VMEM_SIZE;
        uint32_t bit = rand() % 8;
        if (gen < 200000) {
            if (byte >= 256*192/8) {
                j--;
                continue;
            }
            zxmem[byte] ^= 1<<bit;
        } else if (gen > 200000 && gen < 400000) {
            if (byte < 256*192/8) {
                j--;
                continue;
            } else {
                int clr = zxmem[byte] ^ 1<<bit;
                int fg = clr & 7;
                int bg = (clr>>3) & 7;
                fg |= (clr & (1<<6)) >> 3;
                bg |= (clr & (1<<6)) >> 3;
                if (fg != bg) zxmem[byte] = clr;
            }
        } else if (gen > 400000 && gen < 600000) {
            if (byte < 256*192/8) {
                j--;
                continue;
            }
            zxmem[byte] ^= 1<<bit;
        } else {
            zxmem[byte] ^= 1<<bit;
        }
    }
}

// Render the ZX Spectrum VRAM into the framebuffer.
void zx2rgb(unsigned char *fb, unsigned char *zxmem) {
    int blink = 0;
    for (int y = 0; y < 192; y++) {
        uint16_t y_offset = ((y & 0xC0)<<5) | ((y & 0x07)<<8) | ((y & 0x38)<<2);
        for (int x = 0; x < 32; x++) {
            uint16_t pix_offset = y_offset | x;
            uint16_t clr_offset = 0x1800 + (((y & ~0x7)<<2) | x);

            // pixel mask and color attribute bytes
            uint8_t pix = zxmem[pix_offset];
            uint8_t clr = zxmem[clr_offset];

            // foreground and background color
            uint8_t fg, bg;
            if ((clr & (1<<7)) && blink) {
                fg = (clr>>3) & 7;
                bg = clr & 7;
            }
            else {
                fg = clr & 7;
                bg = (clr>>3) & 7;
            }
            // color bit 6: standard vs bright
            fg |= (clr & (1<<6)) >> 3;
            bg |= (clr & (1<<6)) >> 3;

            int c1 = ((pix&0x80) ? fg : bg);
            int c2 = ((pix&0x40) ? fg : bg);
            int c3 = ((pix&0x20) ? fg : bg);
            int c4 = ((pix&0x10) ? fg : bg);
            int c5 = ((pix&0x08) ? fg : bg);
            int c6 = ((pix&0x04) ? fg : bg);
            int c7 = ((pix&0x02) ? fg : bg);
            int c8 = ((pix&0x01) ? fg : bg);

            memcpy(fb,&zxpalette[c1],3); fb += 3;
            memcpy(fb,&zxpalette[c2],3); fb += 3;
            memcpy(fb,&zxpalette[c3],3); fb += 3;
            memcpy(fb,&zxpalette[c4],3); fb += 3;
            memcpy(fb,&zxpalette[c5],3); fb += 3;
            memcpy(fb,&zxpalette[c6],3); fb += 3;
            memcpy(fb,&zxpalette[c7],3); fb += 3;
            memcpy(fb,&zxpalette[c8],3); fb += 3;
        }
    }
}

int main(int argc, char **argv)
{
    FILE *fp;
    int width, height, alpha;
    unsigned char *image, *fb, *new, *best;
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    long long diff;
    float percdiff, bestdiff = 0;

    /* Initialization */
    srand(time(NULL));

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
    for (int j = 0; j < ZX_VMEM_SIZE; j++) best[j] = rand();
    for (int j = 256*192/8; j < ZX_VMEM_SIZE; j++) best[j] = 7; // white fg
    new = malloc(ZX_VMEM_SIZE);

    /* Show the current evolved image and the real image for one second each. */
    zx2rgb(fb,best);
    sdlShowRgb(texture,renderer,fb,width,height);
    sleep(1);
    sdlShowRgb(texture,renderer,image,width,height);
    sleep(1);

    /* Evolve the current solution using simulated annealing. */
    uint64_t generation = 0;
    uint64_t max_mutations = 5; // Max bits mutated per iteration.
    float temperature = 0.1;
    while(1) {
        /* Copy what is currenly the best solution, and mutate it. */
        memcpy(new,best,ZX_VMEM_SIZE);
        mutate(new,1+(rand()%max_mutations),generation);

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
        float dice = (float)rand() / RAND_MAX;
        if (generation == 0 || percdiff <= bestdiff || dice < temperature) {
            /* Save what is currently our "best" solution, even if actually
             * this may be a jump backward depending on the temperature.
             * It will be used as a base of the next iteration. */
            memcpy(best,new,ZX_VMEM_SIZE);
            bestdiff = percdiff;
        }
        if (generation % 1000 == 0) {
            zx2rgb(fb,best);
            sdlShowRgb(texture,renderer,fb,width,height);
            printf("gen:%llu: diff:%f%% temp:%g\n", generation, percdiff,
                temperature);
        }
        processSdlEvents();
        generation++;
        temperature -= 0.000001;
    }
    return 0;
}
