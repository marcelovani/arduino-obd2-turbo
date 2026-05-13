#!/bin/bash
# Installs git hooks from .github/hooks/ into .git/hooks/.
# Called automatically by `make build`.

echo "→ Installing git hooks..."

HOOK_SRC_DIR=".github/hooks"
GIT_HOOK_DIR=".git/hooks"

if [ ! -d ".git" ]; then
  echo "Error: must be run from the root of a git repository."
  exit 1
fi

cp -a "$HOOK_SRC_DIR/." "$GIT_HOOK_DIR/"
chmod +x "$GIT_HOOK_DIR"/pre-push

echo "✓ Git hooks installed in $GIT_HOOK_DIR"
