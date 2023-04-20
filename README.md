# hyprlens
A small plugin to provide a shared image as the background for transparent windows.

# Installing
Since Hyprland plugins don't have ABI guarantees, you *should* download the Hyprland source and compile it if you plan to use plugins.
This ensures the compiler version is the same between the Hyprland build you're running, and the plugins you are using.

The guide on compiling and installing Hyprland manually is on the [wiki](http://wiki.hyprland.org/Getting-Started/Installation/#manual-manual-build)

## Using [hyprload](https://github.com/Duckonaut/hyprload)
Add the line `"Duckonaut/hyprlens",` to your `hyprload.toml` config, like this
```toml
plugins = [
    "Duckonaut/hyprlens",
]
```
Then update via the `hyprload,update` dispatcher

## Manual installation
1. Export the `HYPRLAND_HEADERS` variable to point to the root directory of the Hyprland repo
    - `export HYPRLAND_HEADERS="$HOME/repos/Hyprland"`
2. Compile
    - `make all`
3. Add this line to the bottom of your hyprland config
    - `exec-once=hyprctl plugin load <ABSOLUTE PATH TO hyprlens.so>`


# Usage
Set the config up to your liking. If the image isn't found, you'll notice all your backgrounds are red.

| Name                              | Type      | Default   | Description                                               |
|-----------------------------------|-----------|-----------|-----------------------------------------------------------|
| `plugin:hyprlens:background`      | string    | none      | The ABSOLUTE path to the image (must be .png)             |
| `plugin:hyprlens:tiled`           | bool      | false     | Whether to keep the pixel size constant and draw tiled    |
| `plugin:hyprlens:nearest`         | bool      | false     | If enabled, no filtering                                  |

As of right now, changing the config value will not update the backdrop. A restart is needed.
