# AUR Package

This directory contains the Arch User Repository metadata for
`pipewire-visualizer-git`.

To publish manually:

```sh
scripts/push-aur.sh
```

The script copies `aur/PKGBUILD` and `aur/.SRCINFO` into the AUR Git repository
at `ssh://aur@aur.archlinux.org/pipewire-visualizer-git.git`, commits changed
metadata, and pushes to the AUR `master` branch.

For GitHub Actions publishing, add a repository secret named
`AUR_SSH_PRIVATE_KEY` containing the private SSH key for the AUR account that is
allowed to maintain `pipewire-visualizer-git`.
