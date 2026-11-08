# Movie playback

`src/video/bink_decoder.cpp` decodes the game's original `BIKi` files directly.
It builds as `opencmr2_bink`, a small static library using only the C++ standard
library. The executable does not link FFmpeg or invoke a conversion utility.
Game data stays outside the repository.

The decoder reads the frame index and track metadata, validates packet lengths,
and decodes sequentially with padded YUV420 reference planes. It supports all ten
Bink block types, including scaled, motion, residue and integer DCT blocks.
Output is cropped to the original dimensions and converted to BGRX for the
dynamic movie texture. Alpha planes are decoded when present, but full-screen
playback uses opaque output. Other Bink revisions and Bink 2 are rejected.

Sound is decoded to interleaved float PCM, including RDFT and DCT transforms,
overlap between audio blocks and the valid sample count in the final packet.
`Movie_Open` selects the requested Bink track ID; missing IDs fall back to the
first track and `-1` disables sound. The game's intro files have one stereo RDFT
track; country videos have five language tracks.

The audio layer resamples queued movie PCM into its existing SDL3 mixer. Sound
starts at the first presentation, alongside the millisecond playback clock.
Closing or skipping the movie discards its audio queue. The movie loop retains
the final frame and waits for its display interval before closing. Corrupt input
logs an error, stops movie audio and ends playback; audio-device failure permits
silent video playback with a log message.

The blocking movie loop dispatches SDL events both before decoding and while
waiting for the next frame. Enter (including keypad Enter) skips the movie;
the skip key is consumed rather than queued as a menu confirmation. Joystick
confirmation still uses the game's normal input mapping. Movies keep their
proportions: frames are centred in the requested rectangle (the whole back
buffer unless the game gives one) with black bars, and the back buffer is
presented with its own aspect ratio, so wide and tall windows show bars instead
of a stretched picture. `[video] stretch_movies = 1` fills the rectangle and
the window instead, as earlier versions did.
Each movie pass saves and restores the renderer's drawing state and the caller
restores its viewport. This keeps the game's cached state setters consistent:
fonts and menu sprites retain their texture operations and transparency after
the intro or a country video.

## Amiga movie frames: stream textures (phase 5 of the Bink plan)

The Amiga build will not copy decoded frames into a CPU texture. `src/render/minigl/gfx_minigl_movie.h`
adds stream textures for it: `GfxMiniGL_CreateStreamTexture`,
`GfxMiniGL_GetStreamTextureSize` and `GfxMiniGL_UploadRows`. A stream texture has no CPU
copy (`data == NULL`), so every path that reads `texture->data` skips it and its `dirty` flag
stays 0; it binds and draws like any other texture, and its GL name appears on the first upload.

Measured on the PiStorm (MiniGL 29.1, `build/amiga/minigl-movie-upload-pistorm.log`):

- **A 640x480 texture is not sampled correctly.** Rows shift from x = 32 on and the last
  pixels are never written (284,672 of 307,200 pixels wrong). The stream texture is therefore
  padded to the next powers of two (1024x512): only its top-left 640x480 is filled, and the frame
  is drawn with texture coordinates up to 640/1024 and 480/512. With that, the full frame and two
  replaced row runs read back exactly (0 pixels over tolerance, worst channel difference 0).
  The plan had assumed non-power-of-two sizes were fine from reading the library source.
- **Upload cost.** About 1.5 ms for 48 rows (32 us per row), 14.4 ms for a full frame
  (`glTexSubImage2D`, average of 10), 43 ms once to create the padded texture. A frame whose
  macroblocks are mostly flagged (about 55%) touches nearly every macroblock row, so the upload
  of changed row runs approaches the full-frame figure; this is the largest remaining per-frame
  cost after decoding and conversion.
- **Alternate two textures.** MiniGL protects a texture only against being overwritten between
  draws of the same pass. A render still running from the previous frame can read a texture
  that is overwritten in place, so the movie code uploads frame N to the texture frame N-1 did
  not draw from, and uploads the union of the last two frames' changed rows to each.
- **Edges.** With linear filtering the right and bottom edge of the frame would blend with the
  transparent padding; the movie code insets the texture coordinates by half a texel.

The wrapper is verified inside the game's own renderer when the movie code uses it (phase 7):
a test harness that opens the renderer outside the game crashed in `Gfx_SetVideoMode`, so
`tests/amiga/minigl_movie_upload_test.c` drives MiniGL directly with the same call sequence.

## Tests

The default CTest suite needs no game assets or codec tools. It exercises padded
dimensions, fill/skip/motion blocks, fractional frame rates, RDFT/DCT waveforms,
stereo interleaving, overlap, track selection, truncation and bitstream mutations.
The movie integration test checks texture pitch, duplicate decode calls, audio
startup, clock wrap, the final frame and cleanup after skipping or open failure.
`movie_render_state` runs the movie pass through the real frame recorder and
checks the following menu draw without reapplying its cached render states.

Validation on 2026-10-09 used the ten original movies in the local game copy:
all 8,589 frames matched a reference decode pixel for pixel after applying the
same YUV-to-BGRX conversion. The intro audio and all five UK language tracks
were compared as float PCM; the maximum measured intro difference was
`2.39e-7`. Reference transform padding beyond the final packet's declared sample
count is excluded. Synthetic tests and a complete `cm.bik` decode also ran with
AddressSanitizer/UBSan, and queued output was exercised with SDL's dummy audio
device. Reference comparison used an installed FFmpeg executable only as a
development check; it is not a build, test-suite or runtime requirement.

