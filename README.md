UnityPluginWithWSL
==================

![screenshot](https://i.imgur.com/JbbioDQl.png)

This is an example that shows how to build a Unity native plugin with using a
WSL (Windows Subsystem for Linux) development environment.

How to build the plugin
-----------------------

- Enable WSL and install a distro that you like.
- Install a development toolchain. At least you have to install GNU make.
  - `audo apt install make`
- Install mingw-w64.
  - `audo apt install mingw-w64`
- Execute `make` in the `Plugin` directory.

Why don't you use Visual Studio?
--------------------------------

I just prefer UNIX command line toolchain.

If so, why don't you use cygwin/msys?
-------------------------------------

I used to do. Now WSL gets better than cygsin/msys, so switched to use WSL.
