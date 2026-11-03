# ShortForge

A blazing-fast, 100% local vertical-video renderer for faceless Shorts/Reels/TikToks — written in C++.

Point it at a JSON project (images + captions + audio) and it renders a 1080x1920 MP4 with smooth Ken Burns zoom/pan, text overlays, and muxed audio. Zero cloud, zero subscription, zero Python overhead: on a GTX 1650 it uses NVENC when available and falls back to x264 automatically.

## Quickstart (Linux)

```bash
sudo apt install cmake g++ pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

python3 assets/gen_samples.py          # make demo images + music bed
./build/shortforge probe               # see available encoders
./build/shortforge plan assets/sample_project.json
./build/shortforge render assets/sample_project.json -o short.mp4
```

Windows build: see [docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md).

## Project file

```jsonc
{
  "width": 1080, "height": 1920, "fps": 30,
  "audio": "assets/music.wav",      // optional, wav/mp3/m4a/...
  "output": "short.mp4",
  "scenes": [
    {
      "image": "assets/scene1.bmp", // png/jpg/bmp/webp — any aspect
      "duration": 4.0,              // seconds
      "zoom": [1.0, 1.15],          // smooth zoom from -> to (>= 1.0)
      "pan": [0.5, 0.5, 0.3, 0.7],  // x0,y0 -> x1,y1 in 0..1 travel range
      "title": "DID YOU KNOW?",     // optional big caption
      "subtitle": "Octopuses have 3 hearts"
    }
  ]
}
```

Asset paths (`image`, `audio`) are resolved relative to the project file, and so
is the project file's own `output` (an absent `output` defaults to `short.mp4`
in the current directory). An explicit `-o/--output` on the command line is
always relative to the current directory. In this example the project file
sits next to the `assets/` folder, so `assets/scene1.bmp` resolves correctly —
don't put the project file inside `assets/` *and* keep the `assets/` prefix. Width/height must be even (H.264).

## How it works

- **Decode** — images via FFmpeg (any format), audio resampled to 44.1 kHz stereo.
- **Ken Burns** — every source is cover-scaled, then a smoothstep-eased crop window zooms/pans across it; aspect-correct at every frame.
- **Text** — embedded 5x7 bitmap font, zero font dependencies, auto-shrinks to fit, drawn over a readability bar.
- **Encode** — auto-picks `h264_nvenc` → `libx264` → `mpeg4`; AAC audio interleaved by presentation timestamp.

## Roadmap

- [ ] Word-by-word caption timing (SRT/ASS input)
- [ ] Transitions (crossfade, whip-pan)
- [ ] Background music ducking under voiceover
- [ ] Batch mode: render 100 projects in one invocation
- [ ] Stock-clip fetcher integration

## License

MIT © 2026 Ravi Yadav — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