To also decode every original movie through the standalone decoder:

```sh
cmake --preset linux-x86 -DOPENCMR2_BINK_TEST_DATA_DIR="$HOME/cmr2game"
cmake --build --preset linux-x86
ctest --test-dir build/linux-x86 --output-on-failure
```

`build/linux-x86/tests/bink_decode FILE [TRACK_ID]` decodes a whole movie and
reports frame/sample counts, audio peak, a video hash and a PCM hash. For reference comparison,
optional arguments `BGRX_FILE F32_FILE [FRAME_LIMIT]` dump raw pixels and audio;
an empty filename suppresses that output. Test dumps are not game assets to ship.

On the 68k target the float math differs from the host in the last bits, so
audio is checked by tolerance, not by hash: dump a host reference with
`bink_decode FILE 0 "" REF.f32 FRAMES` and run
`amiga_bink_audio_check FILE REF.f32 [FRAMES]` (maximum absolute difference and
the number of samples over 1e-5). `amiga_bink_planes_check FILE [FRAMES]` does
the same for the planes and the changed-macroblock conversion, against the
decoder's own BGRX output.

### Reference hashes

Recorded 2026-11-07 with `bink_decode FILE TRACK` (x86-64, g++ -O2) on the ten
original movies. Any decoder change, and the Amiga target bench, must reproduce
them. BGRX FNV64 hashes every decoded frame's BGRX bytes; PCM FNV64 hashes the
float samples as four little-endian bytes each (the byte order is explicit so a
big-endian target can compare). The video is identical across audio tracks.

| File | Track | BGRX FNV64 | PCM FNV64 | Samples | Peak |
| --- | --- | --- | --- | --- | --- |
| `intro.bik` | 0 | `8841f58a208e00fe` | `56d13894a6e4807a` | 8114400 | 1.307194 |
| `cm.bik` | 0 | `8692c55c34574b5e` | `5d6c4ef1a582b2ce` | 968436 | 1.013416 |
| `Aus.bik` | 0 | `8863741388b73c24` | `be9049ae2ea37d9f` | 1940400 | 0.976797 |
| `Aus.bik` | 4 | `8863741388b73c24` | `6ab2cbb32a8d0c18` | 1940400 | 0.978715 |
| `Fin.bik` | 0 | `9d52144c7041ae4c` | `a90b811f10cf203c` | 1940400 | 0.975660 |
| `Fin.bik` | 4 | `9d52144c7041ae4c` | `9150241a3a995e6b` | 1940400 | 1.002528 |
| `Fra.bik` | 0 | `6dd0d65def9f41ee` | `11ef7723991479b8` | 1940400 | 0.975660 |
| `Fra.bik` | 4 | `6dd0d65def9f41ee` | `849d6cc1f9c6993f` | 1940400 | 0.913228 |
| `Gre.bik` | 0 | `d3433a815fbce11f` | `3aa22d7f88c585b4` | 1940400 | 0.975660 |
| `Gre.bik` | 4 | `d3433a815fbce11f` | `909146d72f8980b0` | 1940400 | 0.788243 |
| `Ita.bik` | 0 | `57f09899940467d6` | `ee2b45ad07bc878d` | 1940400 | 0.958412 |
| `Ita.bik` | 4 | `57f09899940467d6` | `f3978c83d750e89e` | 1940400 | 0.958412 |
| `Ken.bik` | 0 | `b2c3edfdafe4cc7f` | `655dc3cebbf6d06a` | 1940400 | 0.958412 |
| `Ken.bik` | 4 | `b2c3edfdafe4cc7f` | `0ee548d6f8bb3b54` | 1940400 | 0.798728 |
| `Swe.bik` | 0 | `31e4a07778446361` | `d9a9d4425e9109f6` | 1940400 | 0.975660 |
| `Swe.bik` | 4 | `31e4a07778446361` | `3d8cffd1bd6cbcf1` | 1940400 | 0.839344 |
| `UK.bik` | 0 | `ccff8e99330a5ae2` | `538998dfcb5f2eb5` | 1940400 | 0.958412 |
| `UK.bik` | 4 | `ccff8e99330a5ae2` | `c04e0d11f7e10b02` | 1940400 | 0.958412 |

## Source attribution

The video bundle/coefficient decoding, integer inverse DCT and format tables
are adapted from the narrowly scoped Bink routines in FFmpeg n6.1:

- [bink.c](https://github.com/FFmpeg/FFmpeg/blob/n6.1/libavcodec/bink.c),
  Konstantin Shishkov and Peter Ross.
- [binkdata.h](https://github.com/FFmpeg/FFmpeg/blob/n6.1/libavcodec/binkdata.h) and
  [binkdsp.c](https://github.com/FFmpeg/FFmpeg/blob/n6.1/libavcodec/binkdsp.c),
  Konstantin Shishkov.
- [binkaudio.c](https://github.com/FFmpeg/FFmpeg/blob/n6.1/libavcodec/binkaudio.c),
  Peter Ross and Daniel Verkamp, used as the audio format reference.

Those routines retain their LGPL-2.1-or-later notices, with the licence in
`src/video/COPYING.LGPLv2.1`. OpenCMR2's surrounding implementation remains GPL-3.0.
The file reader, checked bitstream, reference buffers, colour conversion, FFT,
audio queue and platform integration are local code; no FFmpeg infrastructure,
headers or libraries are needed to build or run them.
