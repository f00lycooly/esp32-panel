# esp32-panel — build/validate helper.
#
# Deploy-agnostic: this Makefile knows nothing about any particular site's
# deployment. It runs esphome either
#   * via a local `esphome` binary (default — what public users have), or
#   * inside a running ESPHome container when CONTAINER is set
#     (e.g. `make validate CONTAINER=ESPHome`). In container mode the config
#     tree is expected to already be present at /config inside the container —
#     syncing it there is the job of a site-specific deploy layer (see
#     deploy.local.mk below).
#
# The repo root IS the ESPHome config root: the device file, modules/,
# components/, assets/ and the vendored esphome-modular-lvgl-buttons/ library
# are all siblings, exactly as ESPHome expects them under <config>/. No
# deploy-time path rewriting is needed (the old private-repo Makefile rewrote
# `../../lib/` → `../lib/`; that transform is intentionally gone).
#
# Usage:
#   make generate               # expand swell-manifest.yaml → modules/generated/
#   make validate               # esphome config  (schema + codegen; fast)
#   make compile                # full C++ build (LOCAL esphome only; see below)
#   make upload DEVICE_IP=<ip>  # OTA-upload the last build to the panel
#   make logs DEVICE_IP=<ip>    # stream device logs
#   make flash DEVICE_IP=<ip>   # compile + upload
#   make test                   # host unit tests for panel-side C++ (tests/)
#   make validate CONTAINER=X   # same, but inside docker container X
#                               # (validate only: compile is refused there)
#   make hooks                  # install the secret-scan pre-commit hook
#   make secret-scan            # run the publish gate manually
#   make clean                  # remove local build artifacts (.esphome/)
#
# Variables:
#   DEVICE    ?= waveshare-p4-86-panel.yaml   # device file to build
#   CONTAINER ?=                              # empty = local esphome binary
#   ESPHOME   ?= esphome                      # local mode: path to the binary,
#                                             # e.g. ESPHOME=~/.venvs/esphome/bin/esphome
#   DEVICE_IP ?=                              # panel address for upload/logs/flash
#
# Compiles MUST run on a workstation, not in a shared server's ESPHome
# container: a cold ESP-IDF build of this firmware (2500+ objects, ~20-25 min)
# can exhaust the host's RAM/CPU and take co-hosted services down. Build
# locally, then OTA-upload. Container compiles need ALLOW_CONTAINER_COMPILE=1.

# Pipefail-safe shell: any pipeline stage failing fails the recipe line.
# (Never gate success on the exit code of a `... | tail` wrapper.)
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -ec

DEVICE    ?= waveshare-p4-86-panel.yaml
CONTAINER ?=
ESPHOME   ?= esphome
DEVICE_IP ?=
REPO_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# docker exec does NOT inherit the dashboard process's environment. ESPHome
# dashboards that relocate the platformio dirs (e.g. to a cache volume) need
# the same env in exec'd builds, or the two toolchains fight over the build
# dir's CMake cache. Harmless when the container doesn't use these paths.
PIO_ENV := -e PLATFORMIO_CACHE_DIR=/cache/platformio/cache \
           -e PLATFORMIO_PACKAGES_DIR=/cache/platformio/packages \
           -e PLATFORMIO_PLATFORMS_DIR=/cache/platformio/platforms

ifeq ($(strip $(CONTAINER)),)
  CONFIG_DIR := $(REPO_DIR)
  TARGET     := $(REPO_DIR)/$(DEVICE)
else
  ESPHOME    := docker exec $(PIO_ENV) "$(CONTAINER)" esphome
  CONFIG_DIR := /config
  TARGET     := /config/$(DEVICE)
endif

.PHONY: generate validate compile compile-guard upload logs flash test clean hooks secret-scan check-config

# Expand swell-manifest.yaml → modules/generated/swell-*.yaml.
# Runs on the host; generated files are committed.
generate:
	@echo "-> generating swell panel from swell-manifest.yaml"
	@python3 "$(REPO_DIR)/scripts/gen_swell_panel.py"

# Refuse to build without the two site files. Both are gitignored; the
# .example copies ship with the repo.
check-config:
ifeq ($(strip $(CONTAINER)),)
	@test -f "$(REPO_DIR)/user-config.yaml" || { \
	  echo "x user-config.yaml missing."; \
	  echo "  cp user-config.yaml.example user-config.yaml   # then edit for your site"; \
	  exit 1; }
	@test -f "$(REPO_DIR)/secrets.yaml" || { \
	  echo "x secrets.yaml missing."; \
	  echo "  cp secrets.yaml.example secrets.yaml   # then fill in real values"; \
	  exit 1; }
