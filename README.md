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

Right-click anywhere in the window to open the context menu. It is grouped
into **Playback**, **Video**, **Audio**, **Subtitles** and **Window**, and
each item is bound directly to an mpv property or command (`pause`, `speed`,
`loop-file`, `hwdec`, `video-aspect-override`, `video-rotate`, `deinterlace`,
`vid`, `aid`, `sid`, `mute`, `audio-delay`, `sub-visibility`, `sub-delay`,
`sub-scale`, ...). Track lists and check marks reflect mpv's live state each
time the menu opens.

Volume, seek position, speed, delays and other changes are shown in an
on-screen display in the top-left corner (white labels, amber values).

| Input | Action |
| --- | --- |
| `Space` | Play / pause |
| `Left` / `Right` | Seek -5 s / +5 s |
| `Ctrl+Left` / `Ctrl+Right` | Seek -30 s / +30 s |
| Mouse wheel, `Up` / `Down` | Volume +/- 5 |
| `M` | Mute |
| `C` / `X` / `Z` | Speed +0.1 / -0.1 / reset |
| `Ctrl+L` | Loop file |
| `.` / `,` | Subtitle delay +/- 0.1 s |
| `Ctrl+.` / `Ctrl+,` | Audio delay +/- 0.1 s |
| `Alt+H` | Show / hide subtitles |
| `Alt+Up` / `Alt+Down` | Subtitle size |
| `Ctrl+D` | Deinterlace |
| `Ctrl+E` | Screenshot (saved to `~/Pictures`) |
| `Ctrl+O` | Open file |
| `Ctrl+T` | Always on top |
| `Alt+1`..`Alt+4` | Window size 50% / 100% / 150% / 200% |
| Double-click, `Enter` | Toggle fullscreen |
| `Esc` | Leave fullscreen |
| `Q` | Quit |
| Right-click | Context menu |
| Drag inside window | Move window |
| Drag window edge | Resize window |
