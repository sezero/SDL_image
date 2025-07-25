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

#if (!defined(__APPLE__) || defined(SDL_IMAGE_USE_COMMON_BACKEND)) || !defined(BMP_USES_IMAGEIO)

/* This is a BMP image file loading framework
 *
 * ICO/CUR file support is here as well since it uses similar internal
 * representation
 *
 * A good test suite of BMP images is available at:
 * http://entropymine.com/jason/bmpsuite/bmpsuite/html/bmpsuite.html
 */

#include <SDL3_image/SDL_image.h>

#ifdef LOAD_BMP

#define ICON_TYPE_ICO   1
#define ICON_TYPE_CUR   2

/* See if an image is contained in a data source */
bool IMG_isBMP(SDL_IOStream *src)
{
    Sint64 start;
    bool is_BMP;
    char magic[2];

    if (!src) {
        return false;
    }

    start = SDL_TellIO(src);
    is_BMP = false;
    if (SDL_ReadIO(src, magic, sizeof(magic)) == sizeof(magic)) {
        if (SDL_strncmp(magic, "BM", 2) == 0) {
            is_BMP = true;
        }
    }
    SDL_SeekIO(src, start, SDL_IO_SEEK_SET);
    return is_BMP;
}

static bool IMG_isICOCUR(SDL_IOStream *src, int type)
{
    Sint64 start;
    bool is_ICOCUR;

    /* The Win32 ICO file header (14 bytes) */
    Uint16 bfReserved;
    Uint16 bfType;
    Uint16 bfCount;

    if (!src) {
        return false;
    }

    start = SDL_TellIO(src);
    is_ICOCUR = false;
    if (SDL_ReadU16LE(src, &bfReserved) &&
        SDL_ReadU16LE(src, &bfType) &&
        SDL_ReadU16LE(src, &bfCount) &&
        (bfReserved == 0) && (bfType == type) && (bfCount != 0)) {
        is_ICOCUR = true;
    }
    SDL_SeekIO(src, start, SDL_IO_SEEK_SET);

    return is_ICOCUR;
}

bool IMG_isICO(SDL_IOStream *src)
{
    return IMG_isICOCUR(src, ICON_TYPE_ICO);
}

bool IMG_isCUR(SDL_IOStream *src)
{
    return IMG_isICOCUR(src, ICON_TYPE_CUR);
}

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_endian.h>

/* Compression encodings for BMP files */
#ifndef BI_RGB
#define BI_RGB      0
#define BI_RLE8     1
#define BI_RLE4     2
#define BI_BITFIELDS    3
#endif

static SDL_Surface *LoadBMP_IO(SDL_IOStream *src, bool closeio)
{
    return SDL_LoadBMP_IO(src, closeio);
}

