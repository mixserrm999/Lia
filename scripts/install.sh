#!/usr/bin/env sh
set -eu

repo=${LIA_REPO:-"mixserrm999/Lia"}
version=${LIA_VERSION:-"latest"}
bootstrap_version=${LIA_BOOTSTRAP_VERSION:-"0.1.1"}
prefix=${LIA_PREFIX:-"$HOME/.local"}
archive=${LIA_ARCHIVE:-}
mode=auto
build=1
add_path=1
uninstall=0

show_help() {
    cat <<'EOF'
Usage:
  sh install.sh [options]
  curl -fsSL https://raw.githubusercontent.com/mixserrm999/Lia/main/scripts/install.sh | sh

Options:
  --prefix <path>    Install prefix, default: $HOME/.local
  --system           Install to /usr/local
  --repo <owner/repo>
                     GitHub repository, default: mixserrm999/Lia
  --version <tag>    Release tag or version, default: latest
  --archive <path-or-url>
                     Install from a local or remote release archive
  --from-source      Build and install from the current source tree
  --no-build         With --from-source, install the existing build/lia binary
  --no-path          Do not update shell profile PATH entries
  --uninstall        Remove lia from the selected prefix
  -h, --help         Show this help

Environment:
  LIA_PREFIX         Default install prefix
  LIA_REPO           GitHub repository
  LIA_VERSION        Release tag or version
  LIA_BOOTSTRAP_VERSION
                     Fallback version used when GitHub Releases are unavailable
  LIA_ARCHIVE        Local or remote release archive
  LIA_PROFILE        Shell profile to update, default: $HOME/.profile
EOF
}

fail() {
    echo "lia installer: $*" >&2
    exit 1
}

have() {
    command -v "$1" >/dev/null 2>&1
}

is_url() {
    case "$1" in
        http://*|https://*) return 0 ;;
        *) return 1 ;;
    esac
}

download_to_file() {
    url=$1
    output=$2
    if have curl; then
        curl -fsSL "$url" -o "$output"
    elif have wget; then
        wget -q "$url" -O "$output"
    else
        fail "curl or wget is required to download release assets"
    fi
}

try_download_to_file() {
    url=$1
    output=$2
    if have curl; then
        curl -fsSL "$url" -o "$output" 2>/dev/null
    elif have wget; then
        wget -q "$url" -O "$output" 2>/dev/null
    else
        fail "curl or wget is required to download release assets"
    fi
}

try_download_to_stdout() {
    url=$1
    if have curl; then
        curl -fsSL "$url" 2>/dev/null
    elif have wget; then
        wget -q "$url" -O - 2>/dev/null
    else
        fail "curl or wget is required to download release metadata"
    fi
}

detect_platform() {
    os=$(uname -s 2>/dev/null || echo unknown)
    arch=$(uname -m 2>/dev/null || echo unknown)

    case "$os" in
        Linux) ;;
        *) fail "unsupported OS '$os'; this installer currently supports Linux" ;;
    esac

    case "$arch" in
        x86_64|amd64) echo "linux-x64" ;;
        *) fail "unsupported CPU '$arch'; this installer currently supports x64" ;;
    esac
}

normalize_tag() {
    case "$1" in
        v*) echo "$1" ;;
        *) echo "v$1" ;;
    esac
}

version_from_tag() {
    tag=$1
    case "$tag" in
        v*) echo "${tag#v}" ;;
        *) echo "$tag" ;;
    esac
}

latest_tag() {
    metadata=$(try_download_to_stdout "https://api.github.com/repos/$repo/releases/latest") ||
        return 1
    tag=$(printf '%s\n' "$metadata" | sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
    [ -n "$tag" ] || return 1
    echo "$tag"
}

asset_url() {
    source=$1
    tag=$2
    name=$3

    if [ "$source" = "release" ]; then
        echo "https://github.com/$repo/releases/download/$tag/$name"
    else
        echo "https://raw.githubusercontent.com/$repo/$tag/dist/$name"
    fi
}

install_binary() {
    source_bin=$1
    bin_dir=$prefix/bin
    target=$bin_dir/lia

    mkdir -p "$bin_dir"
    cp "$source_bin" "$target"
    chmod 0755 "$target"
    "$target" --version >/dev/null
    echo "Lia installed at $target"
}

ensure_path() {
    bin_dir=$prefix/bin
    case ":$PATH:" in
        *":$bin_dir:"*)
            return 0
            ;;
    esac

    if [ "$add_path" -ne 1 ]; then
        echo "Add this to your shell profile if needed:"
        echo "  export PATH=\"$bin_dir:\$PATH\""
        return 0
    fi

    profile=${LIA_PROFILE:-"$HOME/.profile"}
    mkdir -p "$(dirname "$profile")"
    touch "$profile"
    if grep -F "$bin_dir" "$profile" >/dev/null 2>&1; then
        echo "Open a new terminal or run:"
        echo "  export PATH=\"$bin_dir:\$PATH\""
        return 0
    fi

    {
        echo ""
        echo "# Lia"
        echo "export PATH=\"$bin_dir:\$PATH\""
    } >> "$profile"

    echo "Added $bin_dir to $profile"
    echo "Open a new terminal or run:"
    echo "  export PATH=\"$bin_dir:\$PATH\""
}

