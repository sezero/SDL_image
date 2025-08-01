/*
  showanim:  A test application for the SDL image loading library.
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

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>


/* Draw a Gimpish background pattern to show transparency in the image */
static void draw_background(SDL_Renderer *renderer, int w, int h)
{
    const SDL_Color col[2] = {
        { 0x66, 0x66, 0x66, 0xff },
        { 0x99, 0x99, 0x99, 0xff }
    };
    const int dx = 8, dy = 8;
    SDL_FRect rect;
    int i, x, y;

    rect.w = (float)dx;
    rect.h = (float)dy;
    for (y = 0; y < h; y += dy) {
        for (x = 0; x < w; x += dx) {
            /* use an 8x8 checkerboard pattern */
            i = (((x ^ y) >> 3) & 1);
            SDL_SetRenderDrawColor(renderer, col[i].r, col[i].g, col[i].b, col[i].a);

            rect.x = (float)x;
            rect.y = (float)y;
            SDL_RenderFillRect(renderer, &rect);
        }
    }
}

static void SaveAnimation(IMG_Animation *anim, const char *file)
{
    int i;
    Uint64 pts = 0;
    IMG_AnimationStream *stream = IMG_CreateAnimationStream(file);
    if (!stream) {
        SDL_Log("Couldn't save anim: %s\n", SDL_GetError());
        return;
    }

    for (i = 0; i < anim->count; ++i) {
        if (!IMG_AddAnimationFrame(stream, anim->frames[i], pts)) {
            SDL_Log("Couldn't add anim frame: %s\n", SDL_GetError());
            break;
        }
        pts += anim->delays[i];
    }
    IMG_CloseAnimationStream(stream);
}

int main(int argc, char *argv[])
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    IMG_Animation *anim;
    SDL_Texture **textures;
    Uint32 flags;
    int i, j, w, h, done;
    int once = 0;
    int current_frame, delay;
    SDL_Event event;
    const char *saveFile = NULL;

    (void)argc;

    /* Check command line usage */
    if ( ! argv[1] ) {
        SDL_Log("Usage: %s [-fullscreen] [-save file] <image_file> ...\n", argv[0]);
        return(1);
    }

    flags = SDL_WINDOW_HIDDEN;
    for ( i=1; argv[i]; ++i ) {
        if ( SDL_strcmp(argv[i], "-fullscreen") == 0 ) {
            SDL_HideCursor();
            flags |= SDL_WINDOW_FULLSCREEN;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
        return(2);
    }

    if (!SDL_CreateWindowAndRenderer("animation demo", 0, 0, flags, &window, &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s\n", SDL_GetError());
        return(2);
    }

    for ( i=1; argv[i]; ++i ) {
        if ( SDL_strcmp(argv[i], "-fullscreen") == 0 ) {
            continue;
        }

        if ( SDL_strcmp(argv[i], "-once") == 0 ) {
            once = 1;
            continue;
        }

        if (SDL_strcmp(argv[i], "-save") == 0 && argv[i + 1]) {
            ++i;
            saveFile = argv[i];
            continue;
        }

        /* Open the image file */
        anim = IMG_LoadAnimation(argv[i]);
        if (!anim) {
            SDL_Log("Couldn't load %s: %s\n", argv[i], SDL_GetError());
            continue;
        }
        w = anim->w;
        h = anim->h;

        if (saveFile) {
            SaveAnimation(anim, saveFile);
        }

        textures = (SDL_Texture **)SDL_calloc(anim->count, sizeof(*textures));
        if (!textures) {
            SDL_Log("Couldn't allocate textures\n");
            IMG_FreeAnimation(anim);
            continue;
        }
        for (j = 0; j < anim->count; ++j) {
            textures[j] = SDL_CreateTextureFromSurface(renderer, anim->frames[j]);
        }
        current_frame = 0;

        /* Show the window */
        SDL_SetWindowTitle(window, argv[i]);
        SDL_SetWindowSize(window, w, h);
        SDL_ShowWindow(window);

        done = 0;
        while ( ! done ) {
            while ( SDL_PollEvent(&event) ) {
                switch (event.type) {
                    case SDL_EVENT_KEY_UP:
                        switch (event.key.key) {
                            case SDLK_LEFT:
                                if ( i > 1 ) {
                                    i -= 2;
                                    done = 1;
                                }
                                break;
                            case SDLK_RIGHT:
                                if ( argv[i+1] ) {
                                    done = 1;
                                }
                                break;
                            case SDLK_ESCAPE:
                            case SDLK_Q:
                                argv[i+1] = NULL;
                                SDL_FALLTHROUGH;
                            case SDLK_SPACE:
                            case SDLK_TAB:
                            done = 1;
                            break;
                            default:
                            break;
                        }
                        break;
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        done = 1;
                        break;
                    case SDL_EVENT_QUIT:
                        argv[i+1] = NULL;
                        done = 1;
                        break;
                    default:
                        break;
                }
            }
            /* Draw a background pattern in case the image has transparency */
            draw_background(renderer, w, h);

            /* Display the image */
            SDL_RenderTexture(renderer, textures[current_frame], NULL, NULL);
            SDL_RenderPresent(renderer);

            if (anim->delays[current_frame]) {
                delay = anim->delays[current_frame];
            } else {
                delay = 100;
            }
            SDL_Delay(delay);

            current_frame = (current_frame + 1) % anim->count;

            if (once && current_frame == 0) {
                break;
            }
        }

        for (j = 0; j < anim->count; ++j) {
            SDL_DestroyTexture(textures[j]);
        }
        IMG_FreeAnimation(anim);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    /* We're done! */
    SDL_Quit();
    return(0);
}
