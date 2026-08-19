# Lightweight developer recipes (build remains CMake-driven).
# Control-port coop workflow — documentation + watcher launch only.
# See agents/control-tools.md.

PORT ?= 6510
OUT_DIR ?= build/debug

.PHONY: help coop coop-watch

help:
	@echo "Targets:"
	@echo "  make coop         # print two-terminal coop recipe"
	@echo "  make coop-watch   # start tools/a2m_coop_watch.py (PORT=$(PORT))"
	@echo "Build with: cmake -B build && cmake --build build -j"

coop:
	@echo "Cooperative remote debug (A2M/5) — two terminals:"
	@echo ""
	@echo "  Terminal A (windowed, playable):"
	@echo "    ./build/a2m --control-port $(PORT)"
	@echo ""
	@echo "  Terminal B (watcher):"
	@echo "    tools/a2m_coop_watch.py --port $(PORT) --out-dir $(OUT_DIR)"
	@echo "    # or: make coop-watch PORT=$(PORT)"
	@echo ""
	@echo "Play; hit F10 to pause. Snaps: $(OUT_DIR)/snap-NNN.txt"
	@echo "Inbox (while frozen): append lines to $(OUT_DIR)/coop_inbox"
	@echo "  resume | arm write \$$C000 | hist \$$C000 | scrub 60 | note … | quit"
	@echo "Full notes: agents/control-tools.md"

coop-watch:
	@mkdir -p $(OUT_DIR)
	tools/a2m_coop_watch.py --port $(PORT) --out-dir $(OUT_DIR)
