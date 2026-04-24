# nComp Presets

This folder contains the user preset library for nComp.

## Included Folders

- `STD`
- `VCA`
- `FET`
- `OPTO`
- `MU`
- `TUBE`

Each folder contains 16 presets for common use cases.

## Where To Install

Copy the preset folders into the `Presets` directory used by nComp on your system.

### macOS

Default location:

```text
~/Music/Audio Music Apps/nComp/Presets
```

Expanded example:

```text
/Users/<your-user-name>/Music/Audio Music Apps/nComp/Presets
```

### Windows

Default location:

```text
C:\Users\<your-user-name>\Music\Audio Music Apps\nComp\Presets
```

### Linux

Default location:

```text
/home/<your-user-name>/Music/Audio Music Apps/nComp/Presets
```

## Install Steps

1. Create the destination `Presets` folder if it does not exist.
2. Copy the preset type folders from this directory into that destination.
3. Reopen the nComp plugin window, or reload the plugin, so the preset list is refreshed.

## Notes

- nComp now scans preset subfolders recursively.
- In the preset menu, entries are shown with their relative folder path.
- Existing presets in the destination folder can live alongside these folders.