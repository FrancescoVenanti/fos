"""Host toolchain setup for the FOs desktop simulator on native Windows.

Puts MSYS2 UCRT64 gcc first on PATH for the native build, points the compiler
at SDL2, and copies the runtime DLLs next to program.exe so it starts from
Explorer or PowerShell without MSYS2 on PATH.

MSYS2 is looked up in FOS_MSYS2_ROOT, then C:/msys64.
"""

Import("env")
import os
import shutil
import sys

if sys.platform == "win32":
    msys_root = os.environ.get("FOS_MSYS2_ROOT", "C:/msys64")
    ucrt = os.path.join(msys_root, "ucrt64")
    ucrt_bin = os.path.join(ucrt, "bin")
    if not os.path.isfile(os.path.join(ucrt_bin, "gcc.exe")):
        sys.stderr.write("FOs simulator: gcc not found in %s (set FOS_MSYS2_ROOT)\n" % ucrt_bin)
        env.Exit(1)

    env.PrependENVPath("PATH", ucrt_bin)
    env.Replace(CC="gcc", CXX="g++", AR="gcc-ar", RANLIB="gcc-ranlib", LINK="g++")
    env.Append(
        CPPPATH=[os.path.join(ucrt, "include", "SDL2")],
        CPPDEFINES=["SDL_MAIN_HANDLED", ("_WIN32_WINNT", "0x0A00"), "WIN32_LEAN_AND_MEAN", "NOMINMAX", ("_FILE_OFFSET_BITS", 64), "_CRT_RAND_S"],
        LIBPATH=[os.path.join(ucrt, "lib")],
        LIBS=["SDL2", "ws2_32", "bcrypt", "shlwapi"],
        LINKFLAGS=["-mconsole"],
    )
    sim_src = os.path.join(env.subst("$PROJECT_DIR"), "sim", "crosspoint-simulator", "src")
    env.Append(CCFLAGS=["-include", os.path.join(sim_src, "HostCompatWin.h")])
    # GCC 15+ defaults C to C23, where bool is a keyword; QRCode's C sources
    # still typedef it.
    env.Append(CFLAGS=["-std=gnu17"])

    # AnimatedGIF calls millis()/delay() on hosts it does not recognise
    # (neither Arduino, macOS nor Linux) without including a declaration.
    def gif_with_arduino(env_, node):
        return env_.Object(node, CCFLAGS=env_["CCFLAGS"] + ["-include", os.path.join(sim_src, "Arduino.h")])

    env.AddBuildMiddleware(gif_with_arduino, "*AnimatedGIF.cpp")

    RUNTIME_DLLS = ["SDL2.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll"]

    def copy_runtime_dlls(target, source, env):
        out_dir = os.path.dirname(target[0].get_abspath())
        for dll in RUNTIME_DLLS:
            src = os.path.join(ucrt_bin, dll)
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(out_dir, dll))

    env.AddPostAction("$PROGPATH", copy_runtime_dlls)
else:
    # macOS/Linux: let sdl2-config describe the host SDL2 install.
    env.ParseConfig("sdl2-config --cflags --libs")