static SDL_Surface *LoadICOCUR_IO(SDL_IOStream * src, int type, bool closeio)
{
    bool was_error = true;
    Sint64 fp_offset = 0;
    int bmpPitch;
    int i,j, pad;
    SDL_Surface *surface = NULL;
    /*
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    */
    Uint8 *bits;
    int ExpandBMP;
    Uint8 maxCol = 0;
    Uint32 icoOfs = 0;
    int nHotX = 0;
    int nHotY = 0;
    Uint32 palette[256];

    /* The Win32 ICO file header (14 bytes) */
    Uint16 bfReserved;
    Uint16 bfType;
    Uint16 bfCount;

    /* The Win32 BITMAPINFOHEADER struct (40 bytes) */
    Uint32 biSize;
    Sint32 biWidth;
    Sint32 biHeight;
    /* Uint16 biPlanes; */
    Uint16 biBitCount;
    Uint32 biCompression;
    /*
    Uint32 biSizeImage;
    Sint32 biXPelsPerMeter;
    Sint32 biYPelsPerMeter;
    Uint32 biClrImportant;
    */
    Uint32 biClrUsed;

    /* Make sure we are passed a valid data source */
    if (src == NULL) {
        goto done;
    }

    /* Read in the ICO file header */
    fp_offset = SDL_TellIO(src);

    if (!SDL_ReadU16LE(src, &bfReserved) ||
        !SDL_ReadU16LE(src, &bfType) ||
        !SDL_ReadU16LE(src, &bfCount) ||
        (bfReserved != 0) || (bfType != type) || (bfCount == 0)) {
        SDL_SetError("File is not a Windows %s file", type == 1 ? "ICO" : "CUR");
        goto done;
    }

    /* Read the Win32 Icon Directory */
    for (i = 0; i < bfCount; i++) {
        /* Icon Directory Entries */
        Uint8 bWidth;       /* Uint8, but 0 = 256 ! */
        Uint8 bHeight;      /* Uint8, but 0 = 256 ! */
        Uint8 bColorCount;  /* Uint8, but 0 = 256 ! */
        Uint8 bReserved;
        Uint16 wPlanes;
        Uint16 wBitCount;
        Uint32 dwBytesInRes;
        Uint32 dwImageOffset;
        int nWidth, nHeight, nColorCount;

        if (!SDL_ReadU8(src, &bWidth) ||
            !SDL_ReadU8(src, &bHeight) ||
            !SDL_ReadU8(src, &bColorCount) ||
            !SDL_ReadU8(src, &bReserved) ||
            !SDL_ReadU16LE(src, &wPlanes) ||
            !SDL_ReadU16LE(src, &wBitCount) ||
            !SDL_ReadU32LE(src, &dwBytesInRes) ||
            !SDL_ReadU32LE(src, &dwImageOffset)) {
            goto done;
        }

        if (bWidth) {
            nWidth = bWidth;
        } else {
            nWidth = 256;
        }
        if (bHeight) {
            nHeight = bHeight;
        } else {
            nHeight = 256;
        }
        if (bColorCount) {
            nColorCount = bColorCount;
        } else {
            nColorCount = 256;
        }

        if (type == ICON_TYPE_CUR) {
            nHotX = wPlanes;
            nHotY = wBitCount;
        }

        //SDL_Log("%dx%d@%d - %08x\n", nWidth, nHeight, nColorCount, dwImageOffset);
        (void)nWidth;
        (void)nHeight;
        if (nColorCount > maxCol) {
            maxCol = nColorCount;
            icoOfs = dwImageOffset;
            //SDL_Log("marked\n");
        }
    }

    /* Advance to the DIB Data */
    if (SDL_SeekIO(src, icoOfs, SDL_IO_SEEK_SET) < 0) {
        goto done;
    }

    /* Read the Win32 BITMAPINFOHEADER */
    if (!SDL_ReadU32LE(src, &biSize)) {
        goto done;
    }
    if (biSize == 40) {
        if (!SDL_ReadS32LE(src, &biWidth) ||
            !SDL_ReadS32LE(src, &biHeight) ||
            !SDL_ReadU16LE(src, NULL /* biPlanes */) ||
            !SDL_ReadU16LE(src, &biBitCount) ||
            !SDL_ReadU32LE(src, &biCompression) ||
            !SDL_ReadU32LE(src, NULL /* biSizeImage */) ||
            !SDL_ReadU32LE(src, NULL /* biXPelsPerMeter */) ||
            !SDL_ReadU32LE(src, NULL /* biYPelsPerMeter */) ||
            !SDL_ReadU32LE(src, &biClrUsed) ||
            !SDL_ReadU32LE(src, NULL /* biClrImportant */)) {
            goto done;
        }
    } else {
        SDL_SetError("Unsupported ICO bitmap format");
        goto done;
    }

    /* We don't support any BMP compression right now */
    switch (biCompression) {
    case BI_RGB:
        /* Default values for the BMP format */
        switch (biBitCount) {
        case 1:
        case 4:
            ExpandBMP = biBitCount;
            break;
        case 8:
            ExpandBMP = 8;
            break;
        case 24:
            ExpandBMP = 24;
            break;
        case 32:
            /*
            Rmask = 0x00FF0000;
            Gmask = 0x0000FF00;
            Bmask = 0x000000FF;
            */
            ExpandBMP = 0;
            break;
        default:
            SDL_SetError("ICO file with unsupported bit count");
            goto done;
        }
        break;
    default:
        SDL_SetError("Compressed ICO files not supported");
        goto done;
    }

    /* sanity check image size, so we don't overflow integers, etc. */
    if ((biWidth < 0) || (biWidth > 0xFFFFFF) ||
        (biHeight < 0) || (biHeight > 0xFFFFFF)) {
        SDL_SetError("Unsupported or invalid ICO dimensions");
        goto done;
    }

    /* Create a RGBA surface */
    biHeight = biHeight >> 1;
    //printf("%d x %d\n", biWidth, biHeight);
    surface = SDL_CreateSurface(biWidth, biHeight, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL) {
        goto done;
    }

    /* Load the palette, if any */
    //printf("bc %d bused %d\n", biBitCount, biClrUsed);
    if (biBitCount <= 8) {
        if (biClrUsed == 0) {
            biClrUsed = 1 << biBitCount;
        }
        if (biClrUsed > SDL_arraysize(palette)) {
            SDL_SetError("Unsupported or incorrect biClrUsed field");
            goto done;
        }
        for (i = 0; i < (int) biClrUsed; ++i) {
            if (SDL_ReadIO(src, &palette[i], 4) != 4) {
                goto done;
            }

            /* Since biSize == 40, we know alpha is reserved and should be zero, meaning opaque */
            if ((palette[i] & 0xFF000000) == 0) {
                palette[i] |= 0xFF000000;
            }
        }
    }

    /* Read the surface pixels.  Note that the bmp image is upside down */
    bits = (Uint8 *) surface->pixels + (surface->h * surface->pitch);
    switch (ExpandBMP) {
    case 1:
        bmpPitch = (biWidth + 7) >> 3;
        pad = (((bmpPitch) % 4) ? (4 - ((bmpPitch) % 4)) : 0);
        break;
    case 4:
        bmpPitch = (biWidth + 1) >> 1;
        pad = (((bmpPitch) % 4) ? (4 - ((bmpPitch) % 4)) : 0);
        break;
    case 8:
        bmpPitch = biWidth;
        pad = (((bmpPitch) % 4) ? (4 - ((bmpPitch) % 4)) : 0);
        break;
    case 24:
        bmpPitch = biWidth * 3;
        pad = (((bmpPitch) % 4) ? (4 - ((bmpPitch) % 4)) : 0);
        break;
    default:
        bmpPitch = biWidth * 4;
        pad = 0;
        break;
    }
    while (bits > (Uint8 *) surface->pixels) {
        bits -= surface->pitch;
        switch (ExpandBMP) {
        case 1:
        case 4:
        case 8:
            {
                Uint8 pixelvalue = 0;
                int shift = (8 - ExpandBMP);
                for (i = 0; i < surface->w; ++i) {
                    if (i % (8 / ExpandBMP) == 0) {
                        if (SDL_ReadIO(src, &pixelvalue, 1) != 1) {
                            goto done;
                        }
                    }
                    *((Uint32 *) bits + i) = (palette[pixelvalue >> shift]);
                    pixelvalue <<= ExpandBMP;
                }
            }
            break;
        case 24:
            {
                Uint32 pixelvalue;
                Uint8 channel;
                for (i = 0; i < surface->w; ++i) {
                    pixelvalue = 0xFF000000;
                    for (j = 0; j < 3; ++j) {
                        /* Load each color channel into pixel */
                        if (SDL_ReadIO(src, &channel, 1) != 1) {
                            goto done;
                        }
                        pixelvalue |= (channel << (j * 8));
                    }
                    *((Uint32 *) bits + i) = pixelvalue;
                }
            }
            break;

        default:
            if (SDL_ReadIO(src, bits, surface->pitch) != (size_t)surface->pitch) {
                goto done;
            }
            break;
        }
        /* Skip padding bytes, ugh */
        if (pad) {
            Uint8 padbyte;
            for (i = 0; i < pad; ++i) {
                if (SDL_ReadIO(src, &padbyte, 1) != 1) {
                    goto done;
                }
            }
        }
    }
    /* Read the mask pixels.  Note that the bmp image is upside down */
    bits = (Uint8 *) surface->pixels + (surface->h * surface->pitch);
    ExpandBMP = 1;
    bmpPitch = (biWidth + 7) >> 3;
    pad = (((bmpPitch) % 4) ? (4 - ((bmpPitch) % 4)) : 0);
    while (bits > (Uint8 *) surface->pixels) {
        Uint8 pixelvalue = 0;
        int shift = (8 - ExpandBMP);

        bits -= surface->pitch;
        for (i = 0; i < surface->w; ++i) {
            if (i % (8 / ExpandBMP) == 0) {
                if (SDL_ReadIO(src, &pixelvalue, 1) != 1) {
                    goto done;
                }
            }
            *((Uint32 *) bits + i) &= ((pixelvalue >> shift) ? 0 : 0xFFFFFFFF);
            pixelvalue <<= ExpandBMP;
        }
        /* Skip padding bytes, ugh */
        if (pad) {
            Uint8 padbyte;
            for (i = 0; i < pad; ++i) {
                if (SDL_ReadIO(src, &padbyte, 1) != 1) {
                    goto done;
                }
            }
        }
    }

    if (type == ICON_TYPE_CUR) {
        SDL_PropertiesID props = SDL_GetSurfaceProperties(surface);
        SDL_SetNumberProperty(props, SDL_PROP_SURFACE_HOTSPOT_X_NUMBER, nHotX);
        SDL_SetNumberProperty(props, SDL_PROP_SURFACE_HOTSPOT_Y_NUMBER, nHotY);
    }

    was_error = false;

done:
    if (closeio && src) {
        SDL_CloseIO(src);
    }
    if (was_error) {
        if (src && !closeio) {
            SDL_SeekIO(src, fp_offset, SDL_IO_SEEK_SET);
        }
        if (surface) {
            SDL_DestroySurface(surface);
        }
        surface = NULL;
    }
    return surface;
}

