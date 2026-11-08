// The Amiga movie player (src/video/movie_amiga.cpp) on the host, against fake
// renderer, audio and file layers: the port/movie.h contract the game relies on,
// the changed-macroblock upload into two alternating stream textures, frame
// dropping and the one-black-frame fallback.
#include "bink_fixture.h"
#include "platform/amiga/amiga_ahi.h"
#include "platform/amiga/amiga_sys.h"
#include "port/audio.h"
#include "port/gfx.h"
#include "port/movie.h"
#include "port/sys.h"
#include "render/minigl/gfx_minigl_movie.h"

#include <string>

using namespace BinkTest;

struct SysFile { size_t position = 0; };
struct GfxTexture {
    int width, height, glWidth, glHeight;
    bool created = false;
    std::vector<uint8_t> pixels; // glWidth x glHeight RGBA8
};
struct AudioStream {};

namespace {
std::vector<uint8_t> data;
DWORD ticks;
int files, textures, streams, starts, clears, presents, stretchedPresents, savedStates, draws;
int failTextureAfter = -1, uploadedRows;
bool missingFile;
int moviesOption = 1, dropOption = 1, stretchMovies;
std::vector<float> queued;
GfxTexture *bound;
GfxTexture *created[2];
GfxTLVertex drawn[4];

} // namespace

extern "C" SysFile *Sys_OpenFile(const char *, int) {
    if (missingFile) return nullptr;
    ++files; return new SysFile;
}
extern "C" void Sys_CloseFile(SysFile *file) { --files; delete file; }
extern "C" DWORD Sys_GetFileSize(SysFile *) { return data.size(); }
extern "C" DWORD Sys_SeekFile(SysFile *file, LONG offset, int) {
    if (offset < 0 || size_t(offset) > data.size()) return 0xffffffff;
    file->position = offset; return offset;
}
extern "C" DWORD Sys_ReadFile(SysFile *file, void *dst, DWORD count) {
    count = std::min(count, DWORD(data.size() - file->position));
    std::memcpy(dst, data.data() + file->position, count); file->position += count; return count;
}
extern "C" DWORD Sys_GetTicks() { return ticks; }
extern "C" double Sys_GetTicksPrecise() { return ticks; }
extern "C" void Sys_Log(const char *, ...) {}
extern "C" int Sys_GetOption(const char *name, int fallback) {
    const std::string option(name);
    if (option == "video.stretch_movies") return stretchMovies;
    if (option == "video.movies") return moviesOption;
    if (option == "video.movie_frame_drop") return dropOption;
    return fallback;
}
extern "C" uint32_t AmigaAhi_GetLatencyMs(void) { return 100; }
extern "C" unsigned long AmigaSys_GetFreeMemory(void) { return 1000000; }

