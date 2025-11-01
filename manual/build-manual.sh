#!/usr/bin/env bash
set -euo pipefail
pandoc manual.md --defaults=pandoc.yaml -o manual.pdf
echo "Built manual.pdf"
