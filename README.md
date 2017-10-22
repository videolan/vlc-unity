UnityPluginWithWSL
==================

![screenshot](https://i.imgur.com/JbbioDQl.png)

This is an example that shows how to build a Unity native plugin with using a
WSL (Windows Subsystem for Linux) development environment.

How to build the plugin
-----------------------

- Enable WSL and install a distro (any distro is fine; Ubuntu is recommended).
- Install `make`.
  - `sudo apt install make`
- Install `mingw-w64` (development toolchain targeting 64-bit Windows).
  - `sudo apt install mingw-w64`
- Execute `make` in the `Plugin` directory.

WSL? MinGW? Isn't it better to use Visual Studio?
-------------------------------------------------

I just prefer UNIX-style CLI development environment.

If so, why don't you use cygwin/msys?
-------------------------------------

I used to do. I switched to use WSL because it's matured enough for practical
use.