extern "C" GfxTexture *GfxMiniGL_CreateStreamTexture(int width, int height) {
    if (failTextureAfter == 0) return nullptr;
    if (failTextureAfter > 0) --failTextureAfter;
    int w = 1, h = 1;
    while (w < width) w <<= 1;
    while (h < height) h <<= 1;
    GfxTexture *t = new GfxTexture{width, height, w, h};
    t->pixels.assign(size_t(w) * h * 4, 0xcd);
    created[textures % 2] = t;
    ++textures;
    return t;
}
extern "C" int GfxMiniGL_GetStreamTextureSize(const GfxTexture *t, int *w, int *h) {
    *w = t->glWidth; *h = t->glHeight; return 1;
}
extern "C" int GfxMiniGL_UploadRows(GfxTexture *t, const uint8_t *rgba, int y0, int rows) {
    Check(y0 >= 0 && rows > 0 && y0 + rows <= t->height, "upload rows inside the frame");
    if (!t->created) { y0 = 0; rows = t->height; t->created = true; }
    for (int y = y0; y < y0 + rows; ++y)
        std::memcpy(&t->pixels[size_t(y) * t->glWidth * 4], rgba + size_t(y) * t->width * 4, size_t(t->width) * 4);
    uploadedRows += rows;
    return 1;
}
extern "C" void Gfx_DestroyTexture(GfxTexture *t) { if (t) { --textures; delete t; } }
extern "C" AudioStream *Audio_CreateStream(DWORD rate, int channels) {
    Check(rate == 8000 && channels == 2, "movie audio format");
    ++streams; return new AudioStream;
}
extern "C" BOOL Audio_QueueStream(AudioStream *, const float *samples, DWORD count) {
    queued.insert(queued.end(), samples, samples + count); return TRUE;
}
extern "C" void Audio_StartStream(AudioStream *) { ++starts; }
extern "C" void Audio_ReleaseStream(AudioStream *stream) { if (stream) { --streams; delete stream; } }
DWORD lastClearFlags, lastClearColor;
BOOL lastVsync;
extern "C" void Gfx_Clear(DWORD flags, DWORD color, float) { ++clears; lastClearFlags = flags; lastClearColor = color; }
extern "C" void Gfx_BeginScene() {}
extern "C" void Gfx_EndScene() {}
extern "C" void Gfx_Present(BOOL vsync) { ++presents; lastVsync = vsync; }
extern "C" void Gfx_PresentStretched(BOOL) { ++stretchedPresents; }
extern "C" void Gfx_PushState() { ++savedStates; }
extern "C" void Gfx_PopState() { --savedStates; }
extern "C" void Gfx_SetDepthTest(BOOL) {}
extern "C" void Gfx_SetAlphaBlend(BOOL) {}
extern "C" void Gfx_SetAlphaTest(BOOL) {}
extern "C" void Gfx_SetCullMode(int) {}
extern "C" void Gfx_SetTexture(int, GfxTexture *t) { bound = t; }
extern "C" void Gfx_SetStageColorOp(int, int) {}
extern "C" void Gfx_SetStageColorArg1(int, int) {}
extern "C" void Gfx_SetStageAlphaOp(int, int) {}
extern "C" void Gfx_SetStageAlphaArg1(int, int) {}
extern "C" void Gfx_SetStageMinFilter(int, int) {}
extern "C" void Gfx_SetStageMagFilter(int, int) {}
extern "C" void Gfx_SetStageAddress(int, int) {}
extern "C" void Gfx_DrawPrimitiveTL(int primitive, const GfxTLVertex *vertices, DWORD count) {
    Check(primitive == GFX_TRIANGLESTRIP && count == 4, "movie quad");
    std::copy(vertices, vertices + 4, drawn);
    ++draws;
}

