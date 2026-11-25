#include <time.h>  // dodging a macro clash with Unix C runtime headers...

#include "SDLLaunchPrivate.h"
#include "SDLLaunchContent.h"

#pragma pack(push,8)
#include <lzma.h>
#include <jni.h>
#pragma pack(pop)
#include <errno.h>
#include <unistd.h>

// Android has one window, so SDLDrv takes this one over and the logo stays up until the game draws.
// It gets the GL ES 3 context OpenGLDrv asks for, so the window's EGL config suits that one too.
void InitSplash(const TCHAR *filename)
{
    SDL_Surface *bmp = filename ? SDL_LoadBMP(appToUtf8(filename).GetTypedData()) : NULL;
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

bool AndroidFail(const char* Fmt, const char* Arg)
{
    char Msg[1024];
    SDL_snprintf(Msg, sizeof(Msg), Fmt, Arg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "UT2004", Msg, NULL);
    return false;
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
bool ExtractAssets(const char* Root)
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
        // SDL opens content:// URIs through the ContentResolver, a download has nothing to seek in.
        SDL_IOStream* Zip = SDL_strncmp(I.Src, "content:", 8) ? NULL : SDL_IOFromFile(I.Src, "rb");
        if (!Zip)
            return I.Fail("%s can't be read as a local file, which zip archives need.");
        bool Ok = Unzip(Zip, I);
        SDL_CloseIO(Zip);
        return Ok;
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

// The download is unpacked while it arrives, as the device may not have room for the archive too.
static bool DigestMatches(JNIEnv* Env, jclass Activity, int Fd)
{
    static uint8_t Buf[1 << 16];
    // The digest covers every byte of the download, which may go on past the end of the archive.
    for (errno = 0; !SetupQuit && (read(Fd, Buf, sizeof(Buf)) > 0 || errno == EINTR); )
        errno = 0;
    jstring JDigest = (jstring)Env->GetStaticObjectField(Activity, Env->GetStaticFieldID(Activity, "assetDigest", "Ljava/lang/String;"));
    const char* Digest = JDigest ? Env->GetStringUTFChars(JDigest, NULL) : NULL;
    bool Ok = !SetupQuit && Digest && SDL_strcasecmp(Digest, UT_ASSETS_SHA256) == 0;
    if (Digest)
        Env->ReleaseStringUTFChars(JDigest, Digest);
    Env->DeleteLocalRef(JDigest);
    return Ok;
}

static bool TryInstall(JNIEnv* Env, jclass Activity, FInstall& I)
{
    // The full game is too large to unpack while it arrives, and its download has to survive a lost connection.
    if (SDL_strcmp(I.Src, UT_FULL_URL) == 0)
        return InstallFull(I);
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
            // Only the built-in download has a known digest, anything else is the player's own choice.
            bool Checked = UT_ASSETS_SHA256[0] && SDL_strcmp(I.Src, UT_ASSETS_URL) == 0;
            std::vector<std::string> Created;
            CreatedFiles = Checked ? &Created : NULL;
            Ok = Unpack((int)FdSize[0], I);
            CreatedFiles = NULL;
            if (Checked && !(Ok && DigestMatches(Env, Activity, (int)FdSize[0])))
            {
                // Only what this download wrote goes, the folders may hold files the player put there.
                for (const std::string& Path : Created)
                    SDL_RemovePath(Path.c_str());
                if (Ok && !SetupQuit)
                    Ok = I.Fail("%s is not the published game content, its SHA-256 does not match.");
            }
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

static bool JavaSource(JNIEnv* Env, jclass Activity, const char* Method, const char* First, const char* Second, char* Src, size_t SrcSize)
{
    jmethodID Ask = Env->GetStaticMethodID(Activity, Method, "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    jstring JFirst = Env->NewStringUTF(First), JSecond = Env->NewStringUTF(Second);
    jstring JSrc = (jstring)Env->CallStaticObjectMethod(Activity, Ask, JFirst, JSecond);
    Env->ExceptionClear();
    Env->DeleteLocalRef(JFirst);
    Env->DeleteLocalRef(JSecond);
    if (!JSrc)
        return false;
    const char* Chars = Env->GetStringUTFChars(JSrc, NULL);
    SDL_strlcpy(Src, Chars, SrcSize);
    Env->ReleaseStringUTFChars(JSrc, Chars);
    Env->DeleteLocalRef(JSrc);
    return true;
}

// The content is too large for the APK, so the player picks on first launch where it comes from.
bool InstallContent(const char* Root)
{
    char StampPath[1024];
    SDL_snprintf(StampPath, sizeof(StampPath), "%s/content.stamp", Root);
    if (SDL_GetPathInfo(StampPath, NULL))
        return true;

    JNIEnv* Env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject ActivityObj = (jobject)SDL_GetAndroidActivity();
    jclass Activity = Env->GetObjectClass(ActivityObj);
    Env->DeleteLocalRef(ActivityObj);

    char Src[1024], Url[1024] = UT_ASSETS_URL, ContentRoot[1024];
    SDL_snprintf(ContentRoot, sizeof(ContentRoot), "%s/UT2004", Root);
    bool Ok = false;
    for (bool Asked = JavaSource(Env, Activity, "askContentChoice", UT_ASSETS_URL, UT_FULL_URL, Src, sizeof(Src)); Asked; )
    {
        FInstall I = {};
        I.Root = ContentRoot;
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
        Asked = JavaSource(Env, Activity, "askAssetSource", I.Err, Url, Src, sizeof(Src));
    }
    Env->DeleteLocalRef(Activity);
    return Ok;
}

void StartServerService(const TCHAR* Cmd)
{
    JNIEnv* Env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject ActivityObj = (jobject)SDL_GetAndroidActivity();
    jclass Activity = Env->GetObjectClass(ActivityObj);
    Env->DeleteLocalRef(ActivityObj);
    jchar Chars[1024];
    jsize Len = 0;
    while (Cmd[Len] && Len < (jsize)ARRAY_COUNT(Chars))
        Chars[Len] = (jchar)Cmd[Len], Len++;
    jstring JCmd = Env->NewString(Chars, Len);
    Env->CallStaticVoidMethod(Activity, Env->GetStaticMethodID(Activity, "startServer", "(Ljava/lang/String;)V"), JCmd);
    Env->ExceptionClear();
    Env->DeleteLocalRef(JCmd);
    Env->DeleteLocalRef(Activity);
}

// ServerService has no activity, so the folders SDL finds for the game come from Java.
extern "C" JNIEXPORT jint JNICALL Java_org_ut2004_game_ServerService_runServer( JNIEnv* Env, jclass, jstring Dir, jstring Cache, jstring Cmd )
{
	const char* Chars = Env->GetStringUTFChars( Cache, NULL );
	setenv( "XDG_CACHE_HOME", Chars, 1 );
	Env->ReleaseStringUTFChars( Cache, Chars );
	Chars = Env->GetStringUTFChars( Dir, NULL );
	int Failed = chdir( Chars );
	Env->ReleaseStringUTFChars( Dir, Chars );
	if( Failed )
		return 1;
	TCHAR CmdLine[1024];
	jchar Buf[ARRAY_COUNT(CmdLine)];
	jsize Len = Min<jsize>( Env->GetStringLength( Cmd ), ARRAY_COUNT(CmdLine) - 1 );
	Env->GetStringRegion( Cmd, 0, Len, Buf );
	for( jsize i=0; i<Len; i++ )
		CmdLine[i] = Buf[i];
	CmdLine[Len] = 0;
	return RunEngine( CmdLine );
}

extern "C" JNIEXPORT void JNICALL Java_org_ut2004_game_ServerService_stopServer( JNIEnv*, jclass )
{
	GIsRequestingExit = 1;
}
