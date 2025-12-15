#!/usr/bin/env bash
set -euo pipefail
pandoc manual.md --defaults=pandoc.yaml -o "a2m User Manual.pdf"
echo "Built a2m User Manual.pdf"
