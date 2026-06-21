#!/usr/bin/env sh
set -eu

show_help() {
    cat <<'EOF'
Usage:
  ./scripts/install.sh [--prefix <path>] [--system] [--no-build]

Options:
  --prefix <path>  Install prefix, default: $HOME/.local
  --system         Install to /usr/local
  --no-build       Skip make build and install the existing build/lia binary
  -h, --help       Show this help
EOF
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
prefix=${LIA_PREFIX:-"$HOME/.local"}
build=1

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            if [ "$#" -lt 2 ]; then
                echo "lia installer: --prefix requires a value" >&2
                exit 2
            fi
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
        --no-build)
            build=0
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

if ! command -v make >/dev/null 2>&1; then
    echo "lia installer: make is required" >&2
    exit 1
fi

cd "$project_dir"

if [ "$build" -eq 1 ]; then
    make build
fi

make install PREFIX="$prefix"

bin_dir="$prefix/bin"
echo "Lia installed at $bin_dir/lia"
case ":$PATH:" in
    *":$bin_dir:"*) ;;
    *)
        echo "Add this to your shell profile if needed:"
        echo "  export PATH=\"$bin_dir:\$PATH\""
        ;;
esac
