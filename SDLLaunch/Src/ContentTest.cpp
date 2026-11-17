// Checks which archive paths the content setup accepts and unpacks the zip given as the first argument
// into the folder given as the second. Tools/ContentTest.cmake builds the zip and compares the result.

#include "SDLLaunchContent.h"

static bool Maps(const FInstall& I, const char* Rel, const char* Expected)
{
    char Dest[1024] = "";
    bool Taken = ContentPath(I, Rel, Dest, sizeof(Dest));
    std::string Want = Expected ? std::string(I.Root) + Expected : "";
    bool Ok = Expected ? Taken && Want == Dest : !Taken;
    if (!Ok)
        SDL_Log("%s gave %s", Rel, Taken ? Dest : "nothing");
    return Ok;
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    FInstall I = {};
    I.Src = argv[1];
    I.Root = argv[2];
    I.Verb = "Unpacking";
    bool Ok = Maps(I, "Maps/a.ut2", "/Maps/a.ut2")
        && Maps(I, "UT2004/Maps/a.ut2", "/Maps/a.ut2")
        && Maps(I, "ut2004/textures/Sub/b.utx", "/Textures/Sub/b.utx")
        && Maps(I, "Maps/", NULL)
        && Maps(I, "System/Core.u", NULL)
        && Maps(I, "UT2004/System/Maps/a.ut2", NULL)
        && Maps(I, "Maps/../System/a.ut2", NULL)
        && Maps(I, "MapsExtra/a.ut2", NULL);
    SDL_IOStream* Zip = SDL_IOFromFile(I.Src, "rb");
    if (Ok && !(Zip && Unzip(Zip, I) && I.Files == 2))
    {
        SDL_Log("Unzip wrote %d files: %s", I.Files, I.Err);
        Ok = false;
    }
    if (Zip)
        SDL_CloseIO(Zip);
    return Ok ? 0 : 1;
}
