// The downloads of the game content. They run before the engine is up on Android, so they touch no engine types.

#include "SDLLaunchContent.h"

#pragma pack(push,8)
#include <curl/curl.h>
#include <openssl/evp.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/statvfs.h>
#endif
#pragma pack(pop)

#if __ANDROID__ || UT_UWP
// IpDrv/Src/WebMaster.cpp
const std::string& CaCerts();
#endif

// The download as a file, kept between attempts and launches so several gigabytes go on where they stopped.
struct FPartFile
{
    SDL_IOStream* File;
    FInstall* I;
    Sint64 From;
};

static size_t WritePart(char* Data, size_t Size, size_t Count, void* User)
{
    FPartFile* Part = (FPartFile*)User;
    Part->I->Progress(Size * Count);
    return !SetupQuit && SDL_WriteIO(Part->File, Data, Size * Count) == Size * Count ? Size * Count : 0;
}

// Called while no data arrives too, so the window answers and closing it stops the transfer.
static int WhileWaiting(void* User, curl_off_t Total, curl_off_t, curl_off_t, curl_off_t)
{
    FPartFile* Part = (FPartFile*)User;
    // A source that publishes no size shows the one the server reports, which leaves out what an earlier attempt got.
    if (!Part->I->Size && Total > 0)
        Part->I->Size = Part->From + Total;
    PumpSetup();
    return SetupQuit;
}

