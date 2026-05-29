#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <pkgver> <release-tag> <sha256>" >&2
  exit 1
fi

pkgver="${1#v}"
tag="$2"
sha256="$3"
repo_root="$(git rev-parse --show-toplevel)"
pkgbuild="${repo_root}/aur/PKGBUILD"
srcinfo="${repo_root}/aur/.SRCINFO"

if [[ ! "${pkgver}" =~ ^[0-9]{12}\.g[0-9a-fA-F]{7,}$ ]]; then
  echo "Invalid pkgver '${pkgver}', expected yyyyMMddhhmm.gHASH" >&2
  exit 1
fi

if [[ ! "${sha256}" =~ ^[0-9a-fA-F]{64}$ ]]; then
  echo "Invalid sha256 '${sha256}'" >&2
  exit 1
fi

sed -i \
  -e "s/^pkgver=.*/pkgver=${pkgver}/" \
  -e "s/^_tag=.*/_tag='${tag}'/" \
  -e "s/^sha256sums=.*/sha256sums=('${sha256}')/" \
  "${pkgbuild}"

(
  cd "${repo_root}/aur"
  makepkg --printsrcinfo > "${srcinfo}"
)