install_from_source() {
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
    project_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

    have make || fail "make is required for --from-source"

    cd "$project_dir"
    if [ "$build" -eq 1 ]; then
        make build
    fi
    make install PREFIX="$prefix"
    "$prefix/bin/lia" --version >/dev/null
    echo "Lia installed at $prefix/bin/lia"
    ensure_path
}

install_from_archive() {
    platform=$(detect_platform)
    tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/lia-install.XXXXXX")
    trap 'rm -rf "$tmp_dir"' EXIT INT TERM

    if [ -n "$archive" ]; then
        archive_name=$(basename "$archive")
        archive_path=$tmp_dir/$archive_name
        if is_url "$archive"; then
            download_to_file "$archive" "$archive_path"
        else
            [ -f "$archive" ] || fail "archive not found: $archive"
            cp "$archive" "$archive_path"
        fi
    else
        asset_source=release
        if [ "$version" = "latest" ]; then
            if ! tag=$(latest_tag); then
                tag=$(normalize_tag "$bootstrap_version")
                asset_source=raw
            fi
        else
            tag=$(normalize_tag "$version")
        fi
        asset_version=$(version_from_tag "$tag")
        archive_name="lia-$asset_version-$platform.tar.gz"
        archive_path=$tmp_dir/$archive_name
        if ! try_download_to_file "$(asset_url "$asset_source" "$tag" "$archive_name")" "$archive_path"; then
            asset_source=raw
            download_to_file "$(asset_url "$asset_source" "$tag" "$archive_name")" "$archive_path"
        fi

        checksum_path=$tmp_dir/SHA256SUMS
        if try_download_to_file "$(asset_url "$asset_source" "$tag" "SHA256SUMS")" "$checksum_path"; then
            if have sha256sum; then
                (cd "$tmp_dir" && grep "  $archive_name\$" SHA256SUMS > SHA256SUMS.one && sha256sum -c SHA256SUMS.one)
            else
                echo "sha256sum not found; skipping checksum verification"
            fi
        else
            echo "Could not download SHA256SUMS; skipping checksum verification"
        fi
    fi

    extract_dir=$tmp_dir/extract
    mkdir -p "$extract_dir"
    tar -xzf "$archive_path" -C "$extract_dir"
    source_bin=$(find "$extract_dir" -path "*/bin/lia" -type f | head -n 1)
    [ -n "$source_bin" ] || fail "archive does not contain bin/lia"

    install_binary "$source_bin"
    ensure_path
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            [ "$#" -ge 2 ] || fail "--prefix requires a value"
            prefix=$2
            shift 2
            ;;
        --prefix=*)
            prefix=${1#--prefix=}
            shift
            ;;
        --system)
            prefix=/usr/local
            shift
            ;;
        --repo)
            [ "$#" -ge 2 ] || fail "--repo requires a value"
            repo=$2
            shift 2
            ;;
        --repo=*)
            repo=${1#--repo=}
            shift
            ;;
        --version)
            [ "$#" -ge 2 ] || fail "--version requires a value"
            version=$2
            shift 2
            ;;
        --version=*)
            version=${1#--version=}
            shift
            ;;
        --archive)
            [ "$#" -ge 2 ] || fail "--archive requires a value"
            archive=$2
            mode=release
            shift 2
            ;;
        --archive=*)
            archive=${1#--archive=}
            mode=release
            shift
            ;;
        --from-source)
            mode=source
            shift
            ;;
        --no-build)
            build=0
            shift
            ;;
        --no-path)
            add_path=0
            shift
            ;;
        --uninstall)
            uninstall=1
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "lia installer: unknown option: $1" >&2
            show_help >&2
            exit 2
            ;;
    esac
done

if [ "$uninstall" -eq 1 ]; then
    target=$prefix/bin/lia
    if [ -e "$target" ]; then
        rm -f "$target"
        echo "Removed $target"
    else
        echo "Lia is not installed at $target"
    fi
    exit 0
fi

if [ "$mode" = "auto" ] && [ -n "${0:-}" ] && [ -f "$0" ]; then
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd || true)
    if [ -n "$script_dir" ] && [ -f "$script_dir/../Makefile" ]; then
        mode=source
    fi
fi

if [ "$mode" = "source" ]; then
    install_from_source
else
    install_from_archive
fi
