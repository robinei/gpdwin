# Licence of this folder

- Our DXVK patches (`0001-gpd-d3d9-async-readback-and-wait-log*.patch`) change DXVK, which is **zlib**-licensed
  (`LICENSES/Zlib.txt` at the repo root); the patches are under the same licence. They mark what they change.
  Copyright of DXVK stays with its authors.
- The dxvk-gplasync patches (Ph42oN, https://gitlab.com/Ph42oN/dxvk-gplasync) state no licence, so they are not
  included: `build.sh` downloads them and checks their hashes.
- Built DLLs (`x32/`, `x64/`) are not in the repo.
- Notes and `build.sh` are ours, MIT (`LICENSE`).
