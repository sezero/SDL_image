/*
  SDL_image:  An example image loading library for use with SDL
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#if !defined(__APPLE__) || defined(SDL_IMAGE_USE_COMMON_BACKEND)

/* This is a Targa image file loading framework */

#include <SDL3/SDL_endian.h>

#include <SDL3_image/SDL_image.h>

#ifdef LOAD_TGA

/*
 * A TGA loader for the SDL library
 * Supports: Reading 8, 15, 16, 24 and 32bpp images, with alpha or colourkey,
 *           uncompressed or RLE encoded.
 *
 * 2000-06-10 Mattias Engdegård <f91-men@nada.kth.se>: initial version
 * 2000-06-26 Mattias Engdegård <f91-men@nada.kth.se>: read greyscale TGAs
 * 2000-08-09 Mattias Engdegård <f91-men@nada.kth.se>: alpha inversion removed
 */

struct TGAheader {
    Uint8 infolen;      /* length of info field */
    Uint8 has_cmap;     /* 1 if image has colormap, 0 otherwise */
    Uint8 type;

    Uint8 cmap_start[2];    /* index of first colormap entry */
    Uint8 cmap_len[2];      /* number of entries in colormap */
    Uint8 cmap_bits;        /* bits per colormap entry */

    Uint8 yorigin[2];       /* image origin (ignored here) */
    Uint8 xorigin[2];
    Uint8 width[2];     /* image size */
    Uint8 height[2];
    Uint8 pixel_bits;       /* bits/pixel */
    Uint8 flags;
};

enum tga_type {
    TGA_TYPE_INDEXED = 1,
    TGA_TYPE_RGB = 2,
    TGA_TYPE_BW = 3,
    TGA_TYPE_RLE_INDEXED = 9,
    TGA_TYPE_RLE_RGB = 10,
    TGA_TYPE_RLE_BW = 11
};

#define TGA_INTERLEAVE_MASK 0xc0
#define TGA_INTERLEAVE_NONE 0x00
#define TGA_INTERLEAVE_2WAY 0x40
#define TGA_INTERLEAVE_4WAY 0x80

#define TGA_ORIGIN_MASK     0x30
#define TGA_ORIGIN_LEFT     0x00
#define TGA_ORIGIN_RIGHT    0x10
#define TGA_ORIGIN_LOWER    0x00
#define TGA_ORIGIN_UPPER    0x20

/* read/write unaligned little-endian 16-bit ints */
#define LE16(p) ((p)[0] + ((p)[1] << 8))
#define SETLE16(p, v) ((p)[0] = (v), (p)[1] = (v) >> 8)

