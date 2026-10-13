# Example user layer

Start your own from this: `cp -r users/example users/<you>` (or keep it in a private repo and set
`user_dir=` in `~/.config/gpd/host`), then edit `manifest.tsv` and `packages.txt`. What belongs here:
your game collections (`pegasus/`), emulator settings you've tuned, your Wi-Fi country, a sway snippet
of your own (link it to `~/.config/gpd/sway.d/90-user.conf`). Hardware fixes belong in the device
layer, and things everyone wants in a component, so others get them too.
