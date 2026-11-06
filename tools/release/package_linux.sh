#!/usr/bin/env bash
# Portable Linux x86_64 directory; build on the oldest supported distribution.
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "Usage: bash package_linux.sh BUILD_DIR QT_QMAKE OUTPUT_DIR" >&2; exit 2
fi
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { echo 'Linux x86_64 host required' >&2; exit 1; }
script_dir="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$script_dir/../.." && pwd)"
build="$(cd "$1" && pwd)"
qmake="$(command -v "$2")"
mkdir -p "$3"
output="$(cd "$3" && pwd)"
version="$(tr -d '\r\n' < "$root/VERSION")"
name="Aster-$version-linux-x64"
stage="$output/$name"
[[ ! -e "$stage" && ! -e "$output/$name.tar.gz" ]] || { echo 'Output already exists' >&2; exit 1; }
file "$build/bin/Aster" | grep -q 'ELF 64-bit.*x86-64' || { echo 'Expected Linux x86_64 executable' >&2; exit 1; }
[[ "$(QT_QPA_PLATFORM=offscreen "$build/bin/Aster" --version)" == "Aster_Workspace $version" ]] || { echo 'Executable version does not match VERSION' >&2; exit 1; }
plugins="${ASTER_QT_PLUGINS:-$("$qmake" -query QT_INSTALL_PLUGINS)}"
qt_prefix="$("$qmake" -query QT_INSTALL_PREFIX)"
mkdir -p "$stage/usr/bin" "$stage/usr/plugins" "$stage/usr/share/applications" "$stage/usr/share/icons/hicolor/256x256/apps"
cp "$build/bin/Aster" "$stage/usr/bin/"
for group in imageformats iconengines xcbglintegrations printsupport tls; do
  [[ ! -d "$plugins/$group" ]] || cp -a "$plugins/$group" "$stage/usr/plugins/"
done
mkdir -p "$stage/usr/plugins/platforms" "$stage/usr/plugins/sqldrivers"
for plugin in platforms/libqxcb.so platforms/libqoffscreen.so sqldrivers/libqsqlite.so; do
  cp "$plugins/$plugin" "$stage/usr/plugins/$plugin"
done
[[ -f "$stage/usr/plugins/tls/libqopensslbackend.so" ]] || { echo 'Qt OpenSSL TLS plugin required' >&2; exit 1; }
printf '[Paths]\nPrefix=..\nPlugins=plugins\n' > "$stage/usr/bin/qt.conf"
cat > "$stage/usr/share/applications/Aster.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Aster
Comment=Geoscience and astronomical time-series analysis
Exec=Aster
Icon=Aster
Categories=Science;Education;
Terminal=false
EOF
cp "$root/resources/app/aster-256.png" "$stage/usr/share/icons/hicolor/256x256/apps/Aster.png"
# Pin the deployment tool by checksum. Supply LINUXDEPLOY for an offline copy.
tool="${LINUXDEPLOY:-$build/release-tools/linuxdeploy-x86_64.AppImage}"
expected=df908b19f1ed1cdafa79508d415aa12d6c56ce4d56d4149aa8c09286718706e5
if [[ ! -f "$tool" ]]; then
  mkdir -p "$(dirname "$tool")"
  curl --fail --location --retry 3 https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -o "$tool"
fi
echo "$expected  $tool" | sha256sum --check --status || { echo 'linuxdeploy checksum changed. Review and update the pinned tool before packaging.' >&2; exit 1; }
chmod +x "$tool"
# Qt loads OpenSSL at runtime; include it even when it is absent from DT_NEEDED.
ssl="$(ldconfig -p | awk '/libssl.so.3 .*x86-64/ && !found {print $NF; found=1}')"
crypto="$(ldconfig -p | awk '/libcrypto.so.3 .*x86-64/ && !found {print $NF; found=1}')"
[[ -f "$ssl" && -f "$crypto" ]] || { echo 'OpenSSL 3 runtime required' >&2; exit 1; }
# Plugins live in platforms/, sqldrivers/, imageformats/, etc. Give
# linuxdeploy the plugin files: listing only usr/plugins misses its subdirectories.
plugin_args=()
while IFS= read -r -d '' plugin; do
  plugin_args+=(--deploy-deps-only "$plugin")
done < <(find "$stage/usr/plugins" -type f -name '*.so' -print0)
APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --appdir "$stage" --executable "$stage/usr/bin/Aster" \
  --desktop-file "$stage/usr/share/applications/Aster.desktop" \
  --icon-file "$stage/usr/share/icons/hicolor/256x256/apps/Aster.png" \
  --custom-apprun "$script_dir/linux_app_run.sh" "${plugin_args[@]}" --library "$ssl" --library "$crypto"
# Detect a developer Qt masking a missing private library in the bundle.
for plugin in "$stage"/usr/plugins/*/*.so; do
  deps="$(LC_ALL=C LD_LIBRARY_PATH="$stage/usr/lib" ldd "$plugin")"
  if [[ "$deps" == *"not found"* ]]; then
    printf '%s\n%s\n' "Missing dependency for $plugin" "$deps" >&2
    exit 1
  fi
  while IFS= read -r line; do
    if [[ "$line" =~ ^[[:space:]]*libQt6[^[:space:]]+[[:space:]] ]] &&
       [[ "$line" != *" => $stage/usr/lib/"* ]]; then
      echo "Unbundled Qt dependency for $plugin: $line" >&2
      exit 1
    fi
  done <<< "$deps"
done
python3 "$script_dir/stage_notices.py" "$stage" --qt-prefix "$qt_prefix"
material_args=(--platform linux-x64 --package-dir "$stage" --qmake "$qmake"
  --output "$output/dependency-materials/linux-x64")
[[ -z "${ASTER_DEPENDENCY_CACHE:-}" ]] || material_args+=(--cache "$ASTER_DEPENDENCY_CACHE")
[[ -z "${ASTER_DEPENDENCY_SUPPLEMENT:-}" ]] || material_args+=(--supplement "$ASTER_DEPENDENCY_SUPPLEMENT")
python3 "$script_dir/collect_dependency_materials.py" "${material_args[@]}"
build_system="$(. /etc/os-release; printf '%s' "$PRETTY_NAME")"
cat > "$stage/START_HERE.txt" <<EOF
Aster $version — Linux x64
Build system: $build_system
Extract the complete directory, then run ./AppRun. No Qt installation is needed.
Copyright (C) 2026 ZiJie Chen. License: GPL-3.0-only; see LICENSE.
Source and redistribution information: SOURCE_CODE.md.
Getting started: QUICK_START.md. Troubleshooting: FAQ.md.
Chinese text shown as boxes usually requires CJK fonts; see FAQ.md (Linux fonts).
Keep usr/ beside AppRun. This package requires a compatible Linux system and an
X11 display (or XWayland). It does not bundle glibc, graphics drivers or a desktop.
The official build targets Ubuntu 22.04 or newer; other distributions need testing.
Updates: Settings > Check for updates opens the public release page.
Close Aster before using a new package. Keep your database and settings separately.
EOF
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH QT_QPA_PLATFORM=offscreen "$stage/AppRun" --version
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH QT_QPA_PLATFORM=offscreen "$stage/AppRun" --numeric-backend
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH QT_QPA_PLATFORM=xcb \
  xvfb-run -a "$stage/AppRun" --version
tar -C "$output" -czf "$output/$name.tar.gz" "$name"
(cd "$output" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo "Created $output/$name.tar.gz"
