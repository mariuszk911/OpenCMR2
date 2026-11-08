// Bink playback on AmigaOS (port/movie.h), Domain B. The same observable rules
// as the host player (movie.cpp): frame numbers count from 1, the clock and the
// sound start at the first frame shown, the last frame is kept for its display
// interval, and closing discards the queued sound at once.
//
// The picture path differs, for speed: the decoder hands over its YUV planes
// and a map of the 16x16 macroblocks that changed; only those are converted,
// into one persistent RGBA8 frame, and only their pixel rows are uploaded into
// one of two stream textures (gfx_minigl_movie.h explains why two). When the
// machine falls behind, Movie_Present drops frames (decoding still happens, see
// movie_pacing.h), because the game's loop never skips one itself.
//
// Movie_Open never fails: GameInfo.cpp loops on a "no CD" requester while it
// returns NULL, so a missing or broken movie is logged and plays as one black
// frame instead.
#include "port/audio.h"
#include "port/gfx.h"
#include "port/movie.h"
#include "port/sys.h"
#include "platform/amiga/amiga_ahi.h"
#include "platform/amiga/amiga_sys.h"
#include "render/minigl/gfx_minigl_movie.h"
#include "video/bink_decoder.h"
#include "video/movie_pacing.h"
#include "video/yuv_rgba.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>

namespace {

const DWORD kFallbackWidth = 640;
const DWORD kFallbackHeight = 480;
// Movie packets are small (about 10 KB) and read in order; one seek and read
// per packet cost 7-9 ms on the target, so the file is read in large blocks.
const uint32_t kReadBlock = 256 * 1024;

struct Timing {
    double total, max;
    uint32_t count;
};

void Add(Timing *timing, double ms)
{
    timing->total += ms;
    timing->max = ms > timing->max ? ms : timing->max;
    ++timing->count;
}

// Average and maximum in microseconds, for the log (no %f on this target).
unsigned long AverageUs(const Timing &timing)
{
    return timing.count ? (unsigned long)(timing.total * 1000.0 / timing.count) : 0ul;
}

unsigned long MaxUs(const Timing &timing)
{
    return (unsigned long)(timing.max * 1000.0);
}

} // namespace

struct Movie {
    bool playing = false;           // false: the one-black-frame fallback
    SysFile *file = nullptr;
    uint32_t fileSize = 0;
    uint8_t *readBuffer = nullptr;
    uint32_t readOffset = 0, readBytes = 0;

    Bink::Decoder decoder;
    Bink::Frame decoded;
    Bink::Planes planes = {};
    Bink::Info info;

    YuvRgba::Tables *tables = nullptr;
    uint8_t *rgba = nullptr;        // the whole frame, pitch width * 4
    uint8_t *pending = nullptr;     // macroblocks changed since the last conversion
    bool pendingAny = false;
    // Per texture: macroblock rows converted since that texture was uploaded.
    uint8_t *staleRows[2] = {nullptr, nullptr};
    YuvRgba::RowRun *runs = nullptr;
    GfxTexture *textures[2] = {nullptr, nullptr};
    int glWidth = 0, glHeight = 0;
    int nextTexture = 0;
    bool uploaded[2] = {false, false};
    bool uploadFailed = false;

    AudioStream *audio = nullptr;
    uint32_t latencyMs = 0;

    DWORD frame = 0;
    uint32_t startTicks = 0;
    bool decodedCurrent = false, started = false;
    bool dropEnabled = true;
    uint32_t consecutiveDrops = 0;

    uint32_t presented = 0, dropped = 0;
    Timing decode = {}, convert = {}, upload = {}, present = {};
};

