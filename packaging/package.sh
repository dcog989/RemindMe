#!/usr/bin/env bash
#
# Build the KRunner "Get New Plugins" payload. Run from any directory; output lands in dist/.
#
# The archive ships a binary built in the container plus the files a normal install places, in
# the layout packaging/kns/krunner-remindme-launcher.sh copies from, so the target machine needs
# no compiler: the binary links only the Qt6/KF6 libraries every Plasma 6 desktop running KRunner
# already has. Raise IMAGE as its packages age; that also raises the glibc, Qt and KF6 the binary
# requires.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

image="${IMAGE:-fedora:43}"
arches="${ARCHES:-x86_64}"
runtime="${RUNTIME:-$(command -v podman || command -v docker || true)}"

for file in krunner-plugininstallerrc packaging/kns/krunner-remindme-launcher.sh; do
    if ! git ls-files --error-unmatch "$file" >/dev/null 2>&1; then
        echo "$file is not committed; commit it before packaging" >&2
        exit 1
    fi
done
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "tracked files are modified; commit them before packaging" >&2
    exit 1
fi
if [ -z "$runtime" ]; then
    echo "neither podman nor docker is installed; the payload is built in a container" >&2
    exit 1
fi

# Single source of truth for the version: the project() declaration.
version="$(sed -n 's/^project(remindme VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
if [ -z "$version" ]; then
    echo "could not read the version from project() in CMakeLists.txt" >&2
    exit 1
fi

name="remindme-$version"
out="dist/$name.tar.gz"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
payload="$work/$name"
mkdir -p "$payload/bin" "$payload/packaging/kns" dist
cp krunner-plugininstallerrc "$payload/"
cp packaging/kns/krunner-remindme-launcher.sh "$payload/packaging/kns/"

# Configure, build and install into the mounted prefix, so the staging tree is exactly the
# install tree. Only the files the launcher needs are copied out of it; the D-Bus service file
# is left behind because krunner-plugininstaller writes its own, pointing at the launcher.
build() {
    local platform="$1" stage="$2"
    "$runtime" run --rm --platform "linux/$platform" -v "$root":/src:ro -v "$stage":/prefix "$image" \
        bash -euo pipefail -c '
            dnf install -y --setopt=install_weak_deps=False \
                cmake gcc-c++ gettext extra-cmake-modules \
                qt6-qtbase-devel kf6-kcoreaddons-devel kf6-ki18n-devel kf6-knotifications-devel kf6-kconfig-devel
            cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/prefix
            cmake --build /build --parallel
            cmake --install /build
            find /prefix -type f -name krunner-remindme -exec strip {} +
        '
}

first_stage=""
for arch in $arches; do
    case "$arch" in
    x86_64) platform=amd64 ;;
    aarch64) platform=arm64 ;;
    *)
        echo "unsupported architecture: $arch" >&2
        exit 1
        ;;
    esac

    stage="$work/$arch"
    mkdir -p "$stage"
    echo "==> building $arch ($image)"
    build "$platform" "$stage"

    binary="$(find "$stage" -type f -name krunner-remindme -print -quit)"
    if [ -z "$binary" ]; then
        echo "the build produced no krunner-remindme binary" >&2
        exit 1
    fi
    install -m 755 "$binary" "$payload/bin/krunner-remindme-$arch"
    if [ -z "$first_stage" ]; then
        first_stage="$stage"
    fi
done

# Architecture-independent: the runner and app metadata, the icon, the notification config and
# the catalogues, where the install rules place them.
for dir in applications icons knotifications6 krunner locale; do
    if [ -d "$first_stage/share/$dir" ]; then
        mkdir -p "$payload/share"
        cp -a "$first_stage/share/$dir" "$payload/share/"
    fi
done

tar -czf "$out" -C "$work" "$name"
echo "==> $out"
