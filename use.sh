#!/usr/bin/env bash
set -euo pipefail

SNIPPETS_DIR="$(cd "$(dirname "$0")" && pwd)/snippets"
SRC_DIR="$(cd "$(dirname "$0")" && pwd)/src"

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

if [ ! -f "$SNIPPET_FILE" ]; then
    echo "Error: snippet '$SNIPPET' not found at $SNIPPET_FILE"
    echo ""
    echo "Available snippets:"
    for f in "$SNIPPETS_DIR"/*.cpp; do
        echo "  $(basename "$f" .cpp)"
    done
    exit 1
fi

ln -sf "$SNIPPET_FILE" "$SRC_DIR/main.cpp"
echo "Activated: $SNIPPET → src/main.cpp"