namespace {

bool ReadDirect(Movie *movie, uint32_t offset, void *dst, uint32_t bytes)
{
    return offset <= 0x7fffffff && Sys_SeekFile(movie->file, (int32_t)offset, SYS_SEEK_SET) == offset &&
           Sys_ReadFile(movie->file, dst, bytes) == bytes;
}

bool ReadMovie(void *context, uint32_t offset, void *dst, uint32_t bytes)
{
    Movie *movie = static_cast<Movie *>(context);
    if (offset > movie->fileSize || bytes > movie->fileSize - offset)
        return false;
    if (bytes >= kReadBlock)
        return ReadDirect(movie, offset, dst, bytes);
    if (offset < movie->readOffset || offset - movie->readOffset > movie->readBytes ||
        bytes > movie->readBytes - (offset - movie->readOffset)) {
        const uint32_t left = movie->fileSize - offset;
        const uint32_t block = left < kReadBlock ? left : kReadBlock;
        movie->readBytes = 0;
        if (!ReadDirect(movie, offset, movie->readBuffer, block))
            return false;
        movie->readOffset = offset;
        movie->readBytes = block;
    }
    memcpy(dst, movie->readBuffer + (offset - movie->readOffset), bytes);
    return true;
}

// Frees everything Load acquired; the Movie stays valid as the fallback.
void Release(Movie *movie)
{
    Audio_ReleaseStream(movie->audio);
    movie->audio = nullptr;
    for (int i = 0; i < 2; ++i) {
        Gfx_DestroyTexture(movie->textures[i]);
        movie->textures[i] = nullptr;
        free(movie->staleRows[i]);
        movie->staleRows[i] = nullptr;
    }
    delete[] movie->runs;
    movie->runs = nullptr;
    free(movie->pending);
    movie->pending = nullptr;
    free(movie->rgba);
    movie->rgba = nullptr;
    delete movie->tables;
    movie->tables = nullptr;
    free(movie->readBuffer);
    movie->readBuffer = nullptr;
    if (movie->file) {
        Sys_CloseFile(movie->file);
        movie->file = nullptr;
    }
    movie->playing = false;
}

// Returns nullptr on success, or why the movie cannot play.
const char *Load(Movie *movie, const char *path, int audioTrack)
{
    if (path == nullptr)
        return "no path";
    if (Sys_GetOption("video.movies", 1) == 0)
        return "movies are off (video.movies = 0)";
    movie->file = Sys_OpenFile(path, SYS_FILE_READ);
    if (!movie->file)
        return "cannot open the file";
    movie->fileSize = Sys_GetFileSize(movie->file);
    if (movie->fileSize > 0x7fffffff)
        return "file too large";
    movie->readBuffer = static_cast<uint8_t *>(malloc(kReadBlock));
    if (!movie->readBuffer)
        return "no memory for the read buffer";
    if (!movie->decoder.Open({movie, movie->fileSize, ReadMovie}, audioTrack))
        return movie->decoder.Error().c_str();
    movie->info = movie->decoder.GetInfo();
    const Bink::Info &info = movie->info;
    if (info.width == 0 || info.height == 0 || info.width > 4096 || info.height > 4096 ||
        info.fpsNumerator == 0 || info.fpsDenominator == 0 || info.frames == 0)
        return "unsupported picture size or frame rate";

    const uint32_t mbColumns = (info.width + 15) / 16, mbRows = (info.height + 15) / 16;
    movie->tables = new (std::nothrow) YuvRgba::Tables;
    movie->rgba = static_cast<uint8_t *>(calloc(size_t(info.width) * info.height, 4));
    movie->pending = static_cast<uint8_t *>(calloc(size_t(mbColumns) * mbRows, 1));
    movie->runs = new (std::nothrow) YuvRgba::RowRun[mbRows];
    for (int i = 0; i < 2; ++i) {
        movie->staleRows[i] = static_cast<uint8_t *>(malloc(mbRows));
        if (movie->staleRows[i])
            memset(movie->staleRows[i], 1, mbRows);
    }
    if (!movie->tables || !movie->rgba || !movie->pending || !movie->runs || !movie->staleRows[0] ||
        !movie->staleRows[1])
        return "no memory for the frame buffers";
    YuvRgba::InitTables(movie->tables);
    for (int i = 0; i < 2; ++i) {
        movie->textures[i] = GfxMiniGL_CreateStreamTexture(int(info.width), int(info.height));
        if (!movie->textures[i])
            return "cannot create the frame textures";
    }
    if (!GfxMiniGL_GetStreamTextureSize(movie->textures[0], &movie->glWidth, &movie->glHeight))
        return "cannot query the frame texture size";

    if (info.channels) {
        movie->audio = Audio_CreateStream(info.sampleRate, int(info.channels));
        if (movie->audio)
            movie->latencyMs = AmigaAhi_GetLatencyMs();
        else
            Sys_Log("movie %s: audio output unavailable, playing silent", path);
    }
    movie->dropEnabled = Sys_GetOption("video.movie_frame_drop", 1) != 0;
    movie->playing = true;
    return nullptr;
}

// Converts the changed macroblocks into the RGBA frame and marks their rows
// stale in both textures.
void ConvertPending(Movie *movie)
{
    const double start = Sys_GetTicksPrecise();
    const unsigned count = YuvRgba::Convert(*movie->tables, movie->planes, movie->pending, movie->info.width,
                                            movie->info.height, movie->rgba, size_t(movie->info.width) * 4,
                                            movie->runs);
    for (unsigned r = 0; r < count; ++r) {
        const uint32_t first = movie->runs[r].y / 16;
        const uint32_t last = (movie->runs[r].y + movie->runs[r].rows - 1) / 16;
        for (uint32_t row = first; row <= last; ++row)
            movie->staleRows[0][row] = movie->staleRows[1][row] = 1;
    }
    memset(movie->pending, 0, size_t(movie->planes.mbColumns) * movie->planes.mbRows);
    movie->pendingAny = false;
    Add(&movie->convert, Sys_GetTicksPrecise() - start);
}

// Brings texture `index` up to the RGBA frame by uploading its stale row runs.
void UploadStale(Movie *movie, int index)
{
    const double start = Sys_GetTicksPrecise();
    const uint32_t mbRows = (movie->info.height + 15) / 16;
    uint8_t *stale = movie->staleRows[index];
    for (uint32_t row = 0; row < mbRows;) {
        if (!stale[row]) {
            ++row;
            continue;
        }
        uint32_t end = row;
        while (end < mbRows && stale[end])
            stale[end++] = 0;
        const uint32_t y0 = row * 16;
        const uint32_t y1 = end * 16 < movie->info.height ? end * 16 : movie->info.height;
        if (GfxMiniGL_UploadRows(movie->textures[index], movie->rgba, int(y0), int(y1 - y0)))
            movie->uploaded[index] = true;
        else if (!movie->uploadFailed) {
            Sys_Log("movie: frame upload failed");
            movie->uploadFailed = true;
        }
        row = end;
    }
    Add(&movie->upload, Sys_GetTicksPrecise() - start);
}

void DrawFrame(Movie *movie, int index, int x, int y, int width, int height, bool stretch)
{
    const MoviePacing::Rect fitted = MoviePacing::Fit({x, y, width, height}, movie->info.width,
                                                      movie->info.height, stretch);
    // Half a texel in from the frame's edges, so linear filtering never blends
    // in the padding of the power-of-two texture.
    const float u0 = 0.5f / float(movie->glWidth);
    const float v0 = 0.5f / float(movie->glHeight);
    const float u1 = (float(movie->info.width) - 0.5f) / float(movie->glWidth);
    const float v1 = (float(movie->info.height) - 0.5f) / float(movie->glHeight);
    const float x0 = float(fitted.x), y0 = float(fitted.y);
    const float x1 = float(fitted.x + fitted.width), y1 = float(fitted.y + fitted.height);
    GfxTLVertex v[4];
    memset(v, 0, sizeof(v));
    for (int i = 0; i < 4; i++) {
        v[i].rhw = 1.0f;
        v[i].color = 0xffffffff;
    }
    v[0].sx = x0; v[0].sy = y0; v[0].tu = u0; v[0].tv = v0;
    v[1].sx = x1; v[1].sy = y0; v[1].tu = u1; v[1].tv = v0;
    v[2].sx = x0; v[2].sy = y1; v[2].tu = u0; v[2].tv = v1;
    v[3].sx = x1; v[3].sy = y1; v[3].tu = u1; v[3].tv = v1;

    Gfx_BeginScene();
    Gfx_SetDepthTest(FALSE);
    Gfx_SetAlphaBlend(FALSE);
    Gfx_SetAlphaTest(FALSE);
    Gfx_SetCullMode(GFX_CULL_NONE);
    Gfx_SetTexture(0, movie->textures[index]);
    Gfx_SetStageColorOp(0, GFX_TOP_SELECTARG1);
    Gfx_SetStageColorArg1(0, GFX_TA_TEXTURE);
    Gfx_SetStageAlphaOp(0, GFX_TOP_SELECTARG1);
    Gfx_SetStageAlphaArg1(0, GFX_TA_TEXTURE);
    Gfx_SetStageColorOp(1, GFX_TOP_DISABLE);
    Gfx_SetStageAlphaOp(1, GFX_TOP_DISABLE);
    Gfx_SetStageMinFilter(0, GFX_FILTER_LINEAR);
    Gfx_SetStageMagFilter(0, GFX_FILTER_LINEAR);
    Gfx_SetStageAddress(0, GFX_ADDRESS_CLAMP);
    Gfx_DrawPrimitiveTL(GFX_TRIANGLESTRIP, v, 4);
    Gfx_EndScene();
}

} // namespace

