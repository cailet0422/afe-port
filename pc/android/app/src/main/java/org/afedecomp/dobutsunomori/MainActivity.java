package org.afedecomp.dobutsunomori;

import org.libsdl.app.SDLActivity;

/* SDL's activity runs SDL_main from libmain.so (pc/src/pc_main.c). */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
