# kernel_file_rename

Tests `NtSetInformationFile` FileRenameInformation (class 10), which goes through `xbox_file_rename` in
`src/kernel/kernel_file.c`. Panzer Dragoon Orta writes each cache file as `Z:\...\x.tmp` and renames it into place.
The class was unhandled, so every cache file stayed `.tmp` (2026-11-01).

Host-level, in a fresh temp folder. Checks:
- **Bare name:** the file is renamed in its own directory, and the handle keeps working afterwards.
- **Existing target:** refused (`OBJECT_NAME_COLLISION`) without ReplaceIfExists, replaced with it.
- **RootDirectory:** a name relative to a directory handle lands in that folder.
- **Bad arguments.**

The bridge reads the Xbox `FILE_RENAME_INFORMATION` out of guest memory first (ANSI `OBJECT_STRING` at +8).

```
cmake -S tests/kernel_file_rename -B build/kernel_file_rename -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/kernel_file_rename && ctest --test-dir build/kernel_file_rename --output-on-failure
```
