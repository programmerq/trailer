Name:           trailer
Version:        0.3.0
Release:        1%{?dist}
Summary:        Cross-platform PDF and image workbench
License:        MIT
URL:            https://github.com/trailer-app/trailer
# TODO: Add real Source0 URL once releases are published

BuildRequires:  cmake >= 3.24
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  python3-pip
BuildRequires:  qpdf-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  glib2-devel
# Qt 6.11 with qtpdf is provided out-of-band during build (via aqtinstall in
# the Docker path, or a preinstalled Qt prefix on the host runner) — it is not
# available as a Fedora package at the required version.

# This package ships a self-contained /opt/trailer bundle: Qt 6.11 (qtpdf),
# onnxruntime and ICU are staged under /opt/trailer/lib by %install (see the
# shared scripts/bundle-qt-runtime.sh) with the binary's RPATH pointing there.
# Those libraries have no matching Fedora packages, and the binary is built on
# Ubuntu, so its auto-detected sonames (e.g. libqpdf.so.29) would not line up
# with Fedora's. Disable rpm's automatic dependency generation entirely and
# declare only the genuinely-external system libraries by Fedora package name,
# mirroring the .deb's Depends.
AutoReqProv:    no

Requires:       glibc
Requires:       libstdc++
Requires:       libgcc
Requires:       qpdf-libs
Requires:       mesa-libGL
Requires:       glib2
Requires:       fontconfig
Requires:       dbus-libs
Requires:       libX11
Requires:       libX11-xcb
Requires:       libXext
Requires:       libXrender
Requires:       libXi
Requires:       libSM
Requires:       libICE
Requires:       libxkbcommon
Requires:       libxkbcommon-x11
Requires:       libxcb
Requires:       xcb-util
Requires:       xcb-util-image
Requires:       xcb-util-keysyms
Requires:       xcb-util-renderutil
Requires:       xcb-util-wm
Requires:       xcb-util-cursor
Requires:       libwayland-client
Requires:       libwayland-egl

%description
Trailer is a fast, cross-platform viewer for PDF documents and images.
Supports PDF, PNG, JPEG, BMP, GIF, TIFF, WebP, and portable image formats.

# TODO: Expand description with feature highlights once feature set is stable

%prep
# Source is pre-built in the Docker container; nothing to unpack here.
# The cmake build is performed before rpmbuild is invoked (see
# build-linux-rpm-inner.sh), so %build is a no-op and %install picks
# up the already-built artifacts.

%build
# cmake configure + build already completed by build-linux-rpm-inner.sh
# before rpmbuild is called; nothing to do here.
:

%install
DESTDIR=%{buildroot} cmake --install %{_builddir}/build-trailer --prefix /usr
install -Dm644 %{_builddir}/trailer-source/packaging/rpm/trailer.desktop \
    %{buildroot}%{_datadir}/applications/trailer.desktop
install -Dm644 %{_builddir}/trailer-source/packaging/rpm/org.trailer.Trailer.metainfo.xml \
    %{buildroot}%{_datadir}/metainfo/org.trailer.Trailer.metainfo.xml
install -Dm644 %{_builddir}/trailer-source/resources/icons/trailer_16.png \
    %{buildroot}%{_datadir}/icons/hicolor/16x16/apps/trailer.png
install -Dm644 %{_builddir}/trailer-source/resources/icons/trailer_32.png \
    %{buildroot}%{_datadir}/icons/hicolor/32x32/apps/trailer.png
install -Dm644 %{_builddir}/trailer-source/resources/icons/trailer_64.png \
    %{buildroot}%{_datadir}/icons/hicolor/64x64/apps/trailer.png
install -Dm644 %{_builddir}/trailer-source/resources/icons/trailer_128.png \
    %{buildroot}%{_datadir}/icons/hicolor/128x128/apps/trailer.png
install -Dm644 %{_builddir}/trailer-source/resources/icons/trailer_256.png \
    %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/trailer.png
install -Dm644 %{_builddir}/trailer-source/resources/icons/trailer_512.png \
    %{buildroot}%{_datadir}/icons/hicolor/512x512/apps/trailer.png
# README goes alongside the license texts that `cmake --install` already
# staged under %{_datadir}/doc/%{name}/ (LICENSE, THIRD_PARTY_LICENSES.md,
# licenses/third-party/…) via the GNUInstallDirs rules in CMakeLists.txt.
install -Dm644 %{_builddir}/trailer-source/README.md \
    %{buildroot}%{_datadir}/doc/%{name}/README.md

# Bundle Qt 6 (qtpdf), onnxruntime and ICU into a self-contained /opt/trailer
# tree: the non-system shared libs the binary needs, the xcb platform plugin, an
# RPATH of /opt/trailer/lib, and a /usr/bin/trailer launcher wrapper. This is the
# exact logic the .deb packager uses (shared script), so both formats stay in
# lockstep. qt_prefix / lib_search_path are passed in via rpmbuild --define from
# scripts/build-linux-rpm-inner.sh.
bash %{_builddir}/trailer-source/scripts/bundle-qt-runtime.sh \
    "%{buildroot}" "%{qt_prefix}" "%{lib_search_path}"

%post
update-desktop-database -q %{_datadir}/applications 2>/dev/null || :
update-mime-database %{_datadir}/mime 2>/dev/null || :
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :

%preun
if [ $1 -eq 0 ]; then
    /bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
fi

%postun
update-desktop-database -q %{_datadir}/applications 2>/dev/null || :
if [ $1 -eq 0 ]; then
    gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi

%files
# License / attribution texts staged into the doc dir by `cmake --install`
# (share/doc/trailer): the first-party MIT license plus the verbatim upstream
# license texts for ONNX Runtime, qpdf, PaddleOCR, Qt, libjpeg-turbo, etc.
%license %{_datadir}/doc/%{name}/LICENSE
%doc %{_datadir}/doc/%{name}/THIRD_PARTY_LICENSES.md
%doc %{_datadir}/doc/%{name}/README.md
%{_datadir}/doc/%{name}/licenses
# Launcher wrapper (sets QT_PLUGIN_PATH, execs /opt/trailer/bin/trailer)
%{_bindir}/trailer
# Self-contained Qt/onnxruntime/ICU bundle: real binary, private libs, and the
# xcb platform plugin (staged by scripts/bundle-qt-runtime.sh in %install).
/opt/trailer
%{_datadir}/applications/trailer.desktop
%{_datadir}/metainfo/org.trailer.Trailer.metainfo.xml
%{_datadir}/icons/hicolor/16x16/apps/trailer.png
%{_datadir}/icons/hicolor/32x32/apps/trailer.png
%{_datadir}/icons/hicolor/64x64/apps/trailer.png
%{_datadir}/icons/hicolor/128x128/apps/trailer.png
%{_datadir}/icons/hicolor/256x256/apps/trailer.png
%{_datadir}/icons/hicolor/512x512/apps/trailer.png

%changelog
* Sun Jul 12 2026 Trailer Contributors <TODO@trailer.example.com> - 0.3.0-1
- Release 0.3.0
* Mon Apr 27 2026 Trailer Contributors <TODO@trailer.example.com> - 0.1.0-1
- Initial package
