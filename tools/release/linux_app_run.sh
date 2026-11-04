#!/usr/bin/env bash
set -e
app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$app_dir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$app_dir/usr/plugins"
# The bundle contains the X11 platform plugin; Wayland desktops use XWayland.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$app_dir/usr/bin/Aster" "$@"
