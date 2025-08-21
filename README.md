# Windows Image Viewer (WIC + Direct2D)

Tiny, efficient Windows image viewer that decodes with **WIC** and renders with **Direct2D**.
- Downscales on decode to cap RAM/VRAM usage (`--max N`)
- No external deps (ships with Windows SDK)

## Build (Visual Studio Developer Command Prompt)
cl /EHsc /O2 d2d_viewer.cpp /DNOMINMAX /DUNICODE /D_UNICODE /link /SUBSYSTEM:WINDOWS d2d1.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib

## Run

**Usage**
d2d_viewer.exe <image_path> [--max N]

- `image_path` — file to open (quote paths with spaces).
- `--max N` — optional cap for the longest side in pixels; the image is downscaled **on decode** to save RAM/VRAM (default `1600`).