extern "C" Movie *Movie_Open(const char *path, int audioTrack)
{
    Movie *movie = new (std::nothrow) Movie;
    if (!movie)
        return nullptr;
    const unsigned long freeBefore = AmigaSys_GetFreeMemory();
    const char *reason = Load(movie, path, audioTrack);
    if (reason) {
        Sys_Log("movie %s: %s; showing one black frame", path ? path : "(null)", reason);
        Release(movie);
        return movie;
    }
    const Bink::Info &info = movie->info;
    Sys_Log("movie %s: %ux%u, %u frames at %u/%u fps, %u Hz/%u channels, audio latency %u ms, "
            "frame drop %s, free memory %lu -> %lu",
            path, (unsigned)info.width, (unsigned)info.height, (unsigned)info.frames,
            (unsigned)info.fpsNumerator, (unsigned)info.fpsDenominator, (unsigned)info.sampleRate,
            (unsigned)info.channels, (unsigned)movie->latencyMs, movie->dropEnabled ? "on" : "off",
            freeBefore, AmigaSys_GetFreeMemory());
    return movie;
}

extern "C" void Movie_Close(Movie *movie)
{
    if (!movie)
        return;
    if (movie->playing) {
        const uint32_t elapsed = movie->started ? Sys_GetTicks() - movie->startTicks : 0;
        const unsigned long fpsHundredths =
            elapsed && elapsed < 0x80000000u ? (unsigned long)((uint64_t)movie->presented * 100000u / elapsed) : 0ul;
        Sys_Log("movie: frame %u of %u, %u shown, %u dropped, %lu.%02lu fps; us avg/max: decode %lu/%lu, "
                "convert %lu/%lu, upload %lu/%lu, present %lu/%lu",
                (unsigned)(movie->frame < movie->info.frames ? movie->frame + 1 : movie->info.frames),
                (unsigned)movie->info.frames, (unsigned)movie->presented, (unsigned)movie->dropped,
                fpsHundredths / 100, fpsHundredths % 100, AverageUs(movie->decode), MaxUs(movie->decode),
                AverageUs(movie->convert), MaxUs(movie->convert), AverageUs(movie->upload), MaxUs(movie->upload),
                AverageUs(movie->present), MaxUs(movie->present));
        Release(movie);
        delete movie; // the decoder's buffers go with it
        Sys_Log("movie: closed, free memory %lu", AmigaSys_GetFreeMemory());
        return;
    }
    delete movie;
}

