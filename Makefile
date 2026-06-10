.PHONY: help all assets build flash boards clean

# --- Paths & tools ---

SCRIPTS_DIR := scripts
PYTHON ?= python3

# --- Compile ---

PROFILE ?= release
FORCE_ASSETS ?=
VERBOSE ?=
NO_GIT_HASH ?=

# --- Upload ---

PORT ?=
VID ?= 303a
PID ?= 1001
AUTO_ATTACH ?= 1

# --- Flag assembly ---

BUILD_FLAGS :=
FLASH_FLAGS := -u --vid $(VID) --pid $(PID) --auto-attach $(AUTO_ATTACH)

ifneq ($(PROFILE),release)
BUILD_FLAGS += -m $(PROFILE)
endif
ifeq ($(FORCE_ASSETS),1)
BUILD_FLAGS += --force-assets
endif
ifeq ($(VERBOSE),1)
BUILD_FLAGS += -v
endif
ifeq ($(NO_GIT_HASH),1)
BUILD_FLAGS += --no-git-hash
endif
ifneq ($(PORT),)
FLASH_FLAGS += -p $(PORT)
endif

.DEFAULT_GOAL := help

all: build

# --- Help ---

help:
	@echo "SerialBridge Firmware"
	@echo ""
	@echo "Targets:"
	@echo "  make assets         generate web assets"
	@echo "  make build          compile firmware"
	@echo "  make flash          compile and upload"
	@echo "  make boards         list boards and serial ports"
	@echo "  make clean          remove build cache"
	@echo "  make help           show this help"
	@echo ""
	@echo "Variables:"
	@echo "  PROFILE=release     debug | debug-full | release | release-full"
	@echo "  FORCE_ASSETS=1      force generate web assets"
	@echo "  VERBOSE=1           verbose compile output"
	@echo "  NO_GIT_HASH=1       skip FW_VERSION_ID injection"
	@echo "  PORT=/dev/ttyACM0   serial port (optional if only one device)"
	@echo ""
	@echo "Examples:"
	@echo "  make"
	@echo "  make build"
	@echo "  make build PROFILE=debug"
	@echo "  make flash PORT=/dev/ttyACM0"
	@echo "  make boards"
	@echo "  make clean"

# --- Assets ---

assets:
	@cd $(SCRIPTS_DIR) && $(PYTHON) embed_assets.py

# --- Compile ---

build:
	@cd $(SCRIPTS_DIR) && $(PYTHON) build.py $(BUILD_FLAGS)

# --- Upload ---

flash:
	@cd $(SCRIPTS_DIR) && $(PYTHON) build.py $(FLASH_FLAGS) $(BUILD_FLAGS)

# --- Utilities ---

boards:
	@cd $(SCRIPTS_DIR) && $(PYTHON) build.py --board-list

clean:
	@cd $(SCRIPTS_DIR) && $(PYTHON) build.py --clean -m $(PROFILE)
