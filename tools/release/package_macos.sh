#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 4 ]]; then
  echo 'Usage: bash package_macos.sh BUILD_DIR QT_QMAKE arm64|x86_64 OUTPUT_DIR' >&2; exit 2
fi
[[ "$(uname -s)" == Darwin ]] || { echo 'Run this script on macOS or a GitHub macOS runner' >&2; exit 1; }
script_dir="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$script_dir/../.." && pwd)"
build="$(cd "$1" && pwd)"
qmake="$(command -v "$2")"
arch="$3"
[[ "$arch" == arm64 || "$arch" == x86_64 ]] || { echo 'Invalid architecture' >&2; exit 1; }
[[ "$(uname -m)" == "$arch" ]] || { echo 'Use a native runner and matching native dependencies' >&2; exit 1; }
mkdir -p "$4"
output="$(cd "$4" && pwd)"
version="$(tr -d '\r\n' < "$root/VERSION")"
name="Aster-$version-macos-$arch"
stage="$output/$name"
[[ ! -e "$stage" && ! -e "$output/$name.zip" ]] || { echo 'Output already exists' >&2; exit 1; }
[[ "$(QT_QPA_PLATFORM=offscreen "$build/bin/Aster.app/Contents/MacOS/Aster" --version)" == "Aster_Workspace $version" ]] || { echo 'Executable version does not match VERSION' >&2; exit 1; }
qt_prefix="$("$qmake" -query QT_INSTALL_PREFIX)"
plugins="$("$qmake" -query QT_INSTALL_PLUGINS)"
qt_bin="$("$qmake" -query QT_INSTALL_BINS)"
gsl_prefix="${GSL_PREFIX:-$(brew --prefix gsl)}"
blas_prefix="${OPENBLAS_ROOT:-$(brew --prefix openblas)}"
mkdir -p "$stage"
ditto "$build/bin/Aster.app" "$stage/Aster.app"
app="$stage/Aster.app"
plugin_dest="$app/Contents/PlugIns"
mkdir -p "$plugin_dest/platforms" "$plugin_dest/sqldrivers"
for group in imageformats iconengines printsupport styles tls; do
  for plugin in "$plugins/$group"/*.dylib; do
    [[ -f "$plugin" ]] || continue
    mkdir -p "$plugin_dest/$group"
    cp -p "$plugin" "$plugin_dest/$group/"
  done
done
for plugin in platforms/libqcocoa.dylib platforms/libqoffscreen.dylib sqldrivers/libqsqlite.dylib; do
  cp "$plugins/$plugin" "$plugin_dest/$plugin"
done
[[ -f "$plugin_dest/tls/libqsecuretransportbackend.dylib" ]] || { echo 'Qt SecureTransport TLS plugin required' >&2; exit 1; }
# SecureTransport uses macOS system TLS; do not ship the optional OpenSSL backend.
rm -f "$plugin_dest/tls/libqopensslbackend.dylib"
deploy_args=(-no-plugins -no-strip -always-overwrite "-libpath=$gsl_prefix/lib" "-libpath=$blas_prefix/lib")
# Include dependencies used only by the staged plugins (for example QtSvg).
while IFS= read -r -d '' plugin; do
  deploy_args+=("-executable=$plugin")
done < <(find "$plugin_dest" -type f -name '*.dylib' -print0)
"$qt_bin/macdeployqt" "$app" "${deploy_args[@]}"
mkdir -p "$app/Contents/Resources"
printf '[Paths]\nPlugins=PlugIns\n' > "$app/Contents/Resources/qt.conf"
library_args=(--library-dir "$gsl_prefix/lib" --library-dir "$blas_prefix/lib" --library-dir "$qt_prefix/lib")
notice_args=(--dependency-prefix "$gsl_prefix" --dependency-prefix "$blas_prefix")
if command -v brew >/dev/null 2>&1; then
  # The staged libgfortran may use @rpath to load its GCC siblings. Use the
  # installed dependencies of either numerical library to locate their kegs.
  formulas="$(brew deps --union --installed --formula gsl openblas)"
  while IFS= read -r formula; do
    [[ -n "$formula" ]] || continue
    prefix="$(brew --prefix "$formula")"
    notice_args+=(--dependency-prefix "$prefix")
    for directory in "$prefix/lib/gcc/current" "$prefix"/lib/gcc/[0-9]* "$prefix/lib"; do
      if [[ -d "$directory" ]]; then
        library_args+=(--library-dir "$directory")
      fi
    done
  done <<< "$formulas"
fi
python3 "$script_dir/macos_dependencies.py" "$app" --arch "$arch" "${library_args[@]}"
python3 "$script_dir/stage_notices.py" "$stage" --qt-prefix "$qt_prefix" "${notice_args[@]}"
material_args=(--platform "macos-$arch" --package-dir "$stage" --qmake "$qmake"
  --output "$output/dependency-materials/macos-$arch")
[[ -z "${ASTER_DEPENDENCY_CACHE:-}" ]] || material_args+=(--cache "$ASTER_DEPENDENCY_CACHE")
[[ -z "${ASTER_DEPENDENCY_SUPPLEMENT:-}" ]] || material_args+=(--supplement "$ASTER_DEPENDENCY_SUPPLEMENT")
python3 "$script_dir/collect_dependency_materials.py" "${material_args[@]}"
for document in LICENSE COPYRIGHT.md SOURCE_CODE.md SOURCE_DEPENDENCIES.md AUTHORS.md; do
  cp "$stage/$document" "$app/Contents/Resources/$document"
done
cp -R "$stage/licenses" "$app/Contents/Resources/licenses"
# Ad-hoc signing permits local acceptance testing. Public trusted distribution
# uses a Developer ID identity and optional preconfigured notarization profile.
identity="${ASTER_SIGN_IDENTITY:--}"
sign_args=(--force --sign "$identity")
if [[ "$identity" != '-' ]]; then sign_args+=(--options runtime --timestamp); fi
# Signing Contents/MacOS/Aster can sign the enclosing application. Leave its
# executable and each framework's main binary to the respective bundle pass.
frameworks="$app/Contents/Frameworks"
sign_item() {
  echo "Signing ${1#"$stage"/}"
  codesign "${sign_args[@]}" "$1"
  codesign --verify --strict "$1"
}
while IFS= read -r -d '' dylib; do
  sign_item "$dylib"
done < <(find "$frameworks" "$plugin_dest" -type f -name '*.dylib' -print0)
while IFS= read -r -d '' framework; do
  sign_item "$framework"
done < <(find "$frameworks" -depth -type d -name '*.framework' -print0)
echo "Signing Aster.app"
codesign "${sign_args[@]}" "$app"
codesign --verify --deep --strict --verbose=2 "$app"
QT_QPA_PLATFORM=offscreen "$app/Contents/MacOS/Aster" --version
QT_QPA_PLATFORM=offscreen "$app/Contents/MacOS/Aster" --numeric-backend
if [[ -n "${ASTER_NOTARY_PROFILE:-}" ]]; then
  [[ "$identity" != '-' ]] || { echo 'Notarization requires a Developer ID identity' >&2; exit 1; }
  ditto -c -k --keepParent "$app" "$stage/notarize.zip"
  xcrun notarytool submit "$stage/notarize.zip" --keychain-profile "$ASTER_NOTARY_PROFILE" --wait
  xcrun stapler staple "$app"
  rm "$stage/notarize.zip"
fi
cat > "$stage/START_HERE.txt" <<EOF
Aster $version — macOS $arch
Move Aster.app to Applications, then open it. No separate Qt installation is needed.
The provided CI build targets macOS 15 or newer. Choose the package for your Mac.
The default build is ad-hoc signed, without Developer ID signing or notarization.
Check the release notes for this package's signing status.
Copyright (C) 2026 ZiJie Chen. License: GPL-3.0-only; see LICENSE.
Source and redistribution information: SOURCE_CODE.md.
Getting started: QUICK_START.md. Gatekeeper and startup help: FAQ.md.
Updates: Settings > Check for updates opens the public release page.
Close Aster before replacing the app. Keep your database and settings separately.
EOF
ditto -c -k --sequesterRsrc --keepParent "$stage" "$output/$name.zip"
echo "Created $output/$name.zip"
