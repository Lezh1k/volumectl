# volumectl

Small PulseAudio volume helper that prints i3blocks-friendly JSON and pops up a tiny slider UI on click.

It listens to the default sink via PulseAudio, prints volume/mute status to stdout, and opens a Raylib + microui slider when it receives click metadata on stdin (for example from i3blocks).

## Features
- PulseAudio sink monitoring and live updates
- i3blocks-compatible JSON output with color and icon
- Click-to-open slider window near the block location
- Keyboard control in the slider (arrow keys, hjkl)
- Lightweight UI (Raylib + microui)

## Dependencies
- CMake 3.16+
- Clang (project defaults to `clang`/`clang++` in `CMakeLists.txt`)
- PulseAudio development headers
- Raylib development headers
- cJSON development headers

On Debian/Ubuntu, the packages are typically:
- `libpulse-dev`, `libraylib-dev`, `libcjson-dev`

On Fedora:
- `pulseaudio-libs-devel`, `raylib-devel`, `cjson-devel`

## Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is `build/volumectl`.

## Run
`volumectl` writes status JSON to stdout and reads click events from stdin. A basic run looks like:

```bash
./build/volumectl
```

When a click event line is received (JSON), it opens a small slider window near the click position:

```json
{"x": 100, "y": 20, "relative_x": 10, "relative_y": 8, "width": 80, "height": 20}
```

Fields:
- `x`, `y`: absolute click position
- `relative_x`, `relative_y`: click position relative to the block
- `width`, `height`: block size

## Configuration
The slider window's font (X11 backend only, `dlg_x.c`) is configurable via environment variables:

- `VOLUMECTL_FONT`: X core font family name (default: `fixed`)
- `VOLUMECTL_FONT_SIZE`: pixel size (default: `20`)

```bash
VOLUMECTL_FONT=courier VOLUMECTL_FONT_SIZE=17 ./build/volumectl
```

These select an [X core bitmap font](https://en.wikipedia.org/wiki/X_logical_font_description) via `XLoadQueryFont`,
**not** a scalable TrueType font — a given family only exists in whatever discrete pixel sizes are actually
installed on the X server. Requesting a size that isn't installed fails to load and silently falls back to the
default `fixed` font (a warning is logged). Before setting `VOLUMECTL_FONT_SIZE`, check which sizes actually exist
for a family:

```bash
xlsfonts -fn '-*-<family>-*-*-*-*-*-*-*-*-*-*-*-*'
# e.g. for the default family:
xlsfonts -fn '-*-fixed-*-*-*-*-*-*-*-*-*-*-*-*'
```

The 7th field of each returned XLFD name (e.g. `...-13-100-...`) is a valid pixel size for that family.

## i3blocks integration (example)
Example block config using click events (adjust to your setup):

```ini
[volume]
command=/path/to/build/volumectl
interval=persist
format=json
```

i3blocks will send click metadata to stdin; `volumectl` will open the slider near the block and update output when the sink changes.

## Third-party
- microui is vendored in `vendor/microui/` (see its LICENSE and README).

## Notes
- The project currently targets PulseAudio; PipeWire users may need the PulseAudio compatibility layer.
- Linux and FreeBSD are the intended platforms; other OSes are not supported.
