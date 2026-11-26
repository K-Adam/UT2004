// The download of the full game content. It runs before the engine is up on Android, so it touches no engine types.

#include "SDLLaunchContent.h"

#include "HttpTransfer.h"

#pragma pack(push,8)
#include <openssl/evp.h>
#include <filesystem>
#pragma pack(pop)

// The download as a file, kept between attempts and launches so several gigabytes go on where they stopped.
struct FPartFile
{
    SDL_IOStream* File;
    FInstall* I;
};

// Appends the file from byte ResumeFrom on. False with the reason in Why when the transfer did not finish.
static bool Download(const char* Url, Sint64 ResumeFrom, FPartFile& Part, char* Why, size_t WhySize, bool& NoResume)
{
    FHttpRequest Request;
    Request.Url = Url;
    Request.FollowRedirects = true;
    Request.ResumeFrom = ResumeFrom;
    Request.ConnectTimeout = 20;
    Request.StallTime = 30;
    Request.Receive = [&Part](const char* Data, size_t Count)
    {
        Part.I->Progress(Count);
        return !SetupQuit && SDL_WriteIO(Part.File, Data, Count) == Count;
    };
    FHttpTransfer Transfer(Request);
    // The window answers while no data arrives too, and closing it stops the transfer.
    while (!Transfer.Poll() && !SetupQuit)
    {
        PumpSetup();
        Transfer.Wait(100);
    }
    // The server has nothing past the end of a file that is already complete.
    bool Ok = Transfer.Ok() || (ResumeFrom > 0 && Transfer.Status == 416);
    NoResume = !Ok && Transfer.RangeRefused;
    if (!Ok)
        SDL_snprintf(Why, WhySize, "%s", SetupQuit ? "Stopped" : Transfer.Error.c_str());
    return Ok;
}

bool PostReport(const char* Url, const char* Text)
{
    FHttpRequest Request;
    Request.Url = Url;
    Request.Body = Text;
    Request.ContentType = "text/plain";
    // The player waits on this with the game already gone.
    Request.ConnectTimeout = 5;
    Request.Timeout = 10;
    Request.Receive = [](const char*, size_t) { return true; };
    // A redirect or an error page is not a stored report.
    return FHttpTransfer(Request).Run();
}

static bool Sha256Matches(FPartFile& Part, const char* Expected)
{
    static unsigned char Buf[1 << 16];
    unsigned char Digest[EVP_MAX_MD_SIZE];
    unsigned int Len = 0;
    EVP_MD_CTX* Ctx = EVP_MD_CTX_new();
    bool Ok = Ctx && EVP_DigestInit_ex(Ctx, EVP_sha256(), NULL);
    for (size_t Got; Ok && !SetupQuit && (Got = SDL_ReadIO(Part.File, Buf, sizeof(Buf))) > 0; )
    {
        Part.I->Progress(Got);
        Ok = EVP_DigestUpdate(Ctx, Buf, Got) == 1;
    }
    Ok = Ok && !SetupQuit && EVP_DigestFinal_ex(Ctx, Digest, &Len);
    EVP_MD_CTX_free(Ctx);
    char Hex[2 * EVP_MAX_MD_SIZE + 1] = "";
    for (unsigned int i = 0; Ok && i < Len; i++)
        SDL_snprintf(Hex + 2 * i, 3, "%02x", Digest[i]);
    return Ok && SDL_strcasecmp(Hex, Expected) == 0;
}

// Negative when the system can't tell.
static Sint64 FreeSpace(const char* Dir)
{
    std::error_code Ec;
    std::filesystem::space_info Space = std::filesystem::space(std::filesystem::u8path(Dir), Ec);
    return Ec ? -1 : (Sint64)Space.available;
}

static Sint64 FileSize(const std::string& Path)
{
    SDL_PathInfo Info;
    return SDL_GetPathInfo(Path.c_str(), &Info) ? (Sint64)Info.size : 0;
}

// UT_FULL_SIZE, UT_FULL_UNPACKED and UT_FULL_SHA256 are zero and empty for a test URL, which then goes unchecked.
bool InstallFull(FInstall& I)
{
    SDL_CreateDirectory(I.Root);
    std::string Path = std::string(I.Root) + "/UT2004.zip.part";
    Sint64 Have = FileSize(Path);
    if (UT_FULL_SIZE && Have > UT_FULL_SIZE)
    {
        SDL_RemovePath(Path.c_str());
        Have = 0;
    }
    Sint64 Need = UT_FULL_SIZE - Have + UT_FULL_UNPACKED, Free = FreeSpace(I.Root);
    // A complete download was unpacking before, and what that left on the disk gets overwritten.
    if (Free >= 0 && Free < Need && (!UT_FULL_SIZE || Have < UT_FULL_SIZE))
    {
        SDL_snprintf(I.Err, sizeof(I.Err), "The game content needs %d GB of free space in %s, there are %d GB.",
            (int)((Need >> 30) + 1), I.Root, (int)(Free >> 30));
        return false;
    }

    I.Verb = "Downloading the game content";
    I.Size = UT_FULL_SIZE;
    I.Done = I.Drawn = Have;
    I.Draw();
    for (int Stalled = 0; !UT_FULL_SIZE || Have < UT_FULL_SIZE; )
    {
        FPartFile Part = { SDL_IOFromFile(Path.c_str(), "ab"), &I };
        if (!Part.File)
            return I.Fail("Couldn't write the download of %s. Is the device out of storage?");
        char Why[256] = "";
        bool NoResume = false;
        bool Ok = Download(I.Src, Have, Part, Why, sizeof(Why), NoResume);
        if (!SDL_CloseIO(Part.File))
            Ok = false;
        if (SetupQuit)
            return false;
        // A server or proxy that ignores the range can only send the file from its start.
        if (NoResume)
            SDL_RemovePath(Path.c_str());
        Sint64 Now = FileSize(Path);
        // Connections to the archive drop now and then, so it gives up only when retries bring nothing more.
        Stalled = Now > Have ? 0 : Stalled + 1;
        Have = Now;
        I.Done = I.Drawn = Have;
        if (Ok)
            break;
        if (Stalled == 3)
        {
            SDL_snprintf(I.Err, sizeof(I.Err), "Couldn't download %s:\n%s\n\nDownloading again goes on where it stopped.", I.Src, Why);
            return false;
        }
        SDL_Delay(2000);
    }

    FPartFile Part = { SDL_IOFromFile(Path.c_str(), "rb"), &I };
    if (!Part.File)
        return I.Fail("Couldn't read the download of %s.");
    I.Size = Have;
    I.Done = I.Drawn = 0;
    bool Ok = !UT_FULL_SIZE || Have == UT_FULL_SIZE;
    if (Ok && UT_FULL_SHA256[0])
    {
        I.Verb = "Checking the download";
        Ok = Sha256Matches(Part, UT_FULL_SHA256);
        I.Done = I.Drawn = 0;
    }
    if (Ok)
    {
        I.Verb = "Unpacking the game content";
        Ok = Unzip(Part.File, I);
        SDL_CloseIO(Part.File);
        // A failed unpack keeps a checked download, as it is the disk that ran out.
        if (Ok || !UT_FULL_SHA256[0])
            SDL_RemovePath(Path.c_str());
        return Ok;
    }
    SDL_CloseIO(Part.File);
    // A closed window stopped the check, which says nothing about the file.
    if (SetupQuit)
        return false;
    SDL_RemovePath(Path.c_str());
    return I.Fail("%s is not the published game content, its size or SHA-256 does not match.");
}