namespace {

// Every pixel of the frame in `t` is grey `value` with alpha 255.
bool Grey(const GfxTexture *t, uint8_t value) {
    for (int y = 0; y < t->height; ++y)
        for (int x = 0; x < t->width; ++x) {
            const uint8_t *p = &t->pixels[(size_t(y) * t->glWidth + x) * 4];
            if (p[0] != value || p[1] != value || p[2] != value || p[3] != 255) return false;
        }
    return true;
}

void PlaysWithSoundAndPacing() {
    auto audio = Audio(false), video = Video(9, 9, 6, 81);
    std::vector<uint8_t> packet(4); Put32(packet, 0, audio.size());
    packet.insert(packet.end(), audio.begin(), audio.end()); packet.insert(packet.end(), video.begin(), video.end());
    data = Container(9, 9, {packet, packet}, 0x2000);
    Movie *movie = Movie_Open("test.bik", 7);
    Check(movie && files == 1 && textures == 2 && streams == 1, "open creates the file, two textures, a stream");
    Check(Movie_GetWidth(movie) == 9 && Movie_GetHeight(movie) == 9 && Movie_GetFrameCount(movie) == 2 &&
          Movie_GetFrameNumber(movie) == 1, "movie size, count and first frame number");
    ticks = 0xfffffff0; // the clock wraps during playback
    Check(!Movie_Wait(movie), "no waiting before the first frame");
    Movie_DecodeFrame(movie); Movie_DecodeFrame(movie);
    Check(queued.size() == 960 && starts == 0, "one decode per frame, sound queued but not started");
    Movie_Present(movie, 0, 0, 1920, 1080);
    Check(starts == 1 && savedStates == 0 && presents == 1, "first present starts the sound, balances state");
    Check(bound == created[0] && Grey(created[0], 76), "frame converted and uploaded to the first texture");
    Check(drawn[0].sx == 420 && drawn[0].sy == 0 && drawn[3].sx == 1500 && drawn[3].sy == 1080,
          "proportions kept with black bars");
    Check(drawn[0].tu == 0.5f / 16 && drawn[0].tv == 0.5f / 16 && drawn[3].tu == 8.5f / 16 &&
          drawn[3].tv == 8.5f / 16, "texture coordinates cover the frame, half a texel in");
    Movie_Present(movie, 0, 0, 640, 480);
    Check(bound == created[1] && Grey(created[1], 76), "the next present draws from the other texture");
    stretchMovies = 1;
    Movie_Present(movie, 0, 0, 1920, 1080);
    Check(stretchedPresents == 1 && drawn[0].sx == 0 && drawn[3].sx == 1920 && bound == created[0],
          "stretch_movies fills the rectangle");
    stretchMovies = 0;
    Movie_NextFrame(movie);
    Check(Movie_GetFrameNumber(movie) == 2, "frame numbers advance");
    ticks += 33 + 99; // frame 2 is due 33 ms after the start, which waits 100 ms of output latency
    Check(Movie_Wait(movie), "the picture waits for the audio latency");
    ticks += 1;
    Check(!Movie_Wait(movie), "frame 2 due across the clock wrap");
    const int uploadsBefore = uploadedRows;
    Movie_DecodeFrame(movie); Movie_Present(movie, 0, 0, 640, 480);
    Check(starts == 1 && queued.size() == 1920, "the sound continues");
    Check(uploadedRows - uploadsBefore == 9, "a changed frame uploads its rows once");
    Movie_NextFrame(movie);
    Check(Movie_GetFrameNumber(movie) > Movie_GetFrameCount(movie) && Movie_Wait(movie),
          "the last frame is kept for its display interval");
    ticks += 34;
    Check(!Movie_Wait(movie), "and then the movie ends");
    Movie_Close(movie);
    Check(files == 0 && textures == 0 && streams == 0, "close releases everything");

    movie = Movie_Open("test.bik", 7); Movie_DecodeFrame(movie); Movie_Close(movie);
    Check(files == 0 && textures == 0 && streams == 0, "a skip releases the queued sound");
}

// Frame 1 fills luma 81, frame 2 luma 150, the rest skip every block. Showing
// frame 3 after dropping frame 2 must still show frame 2's picture.
void DropsLateFramesAndKeepsThePicture() {
    std::vector<std::vector<uint8_t>> frames = {Video(9, 9, 6, 81), Video(9, 9, 6, 150)};
    for (int i = 0; i < 8; ++i) frames.push_back(Video(9, 9, 0, 0));
    data = Container(9, 9, frames);
    Movie *movie = Movie_Open("drop.bik", 0);
    Check(movie && textures == 2 && streams == 0, "a movie without sound opens");
    ticks = 1000;
    const int presentsBefore = presents, drawsBefore = draws;
    Movie_DecodeFrame(movie); Movie_Present(movie, 0, 0, 640, 480); Movie_NextFrame(movie);
    Check(Grey(created[0], 76), "frame 1 shown");
    ticks += 100 + 2000; // far behind: frames 2.. are all late
    int shown = 1;
    for (int i = 1; i < 10; ++i) {
        const int before = presents;
        Movie_DecodeFrame(movie); Movie_Present(movie, 0, 0, 640, 480); Movie_NextFrame(movie);
        shown += presents != before;
        if (i == 3)
            Check(presents == before && !created[1]->created, "frames 2-4 dropped, nothing uploaded");
        if (i == 4)
            Check(presents != before && bound == created[1] && Grey(created[1], 156),
                  "after three drops a frame is shown, with the dropped frames' changes");
    }
    Check(presents - presentsBefore == shown && draws - drawsBefore == shown && shown == 4,
          "frames 1, 5, 9 and the last are shown");
    Movie_Close(movie);
    Check(files == 0 && textures == 0, "closed");

    dropOption = 0;
    movie = Movie_Open("drop.bik", 0);
    ticks = 1000;
    const int noDropBefore = presents;
    for (int i = 0; i < 10; ++i) {
        Movie_DecodeFrame(movie); Movie_Present(movie, 0, 0, 640, 480); Movie_NextFrame(movie);
        ticks += 5000;
    }
    Check(presents - noDropBefore == 10, "video.movie_frame_drop = 0 shows every frame");
    Movie_Close(movie);
    dropOption = 1;
}

void FallsBackToOneBlackFrame(const char *what) {
    const int clearsBefore = clears, presentsBefore = presents, drawsBefore = draws;
    Movie *movie = Movie_Open("test.bik", 0);
    Check(movie != nullptr, what);
    Check(files == 0 && textures == 0 && streams == 0, "the fallback holds no resources");
    Check(Movie_GetWidth(movie) == 640 && Movie_GetHeight(movie) == 480 && Movie_GetFrameCount(movie) == 1 &&
          Movie_GetFrameNumber(movie) == 1, "the fallback is one 640x480 frame");
    Movie_DecodeFrame(movie);
    Movie_Present(movie, 0, 0, 640, 480);
    Check(clears == clearsBefore + 1 && presents == presentsBefore + 1 && draws == drawsBefore,
          "the fallback shows a black frame");
    Check(lastClearColor == 0xff000000 && (lastClearFlags & GFX_CLEAR_TARGET) && lastVsync == TRUE,
          "black, the whole target, with vsync as the host path");
    Movie_NextFrame(movie);
    Check(Movie_GetFrameNumber(movie) == 2 && !Movie_Wait(movie), "and ends at once");
    Movie_NextFrame(movie);
    Check(Movie_GetFrameNumber(movie) == 2, "without running past its count");
    Movie_Close(movie);
}

void Fallbacks() {
    auto video = Video(9, 9, 6, 81);
    data = Container(9, 9, {video, video});
    missingFile = true;
    FallsBackToOneBlackFrame("a missing file falls back");
    missingFile = false;
    moviesOption = 0;
    FallsBackToOneBlackFrame("video.movies = 0 falls back");
    moviesOption = 1;
    failTextureAfter = 1;
    FallsBackToOneBlackFrame("a texture failure falls back");
    failTextureAfter = -1;
    data[0] = 0;
    FallsBackToOneBlackFrame("a broken header falls back");
    Movie *movie = Movie_Open(nullptr, 0);
    Check(movie != nullptr && Movie_GetFrameCount(movie) == 1, "a null path falls back");
    Movie_Close(movie);
    Movie_Close(nullptr);
}

void CorruptPacketEndsTheMovie() {
    auto audio = Audio(false), video = Video(9, 9, 6, 81);
    std::vector<uint8_t> packet(4); Put32(packet, 0, audio.size());
    packet.insert(packet.end(), audio.begin(), audio.end()); packet.insert(packet.end(), video.begin(), video.end());
    data = Container(9, 9, {packet, packet}, 0x2000);
    uint32_t second = uint32_t(data[60]) | uint32_t(data[61]) << 8 | uint32_t(data[62]) << 16 |
                      uint32_t(data[63]) << 24;
    Put32(data, second & ~1u, 0xffffffff);
    Movie *movie = Movie_Open("test.bik", 7);
    Movie_DecodeFrame(movie); Movie_Present(movie, 0, 0, 640, 480); Movie_NextFrame(movie);
    Movie_DecodeFrame(movie);
    Check(Movie_GetFrameNumber(movie) > Movie_GetFrameCount(movie) && !Movie_Wait(movie) && streams == 0,
          "a corrupt packet stops the sound and the movie at once");
    Movie_Present(movie, 0, 0, 640, 480);
    Check(savedStates == 0, "presenting after the failure is harmless");
    Movie_Close(movie);
    Check(files == 0 && textures == 0, "closed after the failure");
}

} // namespace

int main() {
    PlaysWithSoundAndPacing();
    DropsLateFramesAndKeepsThePicture();
    Fallbacks();
    CorruptPacketEndsTheMovie();
    std::puts("movie_amiga: ok");
}
