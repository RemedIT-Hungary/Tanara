# Tanara: Fedora RPM (built in Fedora COPR, see .copr/Makefile and docs/BUILD.md "Packaging").
# The Version is kept in step with CMakeLists.txt by .copr/Makefile (it rewrites it from
# project(VERSION) + TANARA_VERSION_SUFFIX when it builds the SRPM).
%global app_id eu.remedit.Tanara

Name:           tanara
Version:        0.5.317
Release:        1%{?dist}
Summary:        Record, transcribe and summarize meetings

License:        MIT
URL:            https://github.com/RemedIT-Hungary/Tanara
Source0:        %{name}-%{version}.tar.gz

ExclusiveArch:  x86_64 aarch64

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtmultimedia-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  qt6-linguist
BuildRequires:  onnxruntime-devel
BuildRequires:  qtkeychain-qt6-devel
# run-time tools that the tests use (ffmpeg/ffprobe, node for the mock gateway)
BuildRequires:  ffmpeg-free
BuildRequires:  nodejs24
BuildRequires:  python3

# The tools Tanara runs as separate programs. File dependencies, so that the RPM Fusion
# "ffmpeg" works as well as Fedora's ffmpeg-free (libopus and libmp3lame are needed).
Requires:       /usr/bin/ffmpeg
Requires:       /usr/bin/ffprobe
# QML imports are loaded at run time (the library dependencies are found automatically)
Requires:       qt6-qtdeclarative%{?_isa}
Requires:       qt6-qtmultimedia%{?_isa}
Requires:       qt6-qtsvg%{?_isa}
# pw-dump: finds the application that uses the microphone (call detection)
Recommends:     pipewire-utils
Recommends:     ffmpeg-free
Recommends:     qt6-qtwayland%{?_isa}

# Vendored sources (unmodified; see THIRD_PARTY_NOTICES.md)
Provides:       bundled(kaldi-native-fbank) = 1.22.3
Provides:       bundled(kissfft) = 131.1.0
Provides:       bundled(miniaudio) = 0.11.25
Provides:       bundled(miniz) = 3.1.2

%description
Tanara records a meeting (your microphone and the sound of the call), transcribes it with
the speech-to-text service you choose, recognizes the speakers on your computer and writes
a summary with a language model. Recordings, transcripts and voice prints stay on your
computer. A tray watcher can notice that a call has started and offer to record it.

The speaker-recognition model (CAM++, Apache-2.0) is not part of this package. Download it
once with "tanara-cli voice-models fetch"; without it Tanara works but skips speaker labels.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DTANARA_BUILD_VOICEID=ON \
    -DTANARA_USE_KEYCHAIN=ON
%cmake_build

%install
install -Dpm0755 %{_vpath_builddir}/gui/tanara %{buildroot}%{_bindir}/tanara
install -Dpm0755 %{_vpath_builddir}/cli/tanara-cli %{buildroot}%{_bindir}/tanara-cli
install -Dpm0755 %{_vpath_builddir}/watcher/tanara-watcher %{buildroot}%{_bindir}/tanara-watcher
install -Dpm0644 packaging/linux/%{app_id}.desktop %{buildroot}%{_datadir}/applications/%{app_id}.desktop
install -Dpm0644 packaging/linux/%{app_id}.metainfo.xml %{buildroot}%{_metainfodir}/%{app_id}.metainfo.xml
install -Dpm0644 packaging/linux/%{app_id}.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/%{app_id}.svg

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{app_id}.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/%{app_id}.metainfo.xml
# Headless tests: no display, no network, a throw-away data folder (never real user data).
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
export TANARA_HOME="$(mktemp -d)"
%ctest
rm -rf "$TANARA_HOME"

%files
%license LICENSE THIRD_PARTY_NOTICES.md
%license packaging/licenses
%doc README.md
%{_bindir}/tanara
%{_bindir}/tanara-cli
%{_bindir}/tanara-watcher
%{_datadir}/applications/%{app_id}.desktop
%{_metainfodir}/%{app_id}.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/%{app_id}.svg

%changelog
* Wed Oct 14 2026 RemedIT Hungary Kft. <info@remedit.eu> - 0.5.253-1
- Phase 2 cleanup: module layout, service layer, provider ports, PersonId, packaging

* Wed Oct 14 2026 RemedIT Hungary Kft. <info@remedit.eu> - 0.5.11-1
- First COPR package.
