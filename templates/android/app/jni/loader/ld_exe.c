/* SPDX-License-Identifier: MIT
 * ld_exe.c - the linker clang calls on the device (2026-11-11).
 *
 * The launcher links with the clang driver (option-c src/launcher/launcher.c link_job), build.txt names this file
 * with --ld-path. lld picks its flavour from argv[0] or a leading "-flavor"; clang starts the linker under the
 * path it was given (libld_exe.so), which lld does not know. This exec()s the toolkit's lld (liblld_exe.so, the same
 * nativeLibraryDir: the one place an app may exec from) with argv[0] = "ld.lld" - the GNU flavour - and the
 * arguments unchanged. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char self[PATH_MAX], lld[PATH_MAX];
    ssize_t k = readlink("/proc/self/exe", self, sizeof self - 1);
    char *slash;
    (void)argc;
    if (k <= 0) {
        fprintf(stderr, "ld_exe: /proc/self/exe: %s\n", strerror(errno));
        return 127;
    }
    self[k] = 0;
    slash = strrchr(self, '/');
    if (!slash)
        return 127;
    *slash = 0;
    snprintf(lld, sizeof lld, "%s/liblld_exe.so", self);
    argv[0] = "ld.lld";
    execv(lld, argv);
    fprintf(stderr, "ld_exe: exec %s: %s\n", lld, strerror(errno));
    return 127;
}
