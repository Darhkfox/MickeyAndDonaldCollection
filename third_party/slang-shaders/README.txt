Bundled slang shader presets
============================

These RetroArch slang shader presets are included with the Windows version for use with
Options > Video > Slang Shader (run through librashader.dll). They are unmodified copies
of files from the libretro slang-shaders collection:

    https://github.com/libretro/slang-shaders   (master branch, fetched 2026-11-04)

The folder layout is the collection's own, so each preset finds its files. Only the files the
presets below use are included.

Preset                  Authors                                   Licence
----------------------  ----------------------------------------  ---------------------------
crt/crt-lottes          Timothy Lottes                            Public domain
crt/crt-geom            cgwg, Themaister and DOLLS                GPL v2 or later
crt/crt-royale          TroggleMonkey                             GPL v2 or later
                        (blurs/ and include/ helpers: MIT)
crt/crt-easymode        EasyMode                                  GPL
crt/crt-aperture        EasyMode                                  GPL
crt/zfast-crt           Greg Hogan (SoltanGris42)                 GPL v2 or later
crt/crt-pi              davej                                     GPL v2 or later

Each shader file keeps its original copyright and licence notice. The GNU General Public
License version 2 is in LICENSE-GPL-2.0.txt; the MIT notices are in the files themselves.
The shaders are separate files the program loads at run time; they are not part of the
program's own code. The full source of every shader is these files.

Some presets were made for a 4:3 picture and can stretch or frame oddly in widescreen.