/* Load a BMP type image from an SDL datasource */
SDL_Surface *IMG_LoadBMP_IO(SDL_IOStream *src)
{
    return LoadBMP_IO(src, false);
}

/* Load a ICO type image from an SDL datasource */
SDL_Surface *IMG_LoadICO_IO(SDL_IOStream *src)
{
    return LoadICOCUR_IO(src, ICON_TYPE_ICO, false);
}

/* Load a CUR type image from an SDL datasource */
SDL_Surface *IMG_LoadCUR_IO(SDL_IOStream *src)
{
    return LoadICOCUR_IO(src, ICON_TYPE_CUR, false);
}

#else

/* See if an image is contained in a data source */
bool IMG_isBMP(SDL_IOStream *src)
{
    (void)src;
    return false;
}

bool IMG_isICO(SDL_IOStream *src)
{
    (void)src;
    return false;
}

bool IMG_isCUR(SDL_IOStream *src)
{
    (void)src;
    return false;
}

/* Load a BMP type image from an SDL datasource */
SDL_Surface *IMG_LoadBMP_IO(SDL_IOStream *src)
{
    (void)src;
    return NULL;
}

/* Load a BMP type image from an SDL datasource */
SDL_Surface *IMG_LoadCUR_IO(SDL_IOStream *src)
{
    (void)src;
    return NULL;
}

/* Load a BMP type image from an SDL datasource */
SDL_Surface *IMG_LoadICO_IO(SDL_IOStream *src)
{
    (void)src;
    return NULL;
}

#endif /* LOAD_BMP */

#endif /* !defined(__APPLE__) || defined(SDL_IMAGE_USE_COMMON_BACKEND) */
