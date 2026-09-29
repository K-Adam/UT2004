/*=============================================================================
	SDLLaunch.cpp: Game launcher.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Ryan C. Gordon, based on ut2003's win32 Launch.cpp.
=============================================================================*/

#include <time.h>  // dodging a macro clash with Unix C runtime headers...

#include "SDLLaunchPrivate.h"

#if __STATIC_LINK
#include "Engine.h"

#include "UnRender.h"
#include "UnNullRenderDevice.h"
#include "SDLDrv.h"
#include "OpenGLDrv.h"
#include "UnNet.h"
#include "UnFractal.h"
#include "ALAudio.h"
#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnIpDrvCommandlets.h"
#include "UnIpDrvNative.h"
#include "XInterface.h"
#include "UWeb.h"
#include "UWebNative.h"
#include "OnslaughtPrivate.h"
void autoInitializeRegistrantsEditor(INT &Lookup);
extern "C" BYTE GLoadedSDLDrv, GLoadedOpenGLDrv, GLoadedALAudio;

#endif


/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General.
#ifdef WIN32
extern "C" {HINSTANCE hInstance;}
#endif
extern "C" {TCHAR GPackage[64]=TEXT("SDLLaunch");}

#if __STATIC_LINK && defined(_MSC_VER)
// Statically linked third-party libraries allocate in their static constructors, and the engine's global operator new routes
// that through GMalloc. This file's globals initialize in the library phase before theirs, EarlyMalloc sets GMalloc there.
#pragma init_seg(lib)
#endif
// Memory allocator.
#ifdef _DEBUG
	#include "FMallocDebug.h"
	FMallocDebug Malloc;
#else
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#endif

#if __STATIC_LINK && defined(_MSC_VER)
static struct FEarlyMalloc { FEarlyMalloc() { GMalloc = &Malloc; } } EarlyMalloc;
#endif

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error handler.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#ifdef WIN32
#include "FFileManagerWindows.h"
FFileManagerWindows FileManager;
#else
#include "FFileManagerUnix.h"
FFileManagerUnix FileManager;
#endif

// Config.
#include "FConfigCacheIni.h"

/*-----------------------------------------------------------------------------
	mainline
-----------------------------------------------------------------------------*/

// Not really SDL specific apart from the splash screen.
#define USE_SDL_SPLASH

#if (!defined USE_SDL_SPLASH)

static void InitSplash(const TCHAR *filename) {}
static void ExitSplash(void) {}

#else

// SDL3.dll uses default packing, the engine builds with /Zp4.
#pragma pack(push,8)
#include <SDL3/SDL.h>
#pragma pack(pop)

#ifdef __ANDROID__
// Android has one window, so SDLDrv takes this one over and the logo stays up until the game draws.
// It gets the GL ES 3 context OpenGLDrv asks for, so the window's EGL config suits that one too.
static void InitSplash(const TCHAR *filename)
{
    SDL_Surface *bmp = filename ? SDL_LoadBMP(appToAnsi(filename)) : NULL;
    if (bmp == NULL || !SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_DestroySurface(bmp);
        return;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    // SDLDrv destroys the renderer when it adopts the window.
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (SDL_CreateWindowAndRenderer("UT2004", 0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN, &window, &renderer))
    {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, bmp);
        int w, h;
        SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
        float scale = SDL_min(0.6f * w / bmp->w, 0.6f * h / bmp->h);
        SDL_FRect dest = { (w - bmp->w * scale) / 2, (h - bmp->h * scale) / 2, bmp->w * scale, bmp->h * scale };
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, &dest);
        SDL_RenderPresent(renderer);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(bmp);
}

static void ExitSplash(void) {}
#else
static SDL_Window* SplashWindow = NULL;

static void InitSplash(const TCHAR *filename)
{
    if (filename == NULL)
        return;

    // A GL backed surface would make its context current and take that away from SDLDrv's.
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    if (!SDL_Init(SDL_INIT_VIDEO))
        return;

    SDL_Surface *bmp = SDL_LoadBMP(appToAnsi(filename));
    if (bmp == NULL)
        return;

    SplashWindow = SDL_CreateWindow("", bmp->w, bmp->h, SDL_WINDOW_BORDERLESS);
    SDL_Surface *screen = SplashWindow ? SDL_GetWindowSurface(SplashWindow) : NULL;
    if (screen != NULL)
    {
        SDL_BlitSurface(bmp, NULL, screen, NULL);
        SDL_UpdateWindowSurface(SplashWindow);
    }

    SDL_DestroySurface(bmp);
}