// Appends the file from byte ResumeFrom on. False with the reason in Why when the transfer did not finish.
static bool Download(const char* Url, Sint64 ResumeFrom, FPartFile& Part, char* Why, size_t WhySize, bool& NoResume)
{
    CURL* Curl = curl_easy_init();
    if (!Curl)
        return false;
    char Reason[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(Curl, CURLOPT_URL, Url);
    curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
    // An error page must not end up in the file.
    curl_easy_setopt(Curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(Curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)ResumeFrom);
    curl_easy_setopt(Curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(Curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(Curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(Curl, CURLOPT_ERRORBUFFER, Reason);
    // Schannel otherwise fails when the CA's revocation server is unreachable.
    curl_easy_setopt(Curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#if __ANDROID__ || UT_UWP
    curl_blob Ca = { (void*)CaCerts().data(), CaCerts().size(), CURL_BLOB_NOCOPY };
    curl_easy_setopt(Curl, CURLOPT_CAINFO_BLOB, &Ca);
#endif
    curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, WritePart);
    curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &Part);
    curl_easy_setopt(Curl, CURLOPT_XFERINFOFUNCTION, WhileWaiting);
    curl_easy_setopt(Curl, CURLOPT_XFERINFODATA, &Part);
    curl_easy_setopt(Curl, CURLOPT_NOPROGRESS, 0L);
    CURLcode Code = curl_easy_perform(Curl);
    long Status = 0;
    curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &Status);
    curl_easy_cleanup(Curl);
    // The server has nothing past the end of a file that is already complete.
    bool Ok = Code == CURLE_OK || (ResumeFrom > 0 && Status == 416);
    NoResume = Code == CURLE_RANGE_ERROR;
    if (!Ok)
        SDL_snprintf(Why, WhySize, "%s", Reason[0] ? Reason : curl_easy_strerror(Code));
    return Ok;
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
#ifdef _WIN32
    wchar_t Wide[1024];
    ULARGE_INTEGER Free;
    if (MultiByteToWideChar(CP_UTF8, 0, Dir, -1, Wide, SDL_arraysize(Wide)) && GetDiskFreeSpaceExW(Wide, &Free, NULL, NULL))
        return (Sint64)Free.QuadPart;
#else
    struct statvfs St;
    if (statvfs(Dir, &St) == 0)
        return (Sint64)St.f_bavail * (Sint64)St.f_frsize;
#endif
    return -1;
}

static Sint64 FileSize(const std::string& Path)
{
    SDL_PathInfo Info;
    return SDL_GetPathInfo(Path.c_str(), &Info) ? (Sint64)Info.size : 0;
}

static const char* PartNames[] = { "UT2004.zip.part", "assets.tar.xz.part" };

bool PendingInstall(const char* Root, bool Forget)
{
    bool Pending = false;
    for (const char* Name : PartNames)
    {
        std::string Path = std::string(Root) + "/" + Name;
        Pending |= SDL_GetPathInfo(Path.c_str(), NULL);
        if (Forget)
            SDL_RemovePath(Path.c_str());
    }
    return Pending;
}

// Size, Unpacked and Sha256 are zero and empty for a source that publishes none, which then goes unchecked.
static bool InstallArchive(FInstall& I, const char* PartName, Sint64 Size, Sint64 Unpacked, const char* Sha256, bool (*Unpack)(SDL_IOStream*, FInstall&))
{
    SDL_CreateDirectory(I.Root);
    std::string Path = std::string(I.Root) + "/" + PartName;
    Sint64 Have = FileSize(Path);
    if (Size && Have > Size)
    {
        SDL_RemovePath(Path.c_str());
        Have = 0;
    }
    Sint64 Need = Size - Have + Unpacked, Free = FreeSpace(I.Root);
    // A complete download was unpacking before, and what that left on the disk gets overwritten.
    if (Free >= 0 && Free < Need && (!Size || Have < Size))
    {
        SDL_snprintf(I.Err, sizeof(I.Err), "The game content needs %d GB of free space in %s, there are %d GB.",
            (int)((Need >> 30) + 1), I.Root, (int)(Free >> 30));
        return false;
    }

    I.Verb = "Downloading the game content";
    I.Size = Size;
    I.Done = I.Drawn = Have;
    I.Draw();
    for (int Stalled = 0; !Size || Have < Size; )
    {
        FPartFile Part = { SDL_IOFromFile(Path.c_str(), "ab"), &I, Have };
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
    bool Ok = !Size || Have == Size;
    if (Ok && Sha256[0])
    {
        I.Verb = "Checking the download";
        Ok = Sha256Matches(Part, Sha256);
        Ok = Ok && SDL_SeekIO(Part.File, 0, SDL_IO_SEEK_SET) == 0;
        I.Done = I.Drawn = 0;
    }
    if (Ok)
    {
        I.Verb = "Unpacking the game content";
        Ok = Unpack(Part.File, I);
        SDL_CloseIO(Part.File);
        // A failed unpack keeps a checked download, as it is the disk that ran out. An unchecked one may be damaged,
        // so it is emptied and stays behind as the mark of an unfinished install.
        if (Ok)
            SDL_RemovePath(Path.c_str());
        else if (!Sha256[0] && !SetupQuit)
            SDL_CloseIO(SDL_IOFromFile(Path.c_str(), "wb"));
        return Ok;
    }
    SDL_CloseIO(Part.File);
    // A closed window stopped the check, which says nothing about the file.
    if (SetupQuit)
        return false;
    SDL_RemovePath(Path.c_str());
    return I.Fail("%s is not the published game content, its size or SHA-256 does not match.");
}

bool InstallFull(FInstall& I)
{
    return InstallArchive(I, PartNames[0], UT_FULL_SIZE, UT_FULL_UNPACKED, UT_FULL_SHA256, Unzip);
}

#if UT_UWP
static bool UnpackAssets(SDL_IOStream* In, FInstall& I)
{
    bool Zip = false;
    bool Ok = Untar(In, I, Zip);
    if (!Zip)
        return Ok;
    // Unzip counts what it reads itself.
    I.Done = I.Drawn = 0;
    return Unzip(In, I);
}

// Android unpacks this one while it arrives. Here the archive fits beside its content, and a file can be resumed.
bool InstallAssets(FInstall& I)
{
    return InstallArchive(I, PartNames[1], 0, 0, UT_ASSETS_SHA256, UnpackAssets);
}
#endif
