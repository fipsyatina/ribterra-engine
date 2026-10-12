#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vcpkg_dir="$project_dir/.deps/vcpkg"
baseline=04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4
if [[ ! -d "$vcpkg_dir/.git" ]]; then
  git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "$vcpkg_dir"
fi
git -C "$vcpkg_dir" checkout --detach "$baseline"
if [[ ! -x "$vcpkg_dir/vcpkg" ]]; then
  "$vcpkg_dir/bootstrap-vcpkg.sh" -disableMetrics
fi