/* Load a TGA type image from an SDL datasource */
SDL_Surface *IMG_LoadTGA_IO(SDL_IOStream *src)
{
    Sint64 start;
    const char *error = NULL;
    struct TGAheader hdr;
    int rle = 0;
    int indexed = 0;
    int grey = 0;
    int ckey = -1;
    int ncols, w, h;
    SDL_Surface *img = NULL;
    Uint32 format;
    Uint8 *dst;
    int i;
    int bpp;
    int lstep;
    Uint32 pixelvalue;
    int count, rep;

    if ( !src ) {
        /* The error message has been set in SDL_IOFromFile */
        return NULL;
    }
    start = SDL_TellIO(src);

    if (SDL_ReadIO(src, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        error = "Error reading TGA data";
        goto error;
    }
    ncols = LE16(hdr.cmap_len);
    switch(hdr.type) {
    case TGA_TYPE_RLE_INDEXED:
        rle = 1;
        /* fallthrough */
    case TGA_TYPE_INDEXED:
        if (!hdr.has_cmap || hdr.pixel_bits != 8 || ncols > 256)
            goto unsupported;
        indexed = 1;
        break;

    case TGA_TYPE_RLE_RGB:
        rle = 1;
        /* fallthrough */
    case TGA_TYPE_RGB:
        indexed = 0;
        break;

    case TGA_TYPE_RLE_BW:
        rle = 1;
        /* fallthrough */
    case TGA_TYPE_BW:
        if (hdr.pixel_bits != 8)
            goto unsupported;
        /* Treat greyscale as 8bpp indexed images */
        indexed = grey = 1;
        break;

    default:
        goto unsupported;
    }

    bpp = (hdr.pixel_bits + 7) >> 3;
    switch(hdr.pixel_bits) {
    case 8:
        if (!indexed) {
                goto unsupported;
        }
        format = SDL_PIXELFORMAT_INDEX8;
        break;

    case 15:
    case 16:
        /* 15 and 16bpp both seem to use 5 bits/plane. The extra alpha bit
           is ignored for now. */
        format = SDL_PIXELFORMAT_XRGB1555;
        break;

    case 32:
        format = SDL_PIXELFORMAT_BGRA32;
        break;
    case 24:
        format = SDL_PIXELFORMAT_BGR24;
        break;

    default:
        goto unsupported;
    }

    if ((hdr.flags & TGA_INTERLEAVE_MASK) != TGA_INTERLEAVE_NONE
       || hdr.flags & TGA_ORIGIN_RIGHT) {
        goto unsupported;
    }

    SDL_SeekIO(src, hdr.infolen, SDL_IO_SEEK_CUR); /* skip info field */

    w = LE16(hdr.width);
    h = LE16(hdr.height);
    img = SDL_CreateSurface(w, h, format);
    if (img == NULL) {
        error = "Out of memory";
        goto error;
    }

    if (hdr.has_cmap) {
        size_t palsiz = ncols * ((hdr.cmap_bits + 7) >> 3);
        if (indexed && !grey) {
            Uint8 *pal = (Uint8 *)SDL_malloc(palsiz), *p = pal;
            SDL_Palette *palette = SDL_CreateSurfacePalette(img);
            if (!palette) {
                error = "Couldn't create palette";
                SDL_free(pal);
                goto error;
            }
            if (SDL_ReadIO(src, pal, palsiz) != palsiz) {
                error = "Error reading TGA data";
                SDL_free(pal);
                goto error;
            }
            if (ncols > palette->ncolors) {
                ncols = palette->ncolors;
            }
            palette->ncolors = ncols;

            for(i = 0; i < ncols; i++) {
                switch(hdr.cmap_bits) {
                case 15:
                case 16:
                    {
                    Uint16 c = p[0] + (p[1] << 8);
                    p += 2;
                    palette->colors[i].r = (c >> 7) & 0xf8;
                    palette->colors[i].g = (c >> 2) & 0xf8;
                    palette->colors[i].b = c << 3;
                    }
                    break;
                case 24:
                case 32:
                    palette->colors[i].b = *p++;
                    palette->colors[i].g = *p++;
                    palette->colors[i].r = *p++;
                    if (hdr.cmap_bits == 32 && *p++ < 128)
                        ckey = i;
                    break;
                }
            }
            SDL_free(pal);

            if (ckey >= 0)
                SDL_SetSurfaceColorKey(img, true, ckey);
        } else {
            /* skip unneeded colormap */
            SDL_SeekIO(src, palsiz, SDL_IO_SEEK_CUR);
        }
    }

    if (grey) {
        SDL_Palette *palette = SDL_CreateSurfacePalette(img);
        SDL_Color *colors;
        if (!palette) {
            error = "Couldn't create palette";
            goto error;
        }
        colors = palette->colors;
        for(i = 0; i < 256; i++)
            colors[i].r = colors[i].g = colors[i].b = i;
    }

    if (hdr.flags & TGA_ORIGIN_UPPER) {
        lstep = img->pitch;
        dst = (Uint8 *)img->pixels;
    } else {
        lstep = -img->pitch;
        dst = (Uint8 *)img->pixels + (h - 1) * img->pitch;
    }

    /* The RLE decoding code is slightly convoluted since we can't rely on
       spans not to wrap across scan lines */
    count = rep = 0;
    for(i = 0; i < h; i++) {
        if (rle) {
            int x = 0;
            for(;;) {
                Uint8 c;

                if (count) {
                    int n = count;
                    if (n > w - x)
                        n = w - x;
                    if (SDL_ReadIO(src, dst + x * bpp, n * bpp) != (size_t)(n * bpp)) {
                        error = "Error reading TGA data";
                        goto error;
                    }
                    count -= n;
                    x += n;
                    if (x == w)
                        break;
                } else if (rep) {
                    int n = rep;
                    if (n > w - x)
                        n = w - x;
                    rep -= n;
                    while (n--) {
                        SDL_memcpy(dst + x * bpp, &pixelvalue, bpp);
                        x++;
                    }
                    if (x == w)
                        break;
                }

                if (SDL_ReadIO(src, &c, 1) != 1) {
                    error = "Error reading TGA data";
                    goto error;
                }
                if (c & 0x80) {
                    if (SDL_ReadIO(src, &pixelvalue, bpp) != (size_t)bpp) {
                        error = "Error reading TGA data";
                        goto error;
                    }
                    rep = (c & 0x7f) + 1;
                } else {
                    count = c + 1;
                }
            }
        } else {
            if (SDL_ReadIO(src, dst, w * bpp) != (size_t)(w * bpp)) {
                error = "Error reading TGA data";
                goto error;
            }
        }
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        if (bpp == 2) {
            /* swap byte order */
            int x;
            Uint16 *p = (Uint16 *)dst;
            for(x = 0; x < w; x++)
            p[x] = SDL_Swap16(p[x]);
        }
#endif
        dst += lstep;
    }
    return img;

unsupported:
    error = "Unsupported TGA format";

error:
    SDL_SeekIO(src, start, SDL_IO_SEEK_SET);
    if ( img ) {
        SDL_DestroySurface(img);
    }
    SDL_SetError("%s", error);
    return NULL;
}

#else

/* dummy TGA load routine */
SDL_Surface *IMG_LoadTGA_IO(SDL_IOStream *src)
{
    (void)src;
    return NULL;
}

#endif /* LOAD_TGA */

#endif /* !defined(__APPLE__) || defined(SDL_IMAGE_USE_COMMON_BACKEND) */
