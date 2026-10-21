#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

REMOTE="${1:-https://github.com/BOBILLEChristophe/DCC-LocoAddr.git}"

if [ ! -d .git ]; then
  git init -b main
fi

if ! git config user.name >/dev/null 2>&1; then
  echo "Configurez d'abord votre identité Git :"
  echo "  git config user.name \"Christophe BOBILLE\""
  echo "  git config user.email \"votre-adresse-git\""
  exit 1
fi

if ! git rev-parse --verify HEAD >/dev/null 2>&1; then
  git add .
  git commit -m "Initial DCC-LocoAddr v5.0.0"
fi

if git remote get-url origin >/dev/null 2>&1; then
  git remote set-url origin "$REMOTE"
else
  git remote add origin "$REMOTE"
fi

echo "Dépôt local prêt. Pour publier :"
echo "  git push -u origin main"
