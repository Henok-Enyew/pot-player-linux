# pot-player-linux

A lightweight desktop media player for Linux built with **Qt 6** and **libmpv**.

## Building

### Dependencies

| Distro | Packages |
| --- | --- |
| Ubuntu / Debian | `build-essential cmake ninja-build pkg-config qt6-base-dev libgl-dev libmpv-dev` |
| Fedora | `gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel mesa-libGL-devel mpv-devel` |

### Compile

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/pot-player /path/to/video.mkv
```

Files can also be opened by dragging them onto the window.

## Controls

| Input | Action |
| --- | --- |
| `Space` | Play / pause |
| `Left` / `Right` | Seek -5 s / +5 s |
| Mouse wheel, `Up` / `Down` | Volume +/- 5 |
| Double-click, `Enter` | Toggle fullscreen |
| `Esc` | Leave fullscreen |
| `Q` | Quit |
| Drag inside window | Move window |
| Drag window edge | Resize window |