extern "C" DWORD Movie_GetWidth(Movie *movie)
{
    return movie->playing ? movie->info.width : kFallbackWidth;
}

extern "C" DWORD Movie_GetHeight(Movie *movie)
{
    return movie->playing ? movie->info.height : kFallbackHeight;
}

extern "C" DWORD Movie_GetFrameNumber(Movie *movie)
{
    return movie->frame + 1;
}

extern "C" DWORD Movie_GetFrameCount(Movie *movie)
{
    return movie->playing ? movie->info.frames : 1;
}

extern "C" void Movie_DecodeFrame(Movie *movie)
{
    if (!movie->playing || movie->decodedCurrent || movie->frame >= movie->info.frames)
        return;
    const double start = Sys_GetTicksPrecise();
    const bool ok = movie->decoder.Decode(movie->decoded, &movie->planes);
    Add(&movie->decode, Sys_GetTicksPrecise() - start);
    if (!ok) {
        Sys_Log("movie frame %u: %s", (unsigned)(movie->frame + 1), movie->decoder.Error().c_str());
        Audio_ReleaseStream(movie->audio);
        movie->audio = nullptr;
        movie->frame = movie->info.frames;
        movie->started = false;
        return;
    }
    movie->decodedCurrent = true;
    if (movie->audio && !movie->decoded.audio.empty() &&
        !Audio_QueueStream(movie->audio, movie->decoded.audio.data(), DWORD(movie->decoded.audio.size()))) {
        Sys_Log("movie: cannot queue audio");
        Audio_ReleaseStream(movie->audio);
        movie->audio = nullptr;
    }
    const size_t blocks = size_t(movie->planes.mbColumns) * movie->planes.mbRows;
    for (size_t i = 0; i < blocks; ++i) {
        if (movie->planes.changed[i]) {
            movie->pending[i] = 1;
            movie->pendingAny = true;
        }
    }
}

