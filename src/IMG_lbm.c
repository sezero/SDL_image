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

/* This is a ILBM image file loading framework
   Load IFF pictures, PBM & ILBM packing methods, with or without stencil
   Written by Daniel Morais ( Daniel AT Morais DOT com ) in September 2001.
   24 bits ILBM files support added by Marc Le Douarain (http://www.multimania.com/mavati)
   in December 2002.
   EHB and HAM (specific Amiga graphic chip modes) support added by Marc Le Douarain
   (http://www.multimania.com/mavati) in December 2003.
   Stencil and colorkey fixes by David Raulo (david.raulo AT free DOT fr) in February 2004.
   Buffer overflow fix in RLE decompression by David Raulo in January 2008.
*/

#include <SDL3/SDL_endian.h>
#include <SDL3_image/SDL_image.h>

#ifdef LOAD_LBM


#define MAXCOLORS 256

/* Structure for an IFF picture ( BMHD = Bitmap Header ) */

typedef struct
{
    Uint16 w, h;        /* width & height of the bitmap in pixels */
    Sint16 x, y;        /* screen coordinates of the bitmap */
    Uint8 planes;       /* number of planes of the bitmap */
    Uint8 mask;         /* mask type ( 0 => no mask ) */
    Uint8 tcomp;        /* compression type */
    Uint8 pad1;         /* dummy value, for padding */
    Uint16 tcolor;      /* transparent color */
    Uint8 xAspect,      /* pixel aspect ratio */
          yAspect;
    Sint16  Lpage;      /* width of the screen in pixels */
    Sint16  Hpage;      /* height of the screen in pixels */
} BMHD;

bool IMG_isLBM(SDL_IOStream *src )
{
    Sint64 start;
    bool is_LBM;
    Uint8 magic[4+4+4];

    if (!src) {
        return false;
    }

    start = SDL_TellIO(src);
    is_LBM = false;
    if (SDL_ReadIO( src, magic, sizeof(magic) ) == sizeof(magic) )
    {
        if ( !SDL_memcmp( magic, "FORM", 4 ) &&
            ( !SDL_memcmp( magic + 8, "PBM ", 4 ) ||
              !SDL_memcmp( magic + 8, "ILBM", 4 ) ) )
        {
            is_LBM = true;
        }
    }
    SDL_SeekIO(src, start, SDL_IO_SEEK_SET);
    return is_LBM;
}