static void ExitSplash(void)
{
    if (SplashWindow == NULL)
        return;
    SDL_DestroyWindow(SplashWindow);
    SplashWindow = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
#endif
#endif


#ifdef __ANDROID__
#pragma pack(push,8)
#include <SDL3/SDL_main.h>
#include <lzma.h>
#include <zlib.h>
#include <jni.h>
#pragma pack(pop)
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static bool AndroidFail(const char* Fmt, const char* Arg)
{
    char Msg[1024];
    SDL_snprintf(Msg, sizeof(Msg), Fmt, Arg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "UT2004", Msg, NULL);
    return false;
}

static SDL_Window* SetupWindow = NULL;
static SDL_Renderer* SetupRenderer = NULL;
// The app was closed during setup, every step stops at its next read.
static bool SetupQuit = false;

// Shared by the unpack and install steps, so the screen doesn't flicker between them.
static void DrawProgress(const char* Label, float Fraction)
{
    if (!SetupWindow && SDL_Init(SDL_INIT_VIDEO)
        && !SDL_CreateWindowAndRenderer("UT2004", 0, 0, SDL_WINDOW_FULLSCREEN, &SetupWindow, &SetupRenderer))
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_PumpEvents();
    SetupQuit |= SDL_HasEvent(SDL_EVENT_QUIT);
    if (!SetupRenderer)
        return;
    int W, H;
    SDL_GetCurrentRenderOutputSize(SetupRenderer, &W, &H);
    SDL_FRect Back = { W * 0.1f, H * 0.48f, W * 0.8f, H * 0.04f };
    SDL_FRect Bar = Back;
    Bar.w *= SDL_clamp(Fraction, 0.0f, 1.0f);
    SDL_SetRenderDrawColor(SetupRenderer, 0, 0, 0, 255);
    SDL_RenderClear(SetupRenderer);
    SDL_SetRenderDrawColor(SetupRenderer, 60, 60, 60, 255);
    SDL_RenderFillRect(SetupRenderer, &Back);
    SDL_SetRenderDrawColor(SetupRenderer, 40, 120, 230, 255);
    SDL_RenderFillRect(SetupRenderer, &Bar);
    // The debug font is 8 pixels, whole multiples keep it sharp.
    float Scale = SDL_max(1.0f, SDL_floorf(H / 270.0f));
    float Char = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    SDL_SetRenderScale(SetupRenderer, Scale, Scale);
    SDL_SetRenderDrawColor(SetupRenderer, 220, 220, 220, 255);
    SDL_RenderDebugText(SetupRenderer, (W / Scale - SDL_strlen(Label) * Char) / 2, Back.y / Scale - 2 * Char, Label);
    SDL_SetRenderScale(SetupRenderer, 1.0f, 1.0f);
    SDL_RenderPresent(SetupRenderer);
}

static void CloseSetupScreen()
{
    if (!SetupWindow)
        return;
    SDL_DestroyRenderer(SetupRenderer);
    SDL_DestroyWindow(SetupWindow);
    SetupRenderer = NULL;
    SetupWindow = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// Decodes the APK asset <Rel>.xz into Dest.
static bool ExtractAsset(const char* Rel, const char* Dest, lzma_stream& Strm)
{
    static uint8_t InBuf[1 << 16], OutBuf[1 << 18];
    char Path[1024];
    SDL_snprintf(Path, sizeof(Path), "%s.xz", Rel);
    // A relative path is looked up in the APK assets.
    SDL_IOStream* In = SDL_IOFromFile(Path, "rb");
    if (!In)
        return false;
    SDL_strlcpy(Path, Dest, sizeof(Path));
    *SDL_strrchr(Path, '/') = 0;
    SDL_CreateDirectory(Path);
    SDL_IOStream* Out = SDL_IOFromFile(Dest, "wb");
    bool Ok = Out && lzma_stream_decoder(&Strm, UINT64_MAX, 0) == LZMA_OK;
    Strm.avail_in = 0;
    lzma_ret Ret = LZMA_OK;
    while (Ok && Ret != LZMA_STREAM_END)
    {
        if (Strm.avail_in == 0)
        {
            Strm.next_in = InBuf;
            Strm.avail_in = SDL_ReadIO(In, InBuf, sizeof(InBuf));
        }
        Strm.next_out = OutBuf;
        Strm.avail_out = sizeof(OutBuf);
        Ret = lzma_code(&Strm, Strm.avail_in ? LZMA_RUN : LZMA_FINISH);
        size_t Got = sizeof(OutBuf) - Strm.avail_out;
        Ok = (Ret == LZMA_OK || Ret == LZMA_STREAM_END) && SDL_WriteIO(Out, OutBuf, Got) == Got;
    }
    SDL_CloseIO(In);
    if (Out && !SDL_CloseIO(Out))
        Ok = false;
    return Ok;
}

// The engine reads its files with fopen, which can't see into the APK, so they are unpacked once per APK build.
static bool ExtractAssets(const char* Root)
{
    char* Manifest = (char*)SDL_LoadFile("manifest.txt", NULL);
    char* Rel = Manifest ? SDL_strchr(Manifest, '\n') : NULL;
    if (!Rel)
        return AndroidFail("The APK has no %s, it was built without assets.", "manifest.txt");
    *Rel++ = 0;

    char StampPath[1024];
    SDL_snprintf(StampPath, sizeof(StampPath), "%s/assets.stamp", Root);
    char* Stamp = (char*)SDL_LoadFile(StampPath, NULL);
    bool Ok = Stamp && SDL_strcmp(Stamp, Manifest) == 0;
    SDL_free(Stamp);
    if (Ok)
    {
        SDL_free(Manifest);
        return true;
    }

    INT Total = 0;
    for (const char* C = Rel; *C; C++)
        Total += *C == '\n';

    lzma_stream Strm = LZMA_STREAM_INIT;
    Ok = true;
    for (INT Done = 0; Ok && !SetupQuit && *Rel; Done++)
    {
        char* End = SDL_strchr(Rel, '\n');
        *End = 0;
        DrawProgress("Unpacking game files", (float)Done / Total);
        char Dest[1024];
        SDL_snprintf(Dest, sizeof(Dest), "%s/%s", Root, Rel);
        Ok = ExtractAsset(Rel, Dest, Strm);
        if (!Ok)
            AndroidFail("Couldn't unpack %s. Is the device out of storage?", Rel);
        Rel = End + 1;
    }
    lzma_end(&Strm);

    // Written last, so an interrupted unpack starts over on the next launch.
    Ok = Ok && !SetupQuit;
    if (Ok)
        SDL_SaveFile(StampPath, Manifest, SDL_strlen(Manifest));
    SDL_free(Manifest);
    return Ok;
}

// One attempt at installing the content folders from a URL, an archive or a folder.
struct FInstall
{
    const char* Root;
    const char* Src;
    const char* Verb;
    Sint64 Size, Done, Drawn;
    INT Files;
    char Err[1024];

    void Draw()
    {
        char Label[128];
        if (Size > 0)
            SDL_snprintf(Label, sizeof(Label), "%s  %d / %d MB", Verb, (int)(Done >> 20), (int)(Size >> 20));
        else
            SDL_snprintf(Label, sizeof(Label), "%s  %d MB", Verb, (int)(Done >> 20));
        DrawProgress(Label, Size > 0 ? (float)Done / Size : 0.0f);
    }
    void Progress(Sint64 Bytes)
    {
        Done += Bytes;
        if (Done - Drawn >= (1 << 20))
        {
            Drawn = Done;
            Draw();
        }
    }
    bool Fail(const char* Fmt)
    {
        SDL_snprintf(Err, sizeof(Err), Fmt, Src);
        return false;
    }
};

static const char* ContentDirs[] = { "Animations", "Maps", "Music", "Sounds", "StaticMeshes", "Textures" };

// Archives and folders come from anywhere, so only files inside the content folders are written.
// The folders may sit one level down, as in an archive of a whole game folder.
static bool ContentPath(const FInstall& I, const char* Rel, char* Dest, size_t DestSize)
{
    if (SDL_strstr(Rel, ".."))
        return false;
    for (INT Level = 0; Level < 2 && Rel; Level++)
    {
        for (const char* Dir : ContentDirs)
        {
            size_t Len = SDL_strlen(Dir);
            if (SDL_strncasecmp(Rel, Dir, Len) == 0 && Rel[Len] == '/' && Rel[Len + 1])
            {
                // The engine looks the folders up by these names.
                SDL_snprintf(Dest, DestSize, "%s/UT2004/%s%s", I.Root, Dir, Rel + Len);
                return true;
            }
        }
        Rel = SDL_strchr(Rel, '/');
        if (Rel)
            Rel++;
    }
    return false;
}

static SDL_IOStream* CreateContentFile(const char* Dest)
{
    char Dir[1024];
    SDL_strlcpy(Dir, Dest, sizeof(Dir));
    *SDL_strrchr(Dir, '/') = 0;
    SDL_CreateDirectory(Dir);
    return SDL_IOFromFile(Dest, "wb");
}

// Reads a stream with or without xz compression, the first bytes stay buffered to tell the format.
struct FStreamReader
{
    int Fd;
    FInstall& I;
    bool Xz = false, Eof = false;
    lzma_stream Strm = LZMA_STREAM_INIT;
    lzma_ret Ret = LZMA_OK;
    uint8_t In[1 << 16];
    uint8_t* Next = In;
    size_t Avail = 0;

    FStreamReader(int InFd, FInstall& InI) : Fd(InFd), I(InI) {}
    ~FStreamReader() { lzma_end(&Strm); }

    bool Fill()
    {
        if (SetupQuit)
            return false;
        ssize_t Got;
        do
            Got = read(Fd, In + Avail, sizeof(In) - Avail);
        while (Got < 0 && errno == EINTR);
        if (Got <= 0)
        {
            Eof = true;
            return false;
        }
        Avail += Got;
        I.Progress(Got);
        return true;
    }
    void Peek(size_t N)
    {
        while (Avail < N && Fill()) {}
    }
    // Exactly N bytes, false at the end of the stream or on a decoder error.
    bool Read(void* Out, size_t N)
    {
        uint8_t* P = (uint8_t*)Out;
        while (N)
        {
            if (!Avail && !Eof)
            {
                Next = In;
                Fill();
            }
            if (!Xz)
            {
                if (!Avail)
                    return false;
                size_t Count = SDL_min(N, Avail);
                SDL_memcpy(P, Next, Count);
                Next += Count;
                Avail -= Count;
                P += Count;
                N -= Count;
                continue;
            }
            if (Ret != LZMA_OK)
                return false;
            Strm.next_in = Next;
            Strm.avail_in = Avail;
            Strm.next_out = P;
            Strm.avail_out = N;
            Ret = lzma_code(&Strm, Eof ? LZMA_FINISH : LZMA_RUN);
            Next += Avail - Strm.avail_in;
            Avail = Strm.avail_in;
            P += N - Strm.avail_out;
            N = Strm.avail_out;
            if (Ret != LZMA_OK && (Ret != LZMA_STREAM_END || N))
                return false;
        }
        return true;
    }
    bool Fail()
    {
        return I.Fail(Ret == LZMA_MEM_ERROR ? "Not enough memory to unpack %s." : "%s is damaged or incomplete.");
    }
};

static Sint64 TarNumber(const char* Field, INT Len)
{
    // GNU tar stores sizes over 8 GB in base 256.
    if (Field[0] & 0x80)
    {
        Sint64 Value = 0;
        for (INT i = 1; i < Len; i++)
            Value = Value << 8 | (uint8_t)Field[i];
        return Value;
    }
    Sint64 Value = 0;
    for (INT i = 0; i < Len && Field[i] >= '0' && Field[i] <= '7'; i++)
        Value = Value * 8 + Field[i] - '0';
    return Value;
}

static bool TarHeaderValid(const char* Hdr)
{
    unsigned Sum = 0;
    for (INT i = 0; i < 512; i++)
        Sum += i >= 148 && i < 156 ? ' ' : (uint8_t)Hdr[i];
    return Sum == TarNumber(Hdr + 148, 8);
}

// Pax records are "<length> <key>=<value>\n", only the path matters.
static void PaxPath(const char* Data, size_t Len, char* Name, size_t NameSize)
{
    for (const char* P = Data; P < Data + Len; )
    {
        char* Key;
        long RecLen = SDL_strtol(P, &Key, 10);
        if (RecLen <= 0 || *Key != ' ' || P + RecLen > Data + Len)
            return;
        Key++;
        if (SDL_strncmp(Key, "path=", 5) == 0)
            SDL_strlcpy(Name, Key + 5, SDL_min(NameSize, (size_t)(P + RecLen - (Key + 5))));
        P += RecLen;
    }
}

static bool Untar(FStreamReader& R, FInstall& I)
{
    static uint8_t Buf[1 << 18];
    char Hdr[512], LongName[1024] = "";
    while (R.Read(Hdr, sizeof(Hdr)))
    {
        if (!Hdr[0])
            return true;
        if (!TarHeaderValid(Hdr))
            return R.Fail();
        Sint64 Size = TarNumber(Hdr + 124, 12);
        char Type = Hdr[156];
        char Name[1024];
        if (LongName[0])
            SDL_strlcpy(Name, LongName, sizeof(Name));
        // Only POSIX ustar has a name prefix, old GNU headers keep times there.
        else if (SDL_memcmp(Hdr + 257, "ustar", 6) == 0 && Hdr[345])
            SDL_snprintf(Name, sizeof(Name), "%.155s/%.100s", Hdr + 345, Hdr);
        else
            SDL_snprintf(Name, sizeof(Name), "%.100s", Hdr);
        LongName[0] = 0;

        char Dest[1024];
        SDL_IOStream* Out = NULL;
        if ((Type == '0' || Type == 0) && ContentPath(I, Name, Dest, sizeof(Dest)))
        {
            Out = CreateContentFile(Dest);
            if (!Out)
                return I.Fail("Couldn't write the content of %s. Is the device out of storage?");
        }
        // Long names come in an entry of their own just before the file.
        bool Meta = Type == 'L' || Type == 'x';
        char MetaData[sizeof(LongName)];
        size_t MetaLen = 0;
        bool ReadOk = true, WriteOk = true;
        for (Sint64 Left = Size; ReadOk && WriteOk && Left > 0; Left -= (Sint64)sizeof(Buf))
        {
            size_t N = (size_t)SDL_min(Left, (Sint64)sizeof(Buf));
            ReadOk = R.Read(Buf, N);
            if (ReadOk && Out)
                WriteOk = SDL_WriteIO(Out, Buf, N) == N;
            if (ReadOk && Meta && !MetaLen)
            {
                MetaLen = SDL_min(N, sizeof(MetaData) - 1);
                SDL_memcpy(MetaData, Buf, MetaLen);
                MetaData[MetaLen] = 0;
            }
        }
        if (Out && !SDL_CloseIO(Out))
            WriteOk = false;
        if (!WriteOk)
            return I.Fail("Couldn't write the content of %s. Is the device out of storage?");
        if (!ReadOk || !R.Read(Buf, (512 - Size % 512) % 512))
            return R.Fail();
        I.Files += Out != NULL;
        if (Type == 'L')
            SDL_strlcpy(LongName, MetaData, sizeof(LongName));
        else if (Type == 'x')
            PaxPath(MetaData, MetaLen, LongName, sizeof(LongName));
    }
    return R.Fail();
}

static uint32_t Le16(const uint8_t* P) { return P[0] | P[1] << 8; }
static uint32_t Le32(const uint8_t* P) { return Le16(P) | (uint32_t)Le16(P + 2) << 16; }
static uint64_t Le64(const uint8_t* P) { return Le32(P) | (uint64_t)Le32(P + 4) << 32; }

static bool ReadAt(int Fd, void* Out, size_t N, Sint64 Offset)
{
    return !SetupQuit && pread(Fd, Out, N, Offset) == (ssize_t)N;
}

// Writes one zip entry, stored or deflated, to Dest.
static bool UnzipEntry(int Fd, FInstall& I, Sint64 Offset, Sint64 CSize, INT Method, const char* Dest)
{
    static uint8_t In[1 << 16], Out[1 << 18];
    SDL_IOStream* File = CreateContentFile(Dest);
    if (!File)
        return I.Fail("Couldn't write the content of %s. Is the device out of storage?");
    z_stream Z = {};
    bool Ok = Method == 0 || inflateInit2(&Z, -MAX_WBITS) == Z_OK;
    bool Inflating = Ok && Method == 8;
    bool WriteOk = true;
    for (Sint64 Left = CSize; Ok && WriteOk; )
    {
        if (!Z.avail_in)
        {
            if (!Left)
            {
                // Stored data ends with its size, a deflate stream has to reach its end marker first.
                Ok = Method == 0;
                break;
            }
            size_t N = (size_t)SDL_min(Left, (Sint64)sizeof(In));
            Ok = ReadAt(Fd, In, N, Offset);
            Offset += N;
            Left -= N;
            I.Progress(N);
            Z.next_in = In;
            Z.avail_in = N;
            continue;
        }
        if (Method == 0)
        {
            WriteOk = SDL_WriteIO(File, In, Z.avail_in) == Z.avail_in;
            Z.avail_in = 0;
            continue;
        }
        Z.next_out = Out;
        Z.avail_out = sizeof(Out);
        int Ret = inflate(&Z, Z_NO_FLUSH);
        size_t Got = sizeof(Out) - Z.avail_out;
        Ok = Ret == Z_OK || Ret == Z_STREAM_END;
        WriteOk = SDL_WriteIO(File, Out, Got) == Got;
        if (Ret == Z_STREAM_END)
            break;
    }
    if (Inflating)
        inflateEnd(&Z);
    if (!SDL_CloseIO(File))
        WriteOk = false;
    if (!WriteOk)
        return I.Fail("Couldn't write the content of %s. Is the device out of storage?");
    return Ok || I.Fail("%s is damaged.");
}

// Zip keeps its index at the end, so it needs a seekable local file.
static bool Unzip(int Fd, FInstall& I)
{
    struct stat St;
    if (fstat(Fd, &St) != 0 || !S_ISREG(St.st_mode))
        return I.Fail("%s can't be read as a local file, which zip archives need.");
    static uint8_t Tail[22 + 65535];
    Sint64 TailLen = SDL_min((Sint64)St.st_size, (Sint64)sizeof(Tail));
    if (!ReadAt(Fd, Tail, TailLen, St.st_size - TailLen))
        return I.Fail("Couldn't read %s.");
    Sint64 End = TailLen - 22;
    while (End >= 0 && Le32(Tail + End) != 0x06054b50)
        End--;
    if (End < 0)
        return I.Fail("%s is damaged.");
    uint64_t Count = Le16(Tail + End + 10), DirSize = Le32(Tail + End + 12), DirOffset = Le32(Tail + End + 16);
    uint8_t Zip64[56];
    if (End >= 20 && Le32(Tail + End - 20) == 0x07064b50)
    {
        if (!ReadAt(Fd, Zip64, sizeof(Zip64), Le64(Tail + End - 12)) || Le32(Zip64) != 0x06064b50)
            return I.Fail("%s is damaged.");
        Count = Le64(Zip64 + 32);
        DirSize = Le64(Zip64 + 40);
        DirOffset = Le64(Zip64 + 48);
    }
    if (DirOffset + DirSize > (uint64_t)St.st_size)
        return I.Fail("%s is damaged.");
    uint8_t* Dir = (uint8_t*)SDL_malloc(DirSize);
    bool Ok = Dir && ReadAt(Fd, Dir, DirSize, DirOffset);
    if (!Ok)
        I.Fail("Couldn't read %s.");
    const uint8_t* P = Dir;
    const uint8_t* DirEnd = Dir + DirSize;
    for (uint64_t i = 0; Ok && i < Count; i++)
    {
        if (P + 46 > DirEnd || Le32(P) != 0x02014b50 || P + 46 + Le16(P + 28) + Le16(P + 30) + Le16(P + 32) > DirEnd)
        {
            Ok = I.Fail("%s is damaged.");
            break;
        }
        INT Flags = Le16(P + 8), Method = Le16(P + 10), NameLen = Le16(P + 28), ExtraLen = Le16(P + 30);
        uint64_t CSize = Le32(P + 20), USize = Le32(P + 24), Local = Le32(P + 42);
        // Zip64 moves the fields that overflowed into an extra record, in this order.
        const uint8_t* ExtraEnd = P + 46 + NameLen + ExtraLen;
        for (const uint8_t* E = P + 46 + NameLen; E + 4 <= ExtraEnd; E += 4 + Le16(E + 2))
        {
            if (Le16(E) != 0x0001)
                continue;
            const uint8_t* F = E + 4;
            const uint8_t* FEnd = SDL_min(E + 4 + Le16(E + 2), ExtraEnd);
            if (USize == 0xFFFFFFFF && F + 8 <= FEnd) { USize = Le64(F); F += 8; }
            if (CSize == 0xFFFFFFFF && F + 8 <= FEnd) { CSize = Le64(F); F += 8; }
            if (Local == 0xFFFFFFFF && F + 8 <= FEnd) Local = Le64(F);
        }
        char Name[1024];
        SDL_snprintf(Name, sizeof(Name), "%.*s", NameLen, (const char*)P + 46);
        // Some Windows tools write backslashes.
        for (char* C = Name; *C; C++)
            if (*C == '\\')
                *C = '/';
        P = ExtraEnd + Le16(P + 32);

        char Dest[1024];
        uint8_t LocalHdr[30];
        if (!Name[0] || Name[SDL_strlen(Name) - 1] == '/' || !ContentPath(I, Name, Dest, sizeof(Dest)))
            I.Progress(CSize);
        else if (Flags & 1)
            Ok = I.Fail("%s is encrypted.");
        else if (Method != 0 && Method != 8)
            Ok = I.Fail("%s uses a compression other than deflate.");
        else if (!ReadAt(Fd, LocalHdr, sizeof(LocalHdr), Local) || Le32(LocalHdr) != 0x04034b50)
            Ok = I.Fail("%s is damaged.");
        else
        {
            Ok = UnzipEntry(Fd, I, Local + 30 + Le16(LocalHdr + 26) + Le16(LocalHdr + 28), CSize, Method, Dest);
            I.Files++;
        }
    }
    SDL_free(Dir);
    return Ok;
}

static bool Unpack(int Fd, FInstall& I)
{
    FStreamReader R(Fd, I);
    R.Peek(512);
    static const uint8_t XzMagic[] = { 0xFD, '7', 'z', 'X', 'Z', 0 };
    if (R.Avail >= 6 && SDL_memcmp(R.In, XzMagic, 6) == 0)
    {
        R.Xz = true;
        if (lzma_stream_decoder(&R.Strm, UINT64_MAX, 0) != LZMA_OK)
            return I.Fail("Not enough memory to unpack %s.");
        return Untar(R, I);
    }
    if (R.Avail >= 4 && SDL_memcmp(R.In, "PK\3\4", 4) == 0)
    {
        // Unzip counts what it reads itself.
        I.Done = I.Drawn = 0;
        return Unzip(Fd, I);
    }
    if (R.Avail >= 512 && TarHeaderValid((const char*)R.In))
        return Untar(R, I);
    return I.Fail("%s is not a .tar.xz, .tar or .zip archive.");
}

// Turns a pending Java exception into the error message.
static bool JavaFailed(JNIEnv* Env, FInstall& I)
{
    jthrowable E = Env->ExceptionOccurred();
    if (!E)
        return false;
    Env->ExceptionClear();
    jclass Throwable = Env->FindClass("java/lang/Throwable");
    jstring Msg = (jstring)Env->CallObjectMethod(E, Env->GetMethodID(Throwable, "toString", "()Ljava/lang/String;"));
    const char* Chars = Msg ? Env->GetStringUTFChars(Msg, NULL) : NULL;
    SDL_snprintf(I.Err, sizeof(I.Err), "Couldn't get the game content from %s:\n%s", I.Src, Chars ? Chars : "");
    if (Chars)
        Env->ReleaseStringUTFChars(Msg, Chars);
    Env->DeleteLocalRef(Msg);
    Env->DeleteLocalRef(Throwable);
    Env->DeleteLocalRef(E);
    return true;
}

static bool CopyFile(const char* Uri, const char* Dest, FInstall& I)
{
    static uint8_t Buf[1 << 18];
    // SDL opens content:// URIs through the ContentResolver.
    SDL_IOStream* In = SDL_IOFromFile(Uri, "rb");
    if (!In)
        return I.Fail("Couldn't open a file in %s.");
    SDL_IOStream* Out = CreateContentFile(Dest);
    bool WriteOk = Out != NULL;
    size_t Got;
    while (WriteOk && (Got = SDL_ReadIO(In, Buf, sizeof(Buf))) > 0)
        WriteOk = SDL_WriteIO(Out, Buf, Got) == Got;
    bool ReadOk = SDL_GetIOStatus(In) != SDL_IO_STATUS_ERROR;
    SDL_CloseIO(In);
    if (Out && !SDL_CloseIO(Out))
        WriteOk = false;
    if (!WriteOk)
        return I.Fail("Couldn't write the content of %s. Is the device out of storage?");
    return ReadOk || I.Fail("Couldn't read a file in %s.");
}

static bool CopyTree(JNIEnv* Env, jobjectArray Tree, FInstall& I)
{
    jsize Total = Env->GetArrayLength(Tree) / 2;
    bool Ok = true;
    for (jsize i = 0; Ok && !SetupQuit && i < Total; i++)
    {
        jstring JUri = (jstring)Env->GetObjectArrayElement(Tree, 2 * i);
        jstring JRel = (jstring)Env->GetObjectArrayElement(Tree, 2 * i + 1);
        const char* Uri = Env->GetStringUTFChars(JUri, NULL);
        const char* Rel = Env->GetStringUTFChars(JRel, NULL);
        char Dest[1024], Label[128];
        SDL_snprintf(Label, sizeof(Label), "Copying assets  %d / %d files", (int)i, (int)Total);
        DrawProgress(Label, (float)i / Total);
        if (ContentPath(I, Rel, Dest, sizeof(Dest)))
        {
            Ok = CopyFile(Uri, Dest, I);
            I.Files++;
        }
        Env->ReleaseStringUTFChars(JUri, Uri);
        Env->ReleaseStringUTFChars(JRel, Rel);
        Env->DeleteLocalRef(JUri);
        Env->DeleteLocalRef(JRel);
    }
    return Ok;
}

static bool TryInstall(JNIEnv* Env, jclass Activity, FInstall& I)
{
    jstring JSrc = Env->NewStringUTF(I.Src);
    jmethodID ListTree = Env->GetStaticMethodID(Activity, "listAssetTree", "(Ljava/lang/String;)[Ljava/lang/String;");
    jobjectArray Tree = (jobjectArray)Env->CallStaticObjectMethod(Activity, ListTree, JSrc);
    bool Ok = !JavaFailed(Env, I);
    if (Ok && Tree)
        Ok = CopyTree(Env, Tree, I);
    else if (Ok)
    {
        jmethodID Open = Env->GetStaticMethodID(Activity, "openAssetStream", "(Ljava/lang/String;)[J");
        I.Verb = SDL_strncmp(I.Src, "content:", 8) ? "Downloading assets" : "Unpacking assets";
        I.Draw();
        jlongArray Stream = (jlongArray)Env->CallStaticObjectMethod(Activity, Open, JSrc);
        Ok = !JavaFailed(Env, I);
        if (Ok)
        {
            jlong FdSize[2];
            Env->GetLongArrayRegion(Stream, 0, 2, FdSize);
            Env->DeleteLocalRef(Stream);
            I.Size = FdSize[1];
            Ok = Unpack((int)FdSize[0], I);
            close((int)FdSize[0]);
        }
    }
    Env->DeleteLocalRef(Tree);
    Env->DeleteLocalRef(JSrc);
    if (Ok && SetupQuit)
        Ok = I.Fail("Setup was closed.");
    if (Ok && !I.Files)
        Ok = I.Fail("No Animations, Maps, Music, Sounds, StaticMeshes or Textures folders were found in %s.");
    return Ok;
}

// The content is too large for the APK, so it comes from UT_ASSETS_URL on first launch, or from wherever the player points.
static bool InstallContent(const char* Root)
{
    char StampPath[1024];
    SDL_snprintf(StampPath, sizeof(StampPath), "%s/content.stamp", Root);
    if (SDL_GetPathInfo(StampPath, NULL))
        return true;

    JNIEnv* Env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject ActivityObj = (jobject)SDL_GetAndroidActivity();
    jclass Activity = Env->GetObjectClass(ActivityObj);
    Env->DeleteLocalRef(ActivityObj);
    jmethodID Ask = Env->GetStaticMethodID(Activity, "askAssetSource", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

    char Src[1024] = UT_ASSETS_URL, Url[1024] = UT_ASSETS_URL;
    bool Ok = false;
    for (;;)
    {
        FInstall I = {};
        I.Root = Root;
        I.Src = Src;
        if (TryInstall(Env, Activity, I))
        {
            // Written last, so an interrupted install asks again on the next launch.
            SDL_SaveFile(StampPath, Src, SDL_strlen(Src));
            Ok = true;
            break;
        }
        if (SetupQuit)
            break;
        if (SDL_strncmp(Src, "content:", 8) != 0)
            SDL_strlcpy(Url, Src, sizeof(Url));
        // A picker covering the activity destroys the window surface, which must not be current then.
        CloseSetupScreen();
        jstring JErr = Env->NewStringUTF(I.Err), JUrl = Env->NewStringUTF(Url);
        jstring JSrc = (jstring)Env->CallStaticObjectMethod(Activity, Ask, JErr, JUrl);
        Env->ExceptionClear();
        Env->DeleteLocalRef(JErr);
        Env->DeleteLocalRef(JUrl);
        if (!JSrc)
            break;
        const char* Chars = Env->GetStringUTFChars(JSrc, NULL);
        SDL_strlcpy(Src, Chars, sizeof(Src));
        Env->ReleaseStringUTFChars(JSrc, Chars);
        Env->DeleteLocalRef(JSrc);
    }
    Env->DeleteLocalRef(Activity);
    return Ok;
}
#endif


//
// Unreal's main message loop.  All windows in Unreal receive messages
// somewhere below this function on the stack.
//

class CMainLoop
{
public:
	inline CMainLoop( UEngine* InEngine, UBOOL InIncludeMessaging = true );
	inline ~CMainLoop(void) noexcept(false);

	inline void	RunLoop(void);
	inline bool	Finished(void) const	{return !GIsRunning || GIsRequestingExit;}

private:
	UEngine *		Engine;
	UBOOL			IncludeMessaging;

	DOUBLE			OldTime;
	DOUBLE			SecondStartTime;
	INT				TickCount;
	DWORD			LastFrameCycles;

	// Benchmarking.
	INT				BMFrames;
	INT				BMDiscardedFrames;
	INT				BMMaxFrames;
	FLOAT			BMSeconds;
	TArray<FString> BMStrings;
	TArray<FLOAT>	BMFrameTimes;

	// Movie recording.
	UBOOL			RecordingMovie;

};

inline CMainLoop::CMainLoop( UEngine* InEngine, UBOOL InIncludeMessaging )
:	Engine(InEngine), IncludeMessaging(InIncludeMessaging)
{
	guard(MainLoopCtor);
	check(Engine);

	// Loop while running.
	GIsRunning			= 1;
	OldTime				= appSeconds();
	SecondStartTime		= OldTime;
	TickCount			= 0;

	// Benchmarking.
	BMFrames			= 0;
	BMDiscardedFrames	= 10;
	BMSeconds			= 0.0f;
	
	BMMaxFrames			= 0; // gam
	Parse(appCmdLine(),TEXT("SECONDS="),BMMaxFrames);
	BMMaxFrames			*= 30;
	
	for( INT i=0; i<(BMMaxFrames-BMDiscardedFrames+1); i++ )
		new(BMStrings) FString(TEXT(""));
	if( BMMaxFrames )
		BMFrameTimes.Add(BMMaxFrames-BMDiscardedFrames+1);

	LastFrameCycles		= appCycles();

	// Seed random number generator with constant seed for benchmarking.
	if( GIsBenchmarking )
		appRandInit( 0 );

	// Movie recording.
	RecordingMovie	= ParseParam(appCmdLine(),TEXT("RECORDMOVIE"));
		
	unguard;
}

inline void	CMainLoop::RunLoop(void)
{
	// Ever really needed ? - Erik
	if( Finished() ) 
		return;

	// Clear stats (will also update old stats).
	GStats.Clear();

    if( GIsBenchmarking )
	{
		if( BMFrames == BMDiscardedFrames )
			BMSeconds = 0.0f;
		else if( BMFrames > BMMaxFrames )
			appRequestExit(0);
	}

	// Update the world.
	DOUBLE NewTime   = appSeconds();
	FLOAT  DeltaTime = NewTime - OldTime;

	guard(UpdateWorld);
	if( GUseFixedTimeStep )
	{
		GDeltaTime = GFixedTimeStep;
		GCurrentTime += GFixedTimeStep;
		if( BMFrames < BMMaxFrames )
			BMSeconds += DeltaTime;
		BMFrames++;
	}
	else
	{
		GCurrentTime = NewTime;
		GDeltaTime = DeltaTime;
	}

	//Update.
	if( GDeltaTime < 0.f )
		GDeltaTime = 1 / 20.f;
	Engine->Tick( GDeltaTime );
	// sjs --- engine::tick may load a new map and cause the timing to be reset (this is a good thing)
	if( appSeconds() < NewTime )
           SecondStartTime = NewTime = GCurrentTime = appSeconds();
	// --- sjs
	if( GWindowManager )
		GWindowManager->Tick( GDeltaTime );

	TickCount++;
	OldTime = NewTime;

	if( OldTime > SecondStartTime + 1 )
	{
		if( GUseFixedTimeStep )
			Engine->CurrentTickRate = 1 / GFixedTimeStep;
		else
			Engine->CurrentTickRate = (FLOAT)TickCount / (OldTime - SecondStartTime);
		SecondStartTime = OldTime;
		TickCount = 0;
	}
	unguard;

	// Enforce optional maximum tick rate.
	guard(EnforceTickRate);
	if( !GUseFixedTimeStep && IncludeMessaging )
	{		
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
		// Nobody sees a minimized or covered window, so it does not need more frames than the game needs ticks.
		SDL_Window* Window = SDL_GL_GetCurrentWindow();
		if( Window && (SDL_GetWindowFlags( Window ) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) )
			MaxTickRate = MaxTickRate > 0.f ? Min( MaxTickRate, 30.f ) : 30.f;
		if( MaxTickRate>0.0 )
		{
			// Sleep alone wakes up to a millisecond late.
			DOUBLE Delta = 1.0 / MaxTickRate - (appSeconds() - OldTime);
			if( Delta > 0.0 )
				SDL_DelayPrecise( (Uint64)(Delta * 1000000000.0) );
		}
	}
	unguard;

	if( IncludeMessaging && Engine->Client )
	{
		// Handle all incoming messages.
		guard(MessagePump);
		Engine->Client->Viewports(0)->UpdateInput(0, 0);
		unguard;
	}

	if( RecordingMovie && Engine->Client )
		Engine->Client->Viewports(0)->Exec(TEXT("shot"));

	GStats.DWORDStats( GEngineStats.STATS_Frame_TotalCycles ) = appCycles() - LastFrameCycles;
	GStats.DWORDStats( GEngineStats.STATS_Game_ScriptCycles ) = GScriptCycles;
	GScriptCycles	= 0;
	LastFrameCycles = appCycles();

	if( GIsBenchmarking )
	{
		if( BMFrames == 1 )
		{
			// Get descriptions.
			GStats.UpdateString( BMStrings(0), 1 );
			BMFrameTimes(0) = 0.f;
		}
		else
		{
			// Get frame stats. (Index already implies the +1 for descriptions)
			INT BMStringsIndex = Clamp( BMFrames - BMDiscardedFrames, 2, BMMaxFrames - BMDiscardedFrames );
			GStats.UpdateString( BMStrings(BMStringsIndex - 1), 0 );
			BMFrameTimes( BMStringsIndex - 1 ) = GStats.DWORDStats( GEngineStats.STATS_Frame_TotalCycles ) * GSecondsPerCycle * 1000.f;
		}
	}
}

inline CMainLoop::~CMainLoop(void) noexcept(false)
{
	guard(CMainLoopDtor);

	GIsRunning = 0;
	if( GIsBenchmarking )
	{
		INT Frames		= BMMaxFrames - BMDiscardedFrames; 
		UBOOL LogLowFPS = ParseParam(appCmdLine(),TEXT("ONLYLOGLOWFPS"));
		FLOAT LowFPS	= 100.f; // in ms

		// Level.
		FString LevelName = Engine->Client->Viewports(0)->Actor->GetViewTarget()->XLevel->GetPathName();
		LevelName = LevelName.LeftChop( 8 );

		// Get time & date.
		INT Year, Month, DayOfWeek, Day, Hour, Minutes, Sec, MSec;
		appSystemTime( Year, Month, DayOfWeek, Day, Hour, Minutes, Sec, MSec );
		FString DateTime = FString::Printf(TEXT("%i-%02i-%02i-%02i-%02i-%02i"),Year,Month,Day,Hour,Minutes,Sec);

		// Machine Details.
		FString MachineString = FString::Printf(TEXT("%s\r\n%s\r\n%s\r\n%s\r\n\r\n%s\r\n\r\n"),GBuildLabel,GMachineOS,GMachineCPU,GMachineVideo,appCmdLine());
		FString OutputString = MachineString;
		FString LowFPSString = TEXT("");

		// Count how many frames take more than LowFPS ms.
		if( LogLowFPS )
		{
			INT Count = 0;
			for( INT i=1; i<Frames; i++ )	
				if( BMFrameTimes(i) > LowFPS )
					Count++;
			
			LowFPSString = FString::Printf(TEXT("High frametimes: %i / %i == %f percent \r\n\r\n"), Count, Frames, 100.f * FLOAT(Count) / Frames ); 
			OutputString += LowFPSString;
		}

		// Determine min/ max framerate and score.
		FLOAT	MinFPS = 1000.f,
				MaxFPS = 0.f,
				AvgFPS = Frames / BMSeconds,
				Score  = 0.f;

		// Check for FRAMETIMECAP command line option.
		FLOAT FPSCap = 0;
		Parse(appCmdLine(),TEXT("MAXFPS="),FPSCap);
		if( FPSCap <= 0 )
			FPSCap = 80;
		FLOAT FrameTimeCap = 1000.f / FPSCap;

		for( INT i=1; i<Frames; i++ )
		{
			// Calculate min/max.
			FLOAT FPS = 1000.f / BMFrameTimes(i);
			MinFPS = Min( MinFPS, FPS );
			MaxFPS = Max( MaxFPS, FPS );

			// Calculate score.
			Score += Max( BMFrameTimes(i), FrameTimeCap ) ;
		}

		Score = Frames / Score * 1000.f;

		// Output detailed results to a file.
		INT LastRand = appRand();
		for( INT i=0; i<Frames; i++ )
		{
			if( LogLowFPS && i!=0 )
			{
				if( BMFrameTimes(i) > LowFPS )
					OutputString += BMStrings(i);
			}
			else
				OutputString += BMStrings(i);
		}
		TCHAR File[1024];
		appSprintf( File, TEXT("..\\Benchmark\\CSVs\\stats-%s.csv"), *DateTime );
		appSaveStringToFile( OutputString, File );

		// Output average framerate.
		OutputString = TEXT("");
		appLoadFileToString( OutputString, TEXT("..\\Benchmark\\benchmark.log") );
		OutputString += FString::Printf(TEXT("%f / %f / %f fps -- Score = %f        rand[%i]\r\n"), MinFPS, AvgFPS, MaxFPS, Score, LastRand );
		appSaveStringToFile( OutputString, TEXT("..\\Benchmark\\benchmark.log") );
		OutputString = MachineString;
		if( LogLowFPS )
			OutputString += LowFPSString;
		OutputString += FString::Printf(TEXT("%f / %f / %f fps         rand[%i]\r\nScore = %f\r\n"), MinFPS, AvgFPS, MaxFPS, LastRand, Score );
		appSaveStringToFile( OutputString, *FString::Printf(TEXT("..\\Benchmark\\Results\\avgfps-%s.log"), *DateTime ) );
	
		// Output low framerate stats.
		OutputString = TEXT("");
		appLoadFileToString( OutputString, TEXT("..\\Benchmark\\lowframerate.log") );
		OutputString += FString::Printf(TEXT("%s\r\n%f / %f / %f fps\r\nScore = %f\r\n%s\r\n"), *LevelName, MinFPS, AvgFPS, MaxFPS, Score, *LowFPSString );
		appSaveStringToFile( OutputString, TEXT("..\\Benchmark\\lowframerate.log") );

		GLog->Flush();
		GFileManager->Copy( *FString::Printf(TEXT("..\\Benchmark\\Logs\\ut2004-%s.log"),*DateTime), TEXT("ut2004.log") );
	}

	unguard;
}

static void MainLoop( UEngine* Engine )
{
	guard(MainLoop);
	check(Engine);

	CMainLoop* theLoop = new CMainLoop(Engine);
	while (!theLoop->Finished()) 
	{
		theLoop->RunLoop();
	}
	
	delete theLoop;

	unguard;
}


static UEngine* InitEngine()
{
	guard(InitEngine);
	DOUBLE LoadTime = appSeconds();

	// Set exec hook.
	GExec = NULL;

	// First-run menu.
	INT FirstRun=0;
	GConfig->GetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );

	// Older inis and the retail menu still name drivers that no longer exist.
	FString Viewport = GConfig->GetStr(TEXT("Engine.Engine"),TEXT("ViewportManager"),TEXT("System"));
	if( Viewport!=TEXT("SDLDrv.SDLClient") )
	{
		debugf(TEXT("Forcing use of SDLDrv instead of %s."),*Viewport);
		GConfig->SetString(TEXT("Engine.Engine"), TEXT("ViewportManager"), TEXT("SDLDrv.SDLClient"));
	}
	FString Renderer = GConfig->GetStr(TEXT("Engine.Engine"),TEXT("RenderDevice"),TEXT("System"));
	if( Renderer!=TEXT("OpenGLDrv.OpenGLRenderDevice") && Renderer!=TEXT("Engine.NullRenderDevice") )
	{
		debugf(TEXT("Forcing use of OpenGLDrv instead of %s."),*Renderer);
		GConfig->SetString(TEXT("Engine.Engine"), TEXT("RenderDevice"), TEXT("OpenGLDrv.OpenGLRenderDevice"));
	}

	if( ParseParam(appCmdLine(),TEXT("FirstRun")) )
		FirstRun=0;
	if( FirstRun<220 )
	{
		// Migrate savegames.
		TArray<FString> Saves = GFileManager->FindFiles( TEXT("../Save/*.usa"), 1, 0 );
		for( TArray<FString>::TIterator It(Saves); It; ++It )
		{
			INT Pos = appAtoi(**It+4);
			FString Section = TEXT("UnrealShare.UnrealSlotMenu");
			FString Key     = FString::Printf(TEXT("SlotNames[%i]"),Pos);
			if( appStricmp(GConfig->GetStr(*Section,*Key,TEXT("user")),TEXT(""))==0 )
				GConfig->SetString(*Section,*Key,TEXT("Saved game"),TEXT("user"));
		}
	}

	// EXEC from command-line
	FString Command;
	if( Parse(appCmdLine(),TEXT("consolecommand="), Command) )
	{
        if (GExec == NULL)
        {
            debugf(TEXT("Can't use consolecommand"));
        }
        else
        {
		    debugf(TEXT("Executing console command %s"),*Command);
    		GExec->Exec( *Command, *GLog );
        }
		return NULL;
	}

	// Test render device.
	FString Device;
	if( Parse(appCmdLine(),TEXT("testrendev="),Device) )
	{
		debugf(TEXT("Detecting %s"),*Device);
		try
		{
			UClass* Cls = LoadClass<URenderDevice>( NULL, *Device, NULL, 0, NULL );
			GConfig->SetInt(*Device,TEXT("DescFlags"),RDDESCF_Incompatible);
			GConfig->Flush(0);
			if( Cls )
			{
				URenderDevice* RenDev = ConstructObject<URenderDevice>(Cls);
				if( RenDev )
				{
					if( RenDev->Init() )
					{
						debugf(TEXT("Successfully detected %s"),*Device);
					}
					else
					{
						delete RenDev;
						RenDev = NULL;
					}
				}
			}
		} catch( ... ) {}
		FArchive* Ar = GFileManager->CreateFileWriter(TEXT("Detected.ini"),0);
		if( Ar )
			delete Ar;
		return NULL;
	}

    // gam ---
    if( ParseParam(appCmdLine(),TEXT("debugging")) && (GFileManager->FileSize(TEXT("Running.ini")) >= 0) )
        GFileManager->Delete(TEXT("Running.ini"),0,0);
    // --- gam

	// Create is-running semaphore file.
	FArchive* Ar = GFileManager->CreateFileWriter(TEXT("Running.ini"),0);
	if( Ar )
		delete Ar;

	// Update first-run.
	if( FirstRun<ENGINE_VERSION )
		FirstRun = ENGINE_VERSION;
	GConfig->SetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );


	// Create the global engine object.
	UClass* EngineClass;
	if( !GIsEditor )
	{
		// Create game engine.
		EngineClass = UObject::StaticLoadClass( UGameEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.GameEngine"), NULL, LOAD_NoFail, NULL );
	}
	else
	{
		// Editor.
		EngineClass = UObject::StaticLoadClass( UEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail, NULL );
	}
	UEngine* Engine = ConstructObject<UEngine>( EngineClass );
	Engine->Init();
	debugf( TEXT("Startup time: %f seconds"), appSeconds()-LoadTime );

	return Engine;
	unguard;
}



//
// Main entry point.
// This is an example of how to initialize and launch the engine.
//
int main(int argc, char **argv)
{
#ifdef __ANDROID__
	SDL_SetHint( SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight" );
	// Back opens and closes menus like Escape instead of closing the app.
	SDL_SetHint( SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1" );
	// App specific storage needs no permission, and players can still reach the inis over USB.
	const char* Root = SDL_GetAndroidExternalStoragePath();
	if( !Root )
		Root = SDL_GetAndroidInternalStoragePath();
	bool Installed = ExtractAssets(Root) && InstallContent(Root);
	CloseSetupScreen();
	if( !Installed )
		return 1;
	// Apps have no usable HOME, which is where the download cache would go otherwise.
	if( const char* Cache = SDL_GetAndroidCachePath() )
		setenv( "XDG_CACHE_HOME", Cache, 1 );
	// The engine runs from System/ like on the desktop.
	char SystemDir[1024];
	SDL_snprintf( SystemDir, sizeof(SystemDir), "%s/UT2004/System", Root );
	if( chdir(SystemDir) != 0 )
		return AndroidFail( "Couldn't open %s.", SystemDir ), 1;
#endif
	TCHAR CmdLine[1024] = TEXT("");
#ifdef WIN32
	// appInit skips the executable name on Windows, as GetCommandLine starts with it.
	appStrncpy( CmdLine, GetCommandLine(), ARRAY_COUNT(CmdLine) );
#else
    if (argc >= 2)
    {
	    appStrcpy(CmdLine, appFromAnsi(argv[1]));
	    for (INT i = 2; i < argc; i++)
    	{
       		appStrcat(CmdLine, TEXT(" "));
    		appStrcat(CmdLine, appFromAnsi(argv[i]));
    	}
    }
#endif

	// Remember instance.
	INT ErrorLevel = 0;
	GIsStarted     = 1;

#ifdef WIN32
	hInstance = GetModuleHandle( NULL );
#endif
	strcpy( GModule, appToAnsi(TEXT("UT2004")) );
	appStrcpy( GPackage, appPackage() );

	// Begin guarded code.
#ifndef _DEBUG
	try
	{
#endif

		// Init core.
		GIsClient = GIsGuarded = 1;
		appInit( GPackage, CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );

#if __STATIC_LINK
        // Clean lookups.
        for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
            GNativeLookupFuncs[k] = NULL;

        INT Lookup = 0;
        // Core natives.
        GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
        GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;

        // auto-generated lookups and statics
        AUTO_INITIALIZE_REGISTRANTS_ENGINE;
        AUTO_INITIALIZE_REGISTRANTS_SDLDRV;
        AUTO_INITIALIZE_REGISTRANTS_FIRE;
        AUTO_INITIALIZE_REGISTRANTS_ALAUDIO;
        AUTO_INITIALIZE_REGISTRANTS_OPENGLDRV;
        AUTO_INITIALIZE_REGISTRANTS_IPDRV;
        RegisterNamesXInterface();
        AUTO_INITIALIZE_REGISTRANTS_XINTERFACE;
        AUTO_INITIALIZE_REGISTRANTS_XGAME;
        AUTO_INITIALIZE_REGISTRANTS_UWEB;
        RegisterNamesOnslaught();
        AUTO_INITIALIZE_REGISTRANTS_ONSLAUGHT;
        autoInitializeRegistrantsEditor(Lookup);

        GNativeLookupFuncs[Lookup++] = &FindUWebUWebResponseNative;
        GNativeLookupFuncs[Lookup++] = &FindUWebUWebRequestNative;
        check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );


// The linker drops these as dead code without an explicit reference.
char blah;
snprintf(&blah, 1, "%d%d%d", GLoadedSDLDrv, GLoadedOpenGLDrv, GLoadedALAudio);
#endif

		// Init mode.
		GIsServer     = 1;
		GIsClient     = !ParseParam(appCmdLine(),TEXT("SERVER"));
		GIsEditor     = 0;
		GIsScriptable = 1;
		GLazyLoad     = !GIsClient || ParseParam(appCmdLine(),TEXT("LAZY"));

		// Figure out whether to show log or splash screen.
		UBOOL ShowLog = ParseParam(CmdLine,TEXT("LOG"));

		TCHAR *LogoFName = appStaticString1024();
		if( !Parse( appCmdLine(), TEXT("-USERLOGO="), LogoFName, 1024 ) )
			appSprintf( LogoFName, TEXT("%sLogo.bmp"), GPackage );

		FString Filename = FString(TEXT("../Help")) * LogoFName;
		if ( appStrlen(GModPath)>0 )
			Filename = FString(GModPath) * FString(TEXT("Help")) * GModName + TEXT("Logo.bmp");
		if( GFileManager->FileSize(*Filename)<0 )
			Filename = FString(TEXT("../Help")) * GPackage + TEXT("Logo.bmp"); // gam
		if( (!ShowLog) &&
            (!ParseParam(CmdLine,TEXT("server"))) &&
            (!ParseParam(CmdLine,TEXT("nosplash"))) &&
            !appStrfind(CmdLine,TEXT("TestRenDev")) )
			InitSplash( *Filename );

		// Benchmarking/ movie recording.
		GIsBenchmarking		= ParseParam(appCmdLine(),TEXT("BENCHMARK"));
		GIsRecordingMovie	= ParseParam(appCmdLine(),TEXT("RECORDMOVIE"));
	
		// Fixed timestep.
		FLOAT FixedFPS		= 0.f;
		Parse(appCmdLine(),TEXT("FIXEDFPS="),FixedFPS);
	
		if( GIsRecordingMovie || GIsBenchmarking || FixedFPS > 0.f )
		{
			GUseFixedTimeStep	= 1;
			GFixedTimeStep		= FixedFPS > 0.f ? 1.f / FixedFPS : 1.f / 30.f;
		}

		if( ParseParam(appCmdLine(),TEXT("NULLRENDER")) )
			GConfig->SetString( TEXT("Engine.Engine"		), TEXT("RenderDevice"		), TEXT("Engine.NullRenderDevice"	));

		// Don't update ini files if benchmarking.
		if( GIsBenchmarking || ParseParam(appCmdLine(),TEXT("NOINI")) || ParseParam(appCmdLine(),TEXT("EXITAFTERDEMO")) )
		{
			GConfig->Detach( GIni );
			GConfig->Detach( GUserIni );
		}

		// Init engine.
		UEngine* Engine = InitEngine();

		if( Engine )
		{
			// Hide splash screen.
			ExitSplash();

			// Optionally Exec an exec file
			FString Temp;
			if( Parse(CmdLine, TEXT("EXEC="), Temp) )
			{
				Temp = FString(TEXT("exec ")) + Temp;
				if( Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
					Engine->Client->Viewports(0)->Exec( *Temp, *GLog );
			}

#ifdef BETAEXPIRE
			if ( time(NULL) > (BETAEXPIRE + 30 * 24 * 60 * 60) )
			{
				GIsRequestingExit = 1;
				appMsgf( 0, TEXT("This beta has expired.") );
				while (1) _exit(42);
			}
#endif

			// Start main engine loop, including the Windows message pump.
			if( !GIsRequestingExit )
				MainLoop( Engine );
		}

		// Clean shutdown.
		GFileManager->Delete(TEXT("Running.ini"),0,0);

		GLog->Log( NAME_Title, LocalizeGeneral(TEXT("Exit"),GPackage) );

		appPreExit();
		GIsGuarded = 0;
#ifndef _DEBUG
	}
	catch( ... )
	{
		// Crashed.
		ErrorLevel = 1;
		Error.HandleError();
		// The GUI build has no console, so the error would otherwise only be in the log.
		if( GIsClient )
		{
			// The history is one line, which the message box would size to its full width.
			FString Message = GErrorHist;
			for( INT i; (i=Message.InStr(TEXT(" <- ")))!=-1; )
				Message = Message.Left(i) + TEXT("\n") + Message.Mid(i+4);
			SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "UT2004", appToAnsi(*Message), NULL );
		}
	}
#endif

	// Final shut down.
	appExit();
	GIsStarted = 0;
	return((int) ErrorLevel);
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

