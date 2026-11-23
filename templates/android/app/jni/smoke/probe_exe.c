/* SPDX-License-Identifier: MIT
 * probe_exe.c - stand-in for the on-device compiler (2026-11-11): an executable shipped as
 * lib/arm64-v8a/libprobe_exe.so, so Android installs it into nativeLibraryDir where execve is allowed (clang / lld
 * will travel the same way). Prints its arguments and exits with 42. */
#include <stdio.h>

int main(int argc, char **argv)
{
    int i;
    printf("probe_exe running:");
    for (i = 0; i < argc; i++) printf(" [%s]", argv[i]);
    printf("\n");
    return 42;
}