SDL_Surface *IMG_LoadLBM_IO(SDL_IOStream *src )
{
    Sint64 start;
    SDL_Surface *Image;
    Uint8       id[4], pbm, colormap[MAXCOLORS*3], *MiniBuf, *ptr, count, color, msk;
    Uint32      size, bytesloaded, nbcolors;
    Uint32      i, j, bytesperline, nbplanes, stencil, plane, h;
    Uint32      remainingbytes;
    Uint32      width;
    BMHD          bmhd;
    char        *error;
    Uint8       flagHAM,flagEHB;

    Image   = NULL;
    error   = NULL;
    MiniBuf = NULL;

    if ( !src ) {
        /* The error message has been set in SDL_IOFromFile */
        return NULL;
    }
    start = SDL_TellIO(src);

    if (SDL_ReadIO( src, id, 4 ) != 4 )
    {
        error="error reading IFF chunk";
        goto done;
    }

    /* Should be the size of the file minus 4+4 ( 'FORM'+size ) */
    if (SDL_ReadIO( src, &size, 4 ) != 4 )
    {
        error="error reading IFF chunk size";
        goto done;
    }

    /* As size is not used here, no need to swap it */

    if ( SDL_memcmp( id, "FORM", 4 ) != 0 )
    {
        error="not a IFF file";
        goto done;
    }

    if (SDL_ReadIO( src, id, 4 ) != 4 )
    {
        error="error reading IFF chunk";
        goto done;
    }

    pbm = 0;

    /* File format : PBM=Packed Bitmap, ILBM=Interleaved Bitmap */
    if ( !SDL_memcmp( id, "PBM ", 4 ) ) pbm = 1;
    else if ( SDL_memcmp( id, "ILBM", 4 ) )
    {
        error="not a IFF picture";
        goto done;
    }

    nbcolors = 0;

    SDL_memset( &bmhd, 0, sizeof( BMHD ) );
    flagHAM = 0;
    flagEHB = 0;

    while ( SDL_memcmp( id, "BODY", 4 ) != 0 )
    {
        if (SDL_ReadIO( src, id, 4 ) != 4 )
        {
            error="error reading IFF chunk";
            goto done;
        }

        if (SDL_ReadIO( src, &size, 4 ) != 4 )
        {
            error="error reading IFF chunk size";
            goto done;
        }

        bytesloaded = 0;

        size = SDL_Swap32BE( size );

        if ( !SDL_memcmp( id, "BMHD", 4 ) ) /* Bitmap header */
        {
            if (SDL_ReadIO( src, &bmhd, sizeof( BMHD ) ) != sizeof( BMHD ) )
            {
                error="error reading BMHD chunk";
                goto done;
            }

            bytesloaded = sizeof( BMHD );

            bmhd.w      = SDL_Swap16BE( bmhd.w );
            bmhd.h      = SDL_Swap16BE( bmhd.h );
            bmhd.x      = SDL_Swap16BE( bmhd.x );
            bmhd.y      = SDL_Swap16BE( bmhd.y );
            bmhd.tcolor = SDL_Swap16BE( bmhd.tcolor );
            bmhd.Lpage  = SDL_Swap16BE( bmhd.Lpage );
            bmhd.Hpage  = SDL_Swap16BE( bmhd.Hpage );
        }

        if ( !SDL_memcmp( id, "CMAP", 4 ) ) /* palette ( Color Map ) */
        {
            if (size > sizeof (colormap)) {
                error="colormap size is too large";
                goto done;
            }

            if (SDL_ReadIO( src, colormap, size ) != size )
            {
                error="error reading CMAP chunk";
                goto done;
            }

            bytesloaded = size;
            nbcolors = size / 3;
        }

        if ( !SDL_memcmp( id, "CAMG", 4 ) ) /* Amiga ViewMode  */
        {
            Uint32 viewmodes;
            if (SDL_ReadIO( src, &viewmodes, sizeof(viewmodes) ) != sizeof(viewmodes) )
            {
                error="error reading CAMG chunk";
                goto done;
            }

            bytesloaded = size;
            viewmodes = SDL_Swap32BE( viewmodes );
            if ( viewmodes & 0x0800 )
                flagHAM = 1;
            if ( viewmodes & 0x0080 )
                flagEHB = 1;
        }

        if ( SDL_memcmp( id, "BODY", 4 ) )
        {
            if ( size & 1 ) ++size;     /* padding ! */
            size -= bytesloaded;
            /* skip the remaining bytes of this chunk */
            if ( size ) SDL_SeekIO( src, size, SDL_IO_SEEK_CUR );
        }
    }

    /* compute some useful values, based on the bitmap header */

    width = ( bmhd.w + 15 ) & 0xFFFFFFF0;  /* Width in pixels modulo 16 */

    bytesperline = ( ( bmhd.w + 15 ) / 16 ) * 2;

    nbplanes = bmhd.planes;

    if ( pbm )                         /* File format : 'Packed Bitmap' */
    {
        bytesperline *= 8;
        nbplanes = 1;
    }

    stencil = (bmhd.mask & 1);   /* There is a mask ( 'stencil' ) */

    /* Allocate memory for a temporary buffer ( used for
       decompression/deinterleaving ) */

    MiniBuf = (Uint8 *)SDL_malloc( bytesperline * (nbplanes + stencil) );
    if ( MiniBuf == NULL )
    {
        error="not enough memory for temporary buffer";
        goto done;
    }

    {
       Uint32 format = SDL_PIXELFORMAT_INDEX8;
       if (nbplanes == 24 || flagHAM == 1) {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
          format = SDL_PIXELFORMAT_RGB24;
#else
          format = SDL_PIXELFORMAT_BGR24;
#endif
       }
        if ((Image = SDL_CreateSurface(width, bmhd.h, format)) == NULL){
            goto done;
        }
    }

    if ( bmhd.mask & 2 )               /* There is a transparent color */
        SDL_SetSurfaceColorKey( Image, true, bmhd.tcolor );

    /* Update palette information */

    /* There is no palette in 24 bits ILBM file */
    if ( nbcolors>0 && flagHAM==0 )
    {
        /* FIXME: Should this include the stencil? See comment below */
        SDL_Palette *palette;
        int nbrcolorsfinal = 1 << (nbplanes + stencil);
        ptr = &colormap[0];

        palette = SDL_CreateSurfacePalette(Image);
        if (!palette) {
            goto done;
        }

        for ( i=0; i<nbcolors; i++ )
        {
            palette->colors[i].r = *ptr++;
            palette->colors[i].g = *ptr++;
            palette->colors[i].b = *ptr++;
        }

        /* Amiga EHB mode (Extra-Half-Bright) */
        /* 6 bitplanes mode with a 32 colors palette */
        /* The 32 last colors are the same but divided by 2 */
        /* Some Amiga pictures save 64 colors with 32 last wrong colors, */
        /* they shouldn't !, and here we overwrite these 32 bad colors. */
        if ( (nbcolors==32 || flagEHB ) && (1<<nbplanes)==64 )
        {
            nbcolors = 64;
            ptr = &colormap[0];
            for ( i=32; i<64; i++ )
            {
                palette->colors[i].r = (*ptr++)/2;
                palette->colors[i].g = (*ptr++)/2;
                palette->colors[i].b = (*ptr++)/2;
            }
        }

        /* If nbcolors < 2^nbplanes, repeat the colormap */
        /* This happens when pictures have a stencil mask */
        if ( nbrcolorsfinal > (1<<nbplanes) ) {
            nbrcolorsfinal = (1<<nbplanes);
        }
        for ( i=nbcolors; i < (Uint32)nbrcolorsfinal; i++ )
        {
            palette->colors[i].r = palette->colors[i%nbcolors].r;
            palette->colors[i].g = palette->colors[i%nbcolors].g;
            palette->colors[i].b = palette->colors[i%nbcolors].b;
        }
        if ( !pbm )
            palette->ncolors = nbrcolorsfinal;
    }

    /* Get the bitmap */

    for ( h=0; h < bmhd.h; h++ )
    {
        /* uncompress the datas of each planes */

        for ( plane=0; plane < (nbplanes+stencil); plane++ )
        {
            ptr = MiniBuf + ( plane * bytesperline );

            remainingbytes = bytesperline;

            if ( bmhd.tcomp == 1 )      /* Datas are compressed */
            {
                do
                {
                    if (SDL_ReadIO( src, &count, 1 ) != 1 )
                    {
                        error="error reading BODY chunk";
                        goto done;
                    }

                    if ( count & 0x80 )
                    {
                        count ^= 0xFF;
                        count += 2; /* now it */

                        if ( ( count > remainingbytes ) || SDL_ReadIO( src, &color, 1 ) != 1 )
                        {
                            error="error reading BODY chunk";
                            goto done;
                        }
                        SDL_memset( ptr, color, count );
                    }
                    else
                    {
                        ++count;

                        if ( ( count > remainingbytes ) || SDL_ReadIO( src, ptr, count ) != count )
                        {
                           error="error reading BODY chunk";
                            goto done;
                        }
                    }

                    ptr += count;
                    remainingbytes -= count;

                } while ( remainingbytes > 0 );
            }
            else
            {
                if (SDL_ReadIO( src, ptr, bytesperline ) != bytesperline )
                {
                    error="error reading BODY chunk";
                    goto done;
                }
            }
        }

        /* One line has been read, store it ! */

        ptr = (Uint8 *)Image->pixels;
        if ( nbplanes==24 || flagHAM==1 )
            ptr += h * width * 3;
        else
            ptr += h * width;

        if ( pbm )                 /* File format : 'Packed Bitmap' */
        {
           SDL_memcpy( ptr, MiniBuf, width );
        }
        else        /* We have to un-interlace the bits ! */
        {
            if ( nbplanes!=24 && flagHAM==0 )
            {
                size = ( width + 7 ) / 8;

                for ( i=0; i < size; i++ )
                {
                    SDL_memset( ptr, 0, 8 );

                    for ( plane=0; plane < (nbplanes + stencil); plane++ )
                    {
                        color = *( MiniBuf + i + ( plane * bytesperline ) );
                        msk = 0x80;

                        for ( j=0; j<8; j++ )
                        {
                            if ( ( plane + j ) <= 7 ) ptr[j] |= (Uint8)( color & msk ) >> ( 7 - plane - j );
                            else                        ptr[j] |= (Uint8)( color & msk ) << ( plane + j - 7 );

                            msk >>= 1;
                        }
                    }
                    ptr += 8;
                }
            }
            else
            {
                Uint32 finalcolor = 0;
                size = ( width + 7 ) / 8;
                /* 24 bitplanes ILBM : R0...R7,G0...G7,B0...B7 */
                /* or HAM (6 bitplanes) or HAM8 (8 bitplanes) modes */
                for ( i=0; i<width; i=i+8 )
                {
                    Uint8 maskBit = 0x80;
                    for ( j=0; j<8; j++ )
                    {
                        Uint32 pixelcolor = 0;
                        Uint32 maskColor = 1;
                        Uint8 dataBody;
                        for ( plane=0; plane < nbplanes; plane++ )
                        {
                            dataBody = MiniBuf[ plane*size+i/8 ];
                            if ( dataBody&maskBit )
                                pixelcolor = pixelcolor | maskColor;
                            maskColor = maskColor<<1;
                        }
                        /* HAM : 12 bits RGB image (4 bits per color component) */
                        /* HAM8 : 18 bits RGB image (6 bits per color component) */
                        if ( flagHAM )
                        {
                            switch( pixelcolor>>(nbplanes-2) )
                            {
                                case 0: /* take direct color from palette */
                                    finalcolor = colormap[ pixelcolor*3 ] + (colormap[ pixelcolor*3+1 ]<<8) + (colormap[ pixelcolor*3+2 ]<<16);
                                    break;
                                case 1: /* modify only blue component */
                                    finalcolor = finalcolor&0x00FFFF;
                                    finalcolor = finalcolor | (pixelcolor<<(16+(10-nbplanes)));
                                    break;
                                case 2: /* modify only red component */
                                    finalcolor = finalcolor&0xFFFF00;
                                    finalcolor = finalcolor | pixelcolor<<(10-nbplanes);
                                    break;
                                case 3: /* modify only green component */
                                    finalcolor = finalcolor&0xFF00FF;
                                    finalcolor = finalcolor | (pixelcolor<<(8+(10-nbplanes)));
                                    break;
                            }
                        }
                        else
                        {
                            finalcolor = pixelcolor;
                        }
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                        *ptr++ = (Uint8)(finalcolor>>16);
                        *ptr++ = (Uint8)(finalcolor>>8);
                        *ptr++ = (Uint8)(finalcolor);
#else
                        *ptr++ = (Uint8)(finalcolor);
                        *ptr++ = (Uint8)(finalcolor>>8);
                        *ptr++ = (Uint8)(finalcolor>>16);
#endif
                        maskBit = maskBit>>1;
                    }
                }
            }
        }
    }

done:

    if ( MiniBuf ) SDL_free( MiniBuf );

    if ( error )
    {
        SDL_SeekIO(src, start, SDL_IO_SEEK_SET);
        if ( Image ) {
            SDL_DestroySurface( Image );
            Image = NULL;
        }
        SDL_SetError( "%s", error );
    }

    return Image;
}

#else /* LOAD_LBM */

/* See if an image is contained in a data source */
bool IMG_isLBM(SDL_IOStream *src)
{
    (void)src;
    return false;
}

/* Load an IFF type image from an SDL datasource */
SDL_Surface *IMG_LoadLBM_IO(SDL_IOStream *src)
{
    (void)src;
    return NULL;
}

#endif /* LOAD_LBM */
