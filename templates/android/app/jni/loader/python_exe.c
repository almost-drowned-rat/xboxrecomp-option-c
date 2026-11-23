/* SPDX-License-Identifier: MIT
 * python_exe.c - `python` for the toolkit launcher's lift steps ON THE DEVICE (2026-11-11).
 *
 * The launcher (option-c src/launcher/launcher.c lift_job) runs every lift step as a process:
 * `<python> -I -B -m tools.<step> ...` (steps from tools/lift.txt). python.org's Android package is only an
 * embeddable libpython3.15.so, and an app may exec files only from nativeLibraryDir - so this small executable
 * ships there as libpython_exe.so and runs that command line in the embedded interpreter. Isolated config
 * (-I ignores PYTHON* variables, so the home comes from XR_PYTHON_HOME, set by loader.c: <files>/python = the
 * stdlib + site-packages the activity extracted from the APK's assets: the lifter, capstone + libcapstone.so). */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    PyConfig cfg;
    PyStatus st;
    const char *home = getenv("XR_PYTHON_HOME");
    if (!home || !*home) {
        fprintf(stderr, "python_exe: XR_PYTHON_HOME not set\n");
        return 120;
    }
    PyConfig_InitIsolatedConfig(&cfg);
    cfg.parse_argv = 1;                 /* -I -B -m tools.x ... as on the desktop */
    cfg.write_bytecode = 0;
    cfg.buffered_stdio = 0;
    st = PyConfig_SetBytesString(&cfg, &cfg.home, home);
    if (!PyStatus_Exception(st))
        st = PyConfig_SetBytesArgv(&cfg, argc, argv);
    if (!PyStatus_Exception(st))
        st = Py_InitializeFromConfig(&cfg);
    PyConfig_Clear(&cfg);
    if (PyStatus_Exception(st)) {
        if (PyStatus_IsExit(st))
            return st.exitcode;
        fprintf(stderr, "python_exe: init failed: %s\n", st.err_msg ? st.err_msg : "?");
        return 120;
    }
    return Py_RunMain();
}
