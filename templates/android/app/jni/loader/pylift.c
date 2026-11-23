/* SPDX-License-Identifier: MIT
 * pylift.c - the lift ON THE DEVICE in the APK's embedded CPython (2026-11-11).
 *
 * The desktop launcher runs the lift as separate `python -I -B -m tools.<step> ...` processes (option-c
 * src/launcher/launcher.c lift_job, steps from tools/lift.txt). An Android app can only exec files from
 * nativeLibraryDir, and the official CPython Android package is an embeddable libpython3.15.so anyway - so here
 * every step runs in ONE embedded interpreter: sys.argv set, runpy.run_module(<module>, run_name="__main__"),
 * SystemExit -> the step's exit code. Isolated config, no bytecode files, PYTHONHOME = <files>/python (the
 * stdlib + site-packages the activity extracted from the APK's assets: the lifter, capstone + libcapstone.so). */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <android/log.h>
#include <stdio.h>
#include <string.h>

#include "pylift.h"
#include "port_android.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, PORT_TAG, __VA_ARGS__)

static int s_py_on;

static int py_start(const char *home)
{
    PyConfig cfg;
    PyStatus st;
    wchar_t *whome;
    if (s_py_on) return 1;
    PyConfig_InitIsolatedConfig(&cfg);
    cfg.write_bytecode = 0;
    cfg.buffered_stdio = 0;
    cfg.install_signal_handlers = 0;            /* the toolkit / SDL own the signals */
    whome = Py_DecodeLocale(home, NULL);
    st = PyConfig_SetString(&cfg, &cfg.home, whome);
    PyMem_RawFree(whome);
    if (!PyStatus_Exception(st)) st = Py_InitializeFromConfig(&cfg);
    PyConfig_Clear(&cfg);
    if (PyStatus_Exception(st)) {
        LOGI("[PYLIFT] Python init failed: %s", st.err_msg ? st.err_msg : "?");
        return 0;
    }
    s_py_on = 1;
    LOGI("[PYLIFT] Python %s, home %s", Py_GetVersion(), home);
    return 1;
}

/* One step: module + its arguments as on the desktop command line after "-m". Returns the exit code. */
int pylift_run_module(const char *home, const char *module, int argc, const char *const *argv)
{
    PyObject *list, *runpy = NULL, *fn = NULL, *res = NULL;
    int i, rc = 0;
    if (!py_start(home)) return 120;
    list = PyList_New(0);
    PyList_Append(list, PyUnicode_FromString(module));
    for (i = 0; i < argc; i++) {
        PyObject *s = PyUnicode_DecodeFSDefault(argv[i]);
        PyList_Append(list, s);
        Py_DECREF(s);
    }
    PySys_SetObject("argv", list);
    Py_DECREF(list);
    runpy = PyImport_ImportModule("runpy");
    fn = runpy ? PyObject_GetAttrString(runpy, "run_module") : NULL;
    if (fn) {
        PyObject *args = Py_BuildValue("(s)", module), *kw = Py_BuildValue("{s:s,s:O}", "run_name", "__main__",
                                                                          "alter_sys", Py_True);
        res = PyObject_Call(fn, args, kw);
        Py_XDECREF(args);
        Py_XDECREF(kw);
    }
    if (!res) {
        if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
            PyObject *exc = PyErr_GetRaisedException(), *code = exc ? PyObject_GetAttrString(exc, "code") : NULL;
            if (!code || code == Py_None) rc = 0;
            else if (PyLong_Check(code)) rc = (int)PyLong_AsLong(code);
            else rc = 1;
            Py_XDECREF(code);
            Py_XDECREF(exc);
        } else {
            PyErr_Print();
            rc = 1;
        }
    }
    Py_XDECREF(res);
    Py_XDECREF(fn);
    Py_XDECREF(runpy);
    return rc;
}

void pylift_finish(void)
{
    if (s_py_on) Py_FinalizeEx();
    s_py_on = 0;
}
