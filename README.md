# Windows Image Viewer (WIC + Direct2D)

Tiny, efficient Windows image viewer that decodes with **WIC** and renders with **Direct2D**.
- Downscales on decode to cap RAM/VRAM usage (`--max N`)
- No external deps (ships with Windows SDK)

## Build (Visual Studio Developer Command Prompt)
cl /EHsc /O2 d2d_viewer.cpp ^
  /DNOMINMAX /DUNICODE /D_UNICODE ^
  /link /SUBSYSTEM:WINDOWS d2d1.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib
