# kernel_utility_drive

The utility drive (Z:, `\Device\Harddisk0\Partition3-5`) as the console has it (2026-11-01).

`\Device\Harddisk0\Partition5` is the device. On disk that is `data\PartitionN.img`, which XAPI formats.
`\Device\Harddisk0\Partition5\` is the volume's root folder, `data\cache\<title id>`. Panzer Dragoon Orta
probes its folders with `FindFirstFile("z:\<name>")` and creates them only when the probe fails. Because the root
was read as the device, the probe failed in the wrong place and no folder was ever created.

A format ends with `FSCTL_DISMOUNT_VOLUME` and leaves an empty volume, so the title's folder is emptied then. A
valid drive whose image is kept in `data\` mounts without a format and keeps its files.

Checked, in a fresh portable layout:
- Device vs root folder: `Partition5` is the device image, `Partition5\` the root folder; `Partition0\` stays the device.
- Root opens: with and without `FILE_DIRECTORY_FILE`.
- `CreateDirectory("Z:\Sprite")` creates the folder.
- A format empties only that folder.
- Nothing is written outside the layout.

```
cmake -S tests/kernel_utility_drive -B build/kernel_utility_drive -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/kernel_utility_drive && ctest --test-dir build/kernel_utility_drive --output-on-failure
```