extern "C" void Movie_NextFrame(Movie *movie)
{
    const DWORD frames = movie->playing ? movie->info.frames : 1;
    if (movie->frame < frames)
        ++movie->frame;
    movie->decodedCurrent = false;
}

extern "C" BOOL Movie_Wait(Movie *movie)
{
    if (!movie->playing || !movie->started)
        return FALSE;
    return MoviePacing::TooEarly(MoviePacing::ElapsedMs(Sys_GetTicks(), movie->startTicks), movie->frame,
                                 movie->info.fpsNumerator, movie->info.fpsDenominator)
               ? TRUE
               : FALSE;
}

extern "C" void Movie_Present(Movie *movie, int x, int y, int width, int height)
{
    if (!movie->playing) {
        Gfx_Clear(GFX_CLEAR_TARGET | GFX_CLEAR_ZBUFFER, 0xff000000, 1.0f);
        Gfx_Present(TRUE);
        return;
    }
    if (movie->decodedCurrent &&
        MoviePacing::ShouldDrop(movie->dropEnabled, movie->started,
                                MoviePacing::ElapsedMs(Sys_GetTicks(), movie->startTicks), movie->frame,
                                movie->info.frames, movie->consecutiveDrops, movie->info.fpsNumerator,
                                movie->info.fpsDenominator)) {
        ++movie->dropped;
        ++movie->consecutiveDrops;
        return;
    }
    movie->consecutiveDrops = 0;

    // The game caches its render-state setters. Leave their actual values
    // intact so the next menu can still blend its fonts and sprites.
    Gfx_PushState();
    const bool stretch = Sys_GetOption("video.stretch_movies", 0) != 0;
    // Clearing locks the display and begins the frame, which the upload needs.
    Gfx_Clear(GFX_CLEAR_TARGET | GFX_CLEAR_ZBUFFER, 0xff000000, 1.0f);
    if (movie->decodedCurrent && movie->pendingAny)
        ConvertPending(movie);
    const int index = movie->nextTexture;
    UploadStale(movie, index);
    const double start = Sys_GetTicksPrecise();
    if (movie->uploaded[index]) {
        DrawFrame(movie, index, x, y, width, height, stretch);
        movie->nextTexture = index ^ 1;
    }
    if (stretch)
        Gfx_PresentStretched(TRUE);
    else
        Gfx_Present(TRUE);
    Gfx_PopState();
    Add(&movie->present, Sys_GetTicksPrecise() - start);
    ++movie->presented;
    if (!movie->started && movie->decodedCurrent) {
        // The sound starts now but is heard after the output latency; the
        // picture waits as long, so both stay together.
        movie->startTicks = Sys_GetTicks() + movie->latencyMs;
        movie->started = true;
        Audio_StartStream(movie->audio);
    }
}