else
	@docker exec "$(CONTAINER)" sh -c '\
	  [ -f /config/user-config.yaml ] || { echo "x /config/user-config.yaml missing in $(CONTAINER) — deploy your site overlay first"; exit 1; }; \
	  [ -f /config/secrets.yaml ] || { echo "x /config/secrets.yaml missing in $(CONTAINER) — provision it out of band"; exit 1; }; \
	  missing=""; \
	  for k in esphome_api_key wifi_ssid wifi_password; do \
	    grep -q "^$$k:" /config/secrets.yaml || missing="$$missing $$k"; \
	  done; \
	  if [ -n "$$missing" ]; then \
	    echo "x secrets.yaml missing keys:$$missing"; \
	    exit 1; \
	  fi; \
	  echo "-> secrets.yaml OK (esphome_api_key, wifi_ssid, wifi_password present)"'
endif

validate: check-config
	@$(ESPHOME) config "$(TARGET)"

# Guard runs before check-config so a refusal never needs docker.
compile-guard:
ifneq ($(strip $(CONTAINER)),)
ifneq ($(ALLOW_CONTAINER_COMPILE),1)
	@echo "x refusing to compile inside container '$(CONTAINER)'."; \
	  echo "  A cold ESP-IDF build (2500+ objects, ~20-25 min) can exhaust the host's"; \
	  echo "  RAM/CPU and take co-hosted services down. Build with a local esphome"; \
	  echo "  (make compile [ESPHOME=/path/to/esphome]) and OTA-upload with make upload."; \
	  echo "  Override only if you know the host can take it: ALLOW_CONTAINER_COMPILE=1"; \
	  exit 1
endif
endif

compile: compile-guard check-config
	@$(ESPHOME) compile "$(TARGET)"

# Upload/logs run locally against the panel's address (OTA).
upload logs:
	@test -n "$(strip $(DEVICE_IP))" || { \
	  echo "x DEVICE_IP is required: make $@ DEVICE_IP=<panel-ip-or-mdns-name>"; \
	  exit 1; }
	@$(ESPHOME) $@ "$(TARGET)" --device "$(DEVICE_IP)"

# compile + upload. (`deploy` is left to the optional deploy.local.mk.)
flash: compile upload

# Host unit tests for header-only panel C++ (modules/*.h). Needs a C++17
# compiler only — no ESPHome. Binaries go to build/tests/ (gitignored).
CXX ?= g++
TEST_SRCS := $(wildcard $(REPO_DIR)/tests/media/*.cpp)
test:
	@mkdir -p "$(REPO_DIR)/build/tests"
	@for t in $(TEST_SRCS); do \
	  bin="$(REPO_DIR)/build/tests/$$(basename "$$t" .cpp)"; \
	  $(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(REPO_DIR)/modules" -o "$$bin" "$$t"; \
	  "$$bin"; \
	done

# Local build artifacts only. Never touches a container.
clean:
	@rm -rf "$(REPO_DIR)/.esphome"
	@echo "ok removed .esphome/ build cache"

# Publish gate: block private/site-identifying strings from ever being committed.
secret-scan:
	@bash "$(REPO_DIR)/scripts/secret-scan.sh"

hooks:
	@test -d "$(REPO_DIR)/.git" || { echo "x not a git repo (no .git/) — clone first"; exit 1; }
	@cp "$(REPO_DIR)/.pre-commit" "$(REPO_DIR)/.git/hooks/pre-commit"
	@chmod +x "$(REPO_DIR)/.git/hooks/pre-commit"
	@echo "ok installed .git/hooks/pre-commit (runs scripts/secret-scan.sh)"

# Site-specific deploy targets (OTA upload, container sync, device IPs) live
# in an optional, gitignored deploy.local.mk. This keeps the public repo free
# of any one site's infrastructure while letting operators add e.g.:
#   deploy: flash
# (set DEVICE_IP in that file, e.g. `DEVICE_IP ?= panel.local`), or any
# site-specific step such as syncing config elsewhere before `make flash`.
-include deploy.local.mk
