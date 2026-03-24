# tiv — minimal GUI image viewer (Linux)

`tiv` is a small, Linux-focused, GUI image viewer. It opens an image in a window and lets you move **next/previous** with buttons or the keyboard while showing the **current file name**.

## Features

- GTK-based GUI viewer (uses system image loaders)
- `←/→` or `p/n` for previous/next
- Shows current image name and controls
- If opened with a file path, it loads all images in that folder and starts at that file

## Build

```sh
make
```

## Run

```sh
./tiv path/to/image.png
```

Controls:
- Next: `→` or `n`
- Previous: `←` or `p`
- Zoom in/out: `+` / `-` (reset `0`)
- Crop: `c` then drag a rectangle, press `Enter` (saves `*_crop.png`)
- Quit: `q` or `Esc`

## Install (system-wide)

```sh
sudo make install
```

This installs:
- Binary: `/usr/local/bin/tiv`
- Desktop entry: `/usr/local/share/applications/tiv.desktop`

## File manager integration

Set `tiv` as the default application for image types in your desktop environment, or run:

```sh
xdg-mime default tiv.desktop image/png
xdg-mime default tiv.desktop image/jpeg
xdg-mime default tiv.desktop image/webp
```

Desktop entry: `packaging/tiv.desktop`.

## Arch package (local)

```sh
make dist
cp dist/tiv-$(cat VERSION).tar.gz packaging/arch/tiv-$(cat VERSION).tar.gz
cd packaging/arch
makepkg -si
```

## AppImage (local)

This AppImage is minimal and **does not bundle GTK4** (your system still needs `gtk4`).

```sh
./packaging/appimage/build-appimage.sh
```

Output:
- `dist/tiv-<version>-<arch>.AppImage`
