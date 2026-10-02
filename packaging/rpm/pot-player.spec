# Built in CI by .github/workflows/release.yml, which replaces Version with
# the release tag. Local build from a checkout:
#
#   git archive --prefix=pot-player-0.1.0/ -o ~/rpmbuild/SOURCES/pot-player-0.1.0.tar.gz HEAD
#   rpmbuild -ba packaging/rpm/pot-player.spec
Name:           pot-player
Version:        0.1.0
Release:        1%{?dist}
Summary:        Lightweight media player with a PotPlayer-style interface

License:        MIT
URL:            https://github.com/henok-enyew/pot-player-linux
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(mpv)
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6OpenGL)
BuildRequires:  cmake(Qt6OpenGLWidgets)
BuildRequires:  pkgconfig(gl)
BuildRequires:  desktop-file-utils
BuildRequires:  /usr/bin/appstreamcli

Requires:       hicolor-icon-theme

%description
Pot Player is a Qt6 media player for Linux built on libmpv. It brings the
look and keyboard workflow of PotPlayer to the Linux desktop: a borderless
dark skin with an on-screen display, a seekbar with preview thumbnails, a
drag-and-drop playlist drawer and dual subtitle support.

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
desktop-file-validate %{buildroot}%{_datadir}/applications/org.github.potlinux.desktop
appstreamcli validate --no-net %{buildroot}%{_metainfodir}/org.github.potlinux.metainfo.xml

%files
%license LICENSE
%doc README.md
%{_bindir}/pot-player
%{_datadir}/applications/org.github.potlinux.desktop
%{_metainfodir}/org.github.potlinux.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/org.github.potlinux.svg

%changelog
* Fri Oct 02 2026 pot-player-linux contributors - 0.1.0-1
- Initial package
