# AUR Package

This directory contains the Arch User Repository metadata for
`pipewire-visualizer`.

The package is release-based. `PKGBUILD` downloads the source tarball from a
GitHub release asset instead of cloning the Git repository.

To publish manually:

```sh
scripts/update-aur-release.sh 202605291426.gb2cded9 v202605291426.gb2cded9 <sha256>
scripts/push-aur.sh
```

The update script stamps `aur/PKGBUILD` and `aur/.SRCINFO` with the GitHub
release version, release tag, and release tarball checksum. The push script
copies that metadata into the AUR Git repository at
`ssh://aur@aur.archlinux.org/pipewire-visualizer.git`, commits changed metadata,
and pushes to the AUR `master` branch.

For GitHub Actions publishing, add a repository secret named
`AUR_SSH_PRIVATE_KEY` containing the private SSH key for the AUR account that is
allowed to maintain `pipewire-visualizer`.
