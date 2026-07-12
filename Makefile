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
#   make compile                # full C++ build
#   make validate CONTAINER=X   # same, but inside docker container X
#   make hooks                  # install the secret-scan pre-commit hook
#   make secret-scan            # run the publish gate manually
#   make clean                  # remove local build artifacts (.esphome/)
#
# Variables:
#   DEVICE    ?= waveshare-p4-86-panel.yaml   # device file to build
#   CONTAINER ?=                              # empty = local esphome binary

# Pipefail-safe shell: any pipeline stage failing fails the recipe line.
# (Never gate success on the exit code of a `... | tail` wrapper.)
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -ec

DEVICE    ?= waveshare-p4-86-panel.yaml
CONTAINER ?=
REPO_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# docker exec does NOT inherit the dashboard process's environment. ESPHome
# dashboards that relocate the platformio dirs (e.g. to a cache volume) need
# the same env in exec'd builds, or the two toolchains fight over the build
# dir's CMake cache. Harmless when the container doesn't use these paths.
PIO_ENV := -e PLATFORMIO_CACHE_DIR=/cache/platformio/cache \
           -e PLATFORMIO_PACKAGES_DIR=/cache/platformio/packages \
           -e PLATFORMIO_PLATFORMS_DIR=/cache/platformio/platforms

ifeq ($(strip $(CONTAINER)),)
  ESPHOME    := esphome
  CONFIG_DIR := $(REPO_DIR)
  TARGET     := $(REPO_DIR)/$(DEVICE)
else
  ESPHOME    := docker exec $(PIO_ENV) "$(CONTAINER)" esphome
  CONFIG_DIR := /config
  TARGET     := /config/$(DEVICE)
endif

.PHONY: generate validate compile clean hooks secret-scan check-config

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

compile: check-config
	@$(ESPHOME) compile "$(TARGET)"

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
#   deploy: compile
#   	docker exec ESPHome esphome upload /config/$(DEVICE) --device $(IP)
-include deploy.local.mk
