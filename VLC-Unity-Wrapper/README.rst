COMPILING
=========

clone vlc 3.X (+ unity patches) in "vlc" folder
clone vlc-android in "vlc-android" folder

for windows build:

::

  ./build-win.sh -a <arch>

arch might be:

 * x86_64, win64, w64
 * i686, win32, w32 (see known issues)

for android build

::

   ./build-android.sh -a <arch>

arch might be armeabi-v7a (default) or other android compatible architecture

KNOWN ISSUES
============

x86 build for windows doesn't work at the moment, it seems that MINGW64
generated code isn't compatible with unity (callback to C++ function pointer
returns wrong valeurs). You may try to compile it with MSVC.
