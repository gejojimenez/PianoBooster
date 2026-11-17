# Build instructions for Linux and BSD Unix

Ensure that the following packages are installed:

- For Qt5: `build-essential`, `cmake`, `pkg-config`, `libftgl-dev`, `qtbase5-dev`, `qttools5-dev`, `libqt5sql5-sqlite`, `librtmidi-dev` , `libfluidsynth-dev`, `fluid-soundfont-gm`
- For Qt6: `build-essential`, `cmake`, `pkg-config`, `libftgl-dev`, `qt6-base-dev`, `libqt6core5compat6-dev`, `qt6-tools-dev`, `qt6-tools-dev-tools`, `qt6-l10n-tools`, `libqt6sql6-sqlite`, `librtmidi-dev` , `libfluidsynth-dev`, `fluid-soundfont-gm`

(The `sqlite` packages provide the SQLite driver used by the song library; the package
names are those of Debian and Ubuntu.)


To generate a project makefile using CMake, create a build folder
and then run CMake from it:
```
$ mkdir build
$ cd build
$ cmake .. -DQT_PACKAGE_NAME=Qt6 # or cmake .. -DQT_PACKAGE_NAME=Qt5
```

To compile the PianoBooster source code type:

`make -j$(nproc)`

The pianobooster binary executable is now in a new build subdirectory.
PianoBooster is designed to run in place so you can type `build/pianobooster` to start running the program.

(Optional) If you wish to install PianoBooster as root type:

`make install`

There is no make uninstall command but the files that have been installed are listed in a file called `install_manifest.txt` which can then be used to manually delete the installed files.

To build a debug version create a directory called "debug" and change to that dir and then type:

`cmake -DCMAKE_BUILD_TYPE=Debug ..`


# Build instructions for macOS

Install the latest Xcode (from Apple Developer Connection, free registration required).

Install CMake and QT libraries via Homebrew:

`$ brew install cmake qt5 ftgl pkg-config fluid-synth`

To generate the project makefile first create a `build` directory
and then from that directory type:

`cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"`

To compile the PianoBooster source code type:

`make`

To copy the language translations into the build directory type:

`make install-translations`


To make a self contained application bundle use QT's macdeployqt tool (included in QT).

`$(brew --prefix qt)/bin/macdeployqt build/pianobooster.app -dmg`



# Build instructions for Windows

The recommended way to build on Windows is with [MSYS2](https://www.msys2.org), which
provides the compiler, Qt, FluidSynth and FTGL as ready-made 64-bit packages
built with the same toolchain.

Install MSYS2 (default location `C:\msys64`) and open the **MSYS2 UCRT64** shell.
All the commands below must be run from that shell. First update MSYS2, repeating
the command (and reopening the shell if asked) until there is nothing left to update:

```
pacman -Syu
```

Then install the build dependencies:

```
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
    mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools \
    mingw-w64-ucrt-x86_64-qt6-5compat mingw-w64-ucrt-x86_64-ftgl \
    mingw-w64-ucrt-x86_64-fluidsynth
```

From the PianoBooster source directory (MSYS2 writes `C:\` as `/c/`), configure and build:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT_PACKAGE_NAME=Qt6
cmake --build build
```

The executable is created as `build/build/pianobooster.exe`. Run it from the same
shell, which makes the MSYS2 DLLs available:

```
./build/build/pianobooster.exe
```

To use the internal FluidSynth sound generator, download a General MIDI SoundFont
(`.sf2`, e.g. GeneralUser GS or FluidR3_GM) and load it in the MIDI setup dialog.

To create a portable zip that runs without MSYS2 (the executable, its data files,
Qt and every other required DLL), run after building:

```
tools/package-windows.sh
```

The result is `build/package/PianoBooster-<version>-win64.zip`.

QtCreator can also be used: add a kit with the Qt version `C:\msys64\ucrt64\bin\qmake6.exe`,
the compilers and the CMake from `C:\msys64\ucrt64\bin`, open the top-level `CMakeLists.txt`
and add `-DQT_PACKAGE_NAME=Qt6` to the CMake configuration.

Note: if "Smart App Control" is enabled in Windows 11 it may block unsigned compilers
and tools such as those of MSYS2 or MinGW, causing errors like "cannot execute cc1.exe".

# Build options

Using `cmake` without any flags defaults to the recommended build options.
However the following build options below can be changed by using the `-D` flag to `cmake`.

**WITH_INTERNAL_FLUIDSYNTH:** build with an internal FluidSynth sound generator [Default:ON]

**USE_BUNDLED_RTMIDI:**  Build with bundled rtmidi (for older distributions only) [Default: OFF]

**USE_FTGL:** Build with ftgl for notes localization [Default:ON]

**USE_SYSTEM_FONT:** Build with system font [Default: OFF]

**USE_JACK:** Build with Jack. The use of JACK is not required other than for BSD Unix. [Default: OFF]

**DATA_DIR**: Build with specified data directory; [Default:"share/games/pianobooster"]

**NO_LANGS**: Do not install languages; [Default: OFF]

**NO_DOCS**: Do not install documents [Default: OFF]

**NO_LICENSE**: Do not install license [Default: OFF].

**NO_CHANGELOG**: Do not install changelog [Default: OFF].

**WITH_MAN**: Install man page [Default: OFF].
