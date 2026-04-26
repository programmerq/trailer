Name:           trailer
Version:        0.1.0
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
# Qt 6.8.0 with qtpdf is installed via aqtinstall during build
# (not available as a Fedora package at the required version)

Requires:       qpdf-libs
Requires:       mesa-libGL
# Qt 6 libs are bundled in /opt/trailer/

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
install -Dm644 %{_builddir}/trailer-source/packaging/rpm/trailer-256.png \
    %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/trailer.png
# TODO: Bundle Qt 6 libs (ldd walk) into %{buildroot}%{_prefix}/lib/trailer/ and list in %files

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
%license LICENSE
%doc README.md
%{_bindir}/trailer
%{_datadir}/applications/trailer.desktop
%{_datadir}/metainfo/org.trailer.Trailer.metainfo.xml
%{_datadir}/icons/hicolor/256x256/apps/trailer.png
# %{_prefix}/lib/trailer/  # TODO: list bundled Qt libs here

%changelog
* Sun Apr 27 2026 Trailer Contributors <TODO@trailer.example.com> - 0.1.0-1
- Initial package
