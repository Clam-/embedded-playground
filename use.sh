#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SNIPPETS_DIR="$SCRIPT_DIR/snippets"
SRC_DIR="$SCRIPT_DIR/src"
PLATFORMIO_INI="$SCRIPT_DIR/platformio.ini"
LIB_DEPS_BEGIN="; BEGIN use.sh managed lib_deps"
LIB_DEPS_END="; END use.sh managed lib_deps"

snippet_lib_deps() {
    sed -n 's|^[[:space:]]*//[[:space:]]*platformio-lib-dep:[[:space:]]*||p' "$1"
}

write_managed_lib_deps() {
    local snippet_file="$1"
    local deps
    deps="$(snippet_lib_deps "$snippet_file")"

    local tmp_ini="${PLATFORMIO_INI}.tmp.$$"
    local block="${PLATFORMIO_INI}.lib_deps.$$"

    {
        echo "$LIB_DEPS_BEGIN"
        echo "; Generated from // platformio-lib-dep: comments in the active snippet."
        if [ -n "$deps" ]; then
            echo "[env]"
            echo "lib_deps ="
            printf "%s\n" "$deps" | while IFS= read -r dep; do
                if [ -n "$dep" ]; then
                    echo "    $dep"
                fi
            done
        else
            echo "; No active snippet library dependencies."
        fi
        echo "$LIB_DEPS_END"
    } > "$block"

    awk -v begin="$LIB_DEPS_BEGIN" -v end="$LIB_DEPS_END" -v block="$block" '
        function print_block() {
            while ((getline line < block) > 0) {
                print line
            }
            close(block)
            inserted = 1
        }

        $0 == begin {
            in_block = 1
            next
        }

        $0 == end {
            in_block = 0
            next
        }

        in_block {
            next
        }

        $0 == "; ---------- Boards ----------" && !inserted {
            print_block()
        }

        {
            print
        }

        END {
            if (!inserted) {
                print ""
                print_block()
            }
        }
    ' "$PLATFORMIO_INI" > "$tmp_ini"

    mv "$tmp_ini" "$PLATFORMIO_INI"
    rm -f "$block"
}

if [ $# -eq 0 ]; then
    echo "Usage: ./use.sh <snippet_name>"
    echo ""
    echo "Available snippets:"
    for f in "$SNIPPETS_DIR"/*.cpp; do
        name=$(basename "$f" .cpp)
        echo "  $name"
    done
    echo ""
    echo "Active snippet:"
    if [ -L "$SRC_DIR/main.cpp" ]; then
        target=$(readlink "$SRC_DIR/main.cpp")
        echo "  $(basename "$target" .cpp)"
    elif [ -f "$SRC_DIR/main.cpp" ]; then
        echo "  (custom file — not a symlink)"
    else
        echo "  (none)"
    fi
    exit 0
fi

SNIPPET="$1"
SNIPPET_FILE="$SNIPPETS_DIR/${SNIPPET}.cpp"
SNIPPET_LINK="../snippets/${SNIPPET}.cpp"

if [ ! -f "$SNIPPET_FILE" ]; then
    echo "Error: snippet '$SNIPPET' not found at snippets/${SNIPPET}.cpp"
    echo ""
    echo "Available snippets:"
    for f in "$SNIPPETS_DIR"/*.cpp; do
        echo "  $(basename "$f" .cpp)"
    done
    exit 1
fi

write_managed_lib_deps "$SNIPPET_FILE"
ln -sfn "$SNIPPET_LINK" "$SRC_DIR/main.cpp"
echo "Activated: $SNIPPET → src/main.cpp"
