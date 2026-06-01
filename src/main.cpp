#include "app/Application.h"

// Provides the platform entry-point shim (WinMain on the Windows GUI subsystem)
// and remaps main -> SDL_main, so the app runs windowed with no console window
// while keeping the plain int main(argc, argv) signature below. Must be included
// in the translation unit that defines main().
#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    Application app;
    if (argc > 1)
        app.OpenFile(argv[1]);
    return app.Run();
}
