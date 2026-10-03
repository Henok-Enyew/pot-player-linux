# Built in CI by .github/workflows/release.yml, which replaces Version with
# the release tag. Local build from a checkout:
#
#   git archive --prefix=top-player-1.0.0/ -o ~/rpmbuild/SOURCES/top-player-1.0.0.tar.gz HEAD
#   rpmbuild -ba packaging/rpm/top-player.spec
Name:           top-player
Version:        1.0.0
Release:        1%{?dist}
Summary:        High-performance, lightweight native media player

License:        MIT
URL:            https://github.com/henok-enyew/pot-player-linux
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(mpv)
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Concurrent)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6OpenGL)
BuildRequires:  cmake(Qt6OpenGLWidgets)
BuildRequires:  pkgconfig(gl)
BuildRequires:  desktop-file-utils
BuildRequires:  /usr/bin/appstreamcli

Requires:       hicolor-icon-theme

%description
Top Player is a high-performance, lightweight native media player for Linux
powered by Qt6 and libmpv: a borderless cyan-on-obsidian skin with an on-screen display, a seekbar with preview thumbnails, a
playlist manager, an audio view with cover art and visualizations, dual
subtitles, a 10-band equalizer and subtitle downloads from OpenSubtitles.com.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
%cmake_build

%install
%cmake_install
# The license is packaged with %%license instead.
rm -rf %{buildroot}%{_datadir}/licenses/%{name}

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/org.github.topplayer.desktop
appstreamcli validate --no-net %{buildroot}%{_metainfodir}/org.github.topplayer.metainfo.xml

%files
%license LICENSE
%doc README.md
%{_bindir}/top-player
%{_datadir}/applications/org.github.topplayer.desktop
%{_metainfodir}/org.github.topplayer.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/org.github.topplayer.svg

%changelog
* Sat Oct 03 2026 Henok Enyew Andargie - 1.0.0-1
- Renamed to Top Player, new icon and cyan skin
- About dialog, click to pause, double-click for fullscreen
- Asynchronous folder scanning and batched playlist loading
- 10-band audio equalizer

* Sat Oct 03 2026 Top Player contributors - 0.2.0-1
- Subtitle search and download from OpenSubtitles.com
- Audio view with cover art, metadata and visualizations
- Playlist manager with folders, sorting, M3U and session restore

* Fri Oct 02 2026 Top Player contributors - 0.1.0-1
- Initial package
