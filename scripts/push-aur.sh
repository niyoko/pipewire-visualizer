#!/usr/bin/env bash
set -euo pipefail

pkgname="${AUR_PACKAGE:-pipewire-visualizer-git}"
remote="${AUR_REMOTE:-ssh://aur@aur.archlinux.org/${pkgname}.git}"
commit_message="${AUR_COMMIT_MESSAGE:-Update ${pkgname}}"
repo_root="$(git rev-parse --show-toplevel)"
aur_source="${repo_root}/aur"
work_parent="${AUR_WORKDIR:-$(mktemp -d)}"
checkout="${work_parent}/${pkgname}"
cleanup=0

if [[ -z "${AUR_WORKDIR:-}" ]]; then
  cleanup=1
fi

cleanup_workdir() {
  if [[ "${cleanup}" -eq 1 ]]; then
    rm -rf "${work_parent}"
  fi
}
trap cleanup_workdir EXIT

if [[ ! -f "${aur_source}/PKGBUILD" || ! -f "${aur_source}/.SRCINFO" ]]; then
  echo "Missing aur/PKGBUILD or aur/.SRCINFO" >&2
  exit 1
fi

if ! git clone "${remote}" "${checkout}"; then
  mkdir -p "${checkout}"
  git -C "${checkout}" init
  git -C "${checkout}" remote add origin "${remote}"
fi

if git -C "${checkout}" rev-parse --verify refs/heads/master >/dev/null 2>&1; then
  git -C "${checkout}" checkout master
elif git -C "${checkout}" rev-parse --verify HEAD >/dev/null 2>&1; then
  git -C "${checkout}" checkout -B master
else
  git -C "${checkout}" checkout --orphan master
fi

find "${checkout}" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
install -Dm644 "${aur_source}/PKGBUILD" "${checkout}/PKGBUILD"
install -Dm644 "${aur_source}/.SRCINFO" "${checkout}/.SRCINFO"

git -C "${checkout}" add PKGBUILD .SRCINFO

if git -C "${checkout}" diff --cached --quiet; then
  echo "AUR metadata already up to date"
  exit 0
fi

git -C "${checkout}" config user.name "${GIT_AUTHOR_NAME:-github-actions[bot]}"
git -C "${checkout}" config user.email \
  "${GIT_AUTHOR_EMAIL:-github-actions[bot]@users.noreply.github.com}"
git -C "${checkout}" commit -m "${commit_message}"
git -C "${checkout}" push origin HEAD:master
