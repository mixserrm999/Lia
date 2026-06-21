LUA_VERSION ?= 5.4.7
LIA_VERSION ?= 0.1.1

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

THIRD_PARTY_DIR := third_party
LUA_ARCHIVE := $(THIRD_PARTY_DIR)/lua-$(LUA_VERSION).tar.gz
LUA_DIR := $(THIRD_PARTY_DIR)/lua-$(LUA_VERSION)
LUA_SRC_DIR := $(LUA_DIR)/src
BUILD_DIR := build
DIST_DIR := dist
DIST_PLATFORM ?= linux-x64
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

ifeq ($(OS),Windows_NT)
	EXE := .exe
	LUA_PLATFORM_FLAGS := -DLUA_USE_WINDOWS
	LDLIBS :=
	RM := del /Q
else
	EXE :=
	LUA_PLATFORM_FLAGS := -DLUA_USE_LINUX
	LDLIBS := -lm -ldl
	RM := rm -rf
endif

LUA_SOURCES := \
	$(LUA_SRC_DIR)/lapi.c \
	$(LUA_SRC_DIR)/lauxlib.c \
	$(LUA_SRC_DIR)/lbaselib.c \
	$(LUA_SRC_DIR)/lcode.c \
	$(LUA_SRC_DIR)/lcorolib.c \
	$(LUA_SRC_DIR)/lctype.c \
	$(LUA_SRC_DIR)/ldblib.c \
	$(LUA_SRC_DIR)/ldebug.c \
	$(LUA_SRC_DIR)/ldo.c \
	$(LUA_SRC_DIR)/ldump.c \
	$(LUA_SRC_DIR)/lfunc.c \
	$(LUA_SRC_DIR)/lgc.c \
	$(LUA_SRC_DIR)/linit.c \
	$(LUA_SRC_DIR)/liolib.c \
	$(LUA_SRC_DIR)/llex.c \
	$(LUA_SRC_DIR)/lmathlib.c \
	$(LUA_SRC_DIR)/lmem.c \
	$(LUA_SRC_DIR)/loadlib.c \
	$(LUA_SRC_DIR)/lobject.c \
	$(LUA_SRC_DIR)/lopcodes.c \
	$(LUA_SRC_DIR)/loslib.c \
	$(LUA_SRC_DIR)/lparser.c \
	$(LUA_SRC_DIR)/lstate.c \
	$(LUA_SRC_DIR)/lstring.c \
	$(LUA_SRC_DIR)/lstrlib.c \
	$(LUA_SRC_DIR)/ltable.c \
	$(LUA_SRC_DIR)/ltablib.c \
	$(LUA_SRC_DIR)/ltm.c \
	$(LUA_SRC_DIR)/lundump.c \
	$(LUA_SRC_DIR)/lutf8lib.c \
	$(LUA_SRC_DIR)/lvm.c \
	$(LUA_SRC_DIR)/lzio.c

BUILD_MARKER := $(BUILD_DIR)/.dir
LIA_BIN := $(BUILD_DIR)/lia$(EXE)
LIA_SOURCES := \
	src/common.c \
	src/install.c \
	src/json.c \
	src/main.c \
	src/manifest.c \
	src/packages.c \
	src/registry.c \
	src/runtime.c
LIA_HEADERS := \
	src/lia.h
INIT_TEST_DIR := $(BUILD_DIR)/init-smoke
INSTALL_TEST_DIR := $(BUILD_DIR)/install-smoke
PACKAGE_UX_TEST_DIR := $(BUILD_DIR)/package-ux-smoke
PHASE32_TEST_DIR := $(BUILD_DIR)/phase32-smoke
MANIFEST_TEST_DIR := $(BUILD_DIR)/manifest-smoke
REGISTRY_TEST_DIR := $(BUILD_DIR)/registry-smoke
REGISTRY_RANGE_TEST_DIR := $(BUILD_DIR)/registry-range-smoke
REGISTRY_LATEST_TEST_DIR := $(BUILD_DIR)/registry-latest-smoke
PUBLISH_TEST_DIR := $(BUILD_DIR)/publish-smoke
PUBLISH_INSTALL_TEST_DIR := $(BUILD_DIR)/publish-install-smoke
PUBLISH_HOME := $(BUILD_DIR)/publish-home
REGISTRY_ROOT := $(BUILD_DIR)/registry-store
REGISTRY_PORT := 7789
REGISTRY_TOKEN := test-token
LIA_CACHE_DIR := $(BUILD_DIR)/lia-cache
INSTALL_PREFIX := $(BUILD_DIR)/install-prefix
RELEASE_INSTALL_PREFIX := $(BUILD_DIR)/release-install-prefix
DIST_SMOKE_DIR := $(BUILD_DIR)/dist-smoke
INSTALL_FIXTURE_DIR := $(BUILD_DIR)/package-fixture
INSTALL_ARCHIVE := $(BUILD_DIR)/smoke-0.1.0.tar.gz
SMOKE_LATEST_ARCHIVE := $(BUILD_DIR)/smoke-0.2.0.tar.gz
BASE_ARCHIVE := $(BUILD_DIR)/base-0.1.0.tar.gz
FEATURE_ARCHIVE := $(BUILD_DIR)/feature-0.1.0.tar.gz

.PHONY: build clean dist distclean install uninstall test run help

help:
	@echo "Targets:"
	@echo "  make build      Build $(LIA_BIN)"
	@echo "  make dist       Build release archives in $(DIST_DIR)"
	@echo "  make install    Install lia to PREFIX/bin, default: /usr/local/bin"
	@echo "  make uninstall  Remove lia from PREFIX/bin"
	@echo "  make test       Run smoke tests"
	@echo "  make run        Run examples/hello.lua"
	@echo "  make clean      Remove build output"
	@echo "  make distclean  Remove build output and downloaded Lua source"

build: $(LIA_BIN)

dist: build
	python3 tools/make_dist.py --version "$(LIA_VERSION)" --platform "$(DIST_PLATFORM)" --binary "$(LIA_BIN)" --out "$(DIST_DIR)"

$(LIA_BIN): $(LIA_SOURCES) $(LIA_HEADERS) $(LUA_SOURCES) Makefile | $(BUILD_MARKER)
	$(CC) $(CFLAGS) $(LUA_PLATFORM_FLAGS) \
		-DLIA_VERSION=\"$(LIA_VERSION)\" \
		-I$(LUA_SRC_DIR) \
		$(LIA_SOURCES) $(LUA_SOURCES) \
		$(LDLIBS) -o $@

$(LUA_SRC_DIR)/lua.h:
	mkdir -p $(THIRD_PARTY_DIR)
	curl -fsSL https://www.lua.org/ftp/lua-$(LUA_VERSION).tar.gz -o $(LUA_ARCHIVE)
	tar -xzf $(LUA_ARCHIVE) -C $(THIRD_PARTY_DIR)

$(LUA_SOURCES): $(LUA_SRC_DIR)/lua.h

$(BUILD_MARKER):
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_MARKER)

install: build
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 0755 "$(LIA_BIN)" "$(DESTDIR)$(BINDIR)/lia$(EXE)"
	@echo "Installed lia to $(DESTDIR)$(BINDIR)/lia$(EXE)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/lia$(EXE)"
	@echo "Removed $(DESTDIR)$(BINDIR)/lia$(EXE)"

test: build
	./$(LIA_BIN) --version
	sh -n scripts/install.sh
	sh scripts/install.sh --help >/dev/null
	python3 -m py_compile tools/make_dist.py tools/registry/server.py
	if command -v pwsh >/dev/null 2>&1; then pwsh -NoProfile -File scripts/install.ps1 -Help >/dev/null; fi
	rm -rf $(INSTALL_PREFIX)
	$(MAKE) install PREFIX=$(abspath $(INSTALL_PREFIX))
	test -x $(INSTALL_PREFIX)/bin/lia$(EXE)
	$(INSTALL_PREFIX)/bin/lia$(EXE) --version
	$(MAKE) uninstall PREFIX=$(abspath $(INSTALL_PREFIX))
	test ! -e $(INSTALL_PREFIX)/bin/lia$(EXE)
	rm -rf $(DIST_SMOKE_DIR) $(RELEASE_INSTALL_PREFIX)
	python3 tools/make_dist.py --version "$(LIA_VERSION)" --platform "linux-x64" --binary "$(LIA_BIN)" --out "$(DIST_SMOKE_DIR)"
	cd $(DIST_SMOKE_DIR) && sha256sum -c SHA256SUMS
	sh scripts/install.sh --archive "$(abspath $(DIST_SMOKE_DIR))/lia-$(LIA_VERSION)-linux-x64.tar.gz" --prefix "$(abspath $(RELEASE_INSTALL_PREFIX))" --no-path
	test -x $(RELEASE_INSTALL_PREFIX)/bin/lia$(EXE)
	$(RELEASE_INSTALL_PREFIX)/bin/lia$(EXE) --version | grep -q 'lia $(LIA_VERSION)'
	sh scripts/install.sh --prefix "$(abspath $(RELEASE_INSTALL_PREFIX))" --uninstall --no-path
	test ! -e $(RELEASE_INSTALL_PREFIX)/bin/lia$(EXE)
	./$(LIA_BIN) examples/hello.lua Codex
	./$(LIA_BIN) examples/args.lua first second
	./$(LIA_BIN) examples/runtime.lua step10 | grep -q 'runtime=Lia $(LIA_VERSION)'
	./$(LIA_BIN) examples/runtime.lua step10 | grep -q 'arg1=step10'
	rm -rf .lia-stdlib-smoke
	./$(LIA_BIN) examples/stdlib.lua | grep -q 'stdlib=ok'
	rm -rf .lia-stdlib-smoke
	rm -rf $(INIT_TEST_DIR)
	mkdir -p $(INIT_TEST_DIR)
	cd $(INIT_TEST_DIR) && ../../$(LIA_BIN) init --name lia-smoke --main src/main.lua
	test -f $(INIT_TEST_DIR)/lia.json
	grep -q '"name": "lia-smoke"' $(INIT_TEST_DIR)/lia.json
	grep -q '"main": "src/main.lua"' $(INIT_TEST_DIR)/lia.json
	grep -q '"start": "lia src/main.lua"' $(INIT_TEST_DIR)/lia.json
	grep -q '"devDependencies": {}' $(INIT_TEST_DIR)/lia.json
	cd $(INIT_TEST_DIR) && ../../$(LIA_BIN) check
	cd $(INIT_TEST_DIR) && ! ../../$(LIA_BIN) init
	rm -rf $(INSTALL_TEST_DIR) $(PACKAGE_UX_TEST_DIR) $(INSTALL_FIXTURE_DIR) $(INSTALL_ARCHIVE) $(SMOKE_LATEST_ARCHIVE) $(BASE_ARCHIVE) $(FEATURE_ARCHIVE)
	mkdir -p $(INSTALL_FIXTURE_DIR)
	cp -R testdata/packages/smoke $(INSTALL_FIXTURE_DIR)/smoke
	cp -R testdata/packages/smoke $(INSTALL_FIXTURE_DIR)/smoke-0.2.0
	sed -i 's/"version": "0.1.0"/"version": "0.2.0"/' $(INSTALL_FIXTURE_DIR)/smoke-0.2.0/lia.json
	cp -R testdata/packages/base $(INSTALL_FIXTURE_DIR)/base
	cp -R testdata/packages/feature $(INSTALL_FIXTURE_DIR)/feature
	tar -czf $(INSTALL_ARCHIVE) -C $(INSTALL_FIXTURE_DIR) smoke
	tar -czf $(SMOKE_LATEST_ARCHIVE) -C $(INSTALL_FIXTURE_DIR) smoke-0.2.0
	tar -czf $(BASE_ARCHIVE) -C $(INSTALL_FIXTURE_DIR) base
	tar -czf $(FEATURE_ARCHIVE) -C $(INSTALL_FIXTURE_DIR) feature
	mkdir -p $(INSTALL_TEST_DIR)
	cd $(INSTALL_TEST_DIR) && ../../$(LIA_BIN) init --name install-smoke --main src/main.lua
	cd $(INSTALL_TEST_DIR) && ../../$(LIA_BIN) install ../smoke-0.1.0.tar.gz
	cd $(INSTALL_TEST_DIR) && ../../$(LIA_BIN) install ../feature-0.1.0.tar.gz
	test -f $(INSTALL_TEST_DIR)/packages/smoke/lia.json
	test -x $(INSTALL_TEST_DIR)/packages/.bin/smoke-cli
	test -f $(INSTALL_TEST_DIR)/packages/feature/lia.json
	test -f $(INSTALL_TEST_DIR)/packages/base/lia.json
	test -f $(INSTALL_TEST_DIR)/lia-lock.json
	grep -q '"smoke": "../smoke-0.1.0.tar.gz"' $(INSTALL_TEST_DIR)/lia.json
	grep -q '"feature": "../feature-0.1.0.tar.gz"' $(INSTALL_TEST_DIR)/lia.json
	! grep -q '"base": "../base-0.1.0.tar.gz' $(INSTALL_TEST_DIR)/lia.json
	grep -q '"smoke": {"version": "0.1.0", "source": "../smoke-0.1.0.tar.gz", "integrity": "sha256:' $(INSTALL_TEST_DIR)/lia-lock.json
	grep -F -q '"base": {"version": "0.1.0", "source": "../base-0.1.0.tar.gz#^0.1.0", "requirement": "^0.1.0", "integrity": "sha256:' $(INSTALL_TEST_DIR)/lia-lock.json
	rm -rf $(INSTALL_TEST_DIR)/packages
	cd $(INSTALL_TEST_DIR) && ../../$(LIA_BIN) install > restore.log
	cat $(INSTALL_TEST_DIR)/restore.log
	test "$$(grep -c '^Installed base@0.1.0$$' $(INSTALL_TEST_DIR)/restore.log)" -eq 1
	grep -q 'Installed packages from lia-lock.json' $(INSTALL_TEST_DIR)/restore.log
	test -f $(INSTALL_TEST_DIR)/packages/smoke/lia.json
	test -x $(INSTALL_TEST_DIR)/packages/.bin/smoke-cli
	test -f $(INSTALL_TEST_DIR)/packages/feature/lia.json
	test -f $(INSTALL_TEST_DIR)/packages/base/lia.json
	mkdir -p $(INSTALL_TEST_DIR)/src
	printf 'local smoke = require("smoke")\nlocal feature = require("feature")\nprint(smoke.name() .. ":" .. feature.name() .. ":" .. tostring(arg[1]))\n' > $(INSTALL_TEST_DIR)/src/main.lua
	cd $(INSTALL_TEST_DIR) && ../../$(LIA_BIN) src/main.lua direct
	cd $(INSTALL_TEST_DIR) && PATH=../../$(BUILD_DIR):$$PATH ../../$(LIA_BIN) run start via-run
	cd $(INSTALL_TEST_DIR) && ! ../../$(LIA_BIN) run missing
	sed -i '0,/sha256:/s/sha256:/sha256:bad/' $(INSTALL_TEST_DIR)/lia-lock.json
	rm -rf $(INSTALL_TEST_DIR)/packages
	cd $(INSTALL_TEST_DIR) && ! ../../$(LIA_BIN) install
	mkdir -p $(PACKAGE_UX_TEST_DIR)
	cd $(PACKAGE_UX_TEST_DIR) && ../../$(LIA_BIN) init --name package-ux-smoke --main src/main.lua
	cd $(PACKAGE_UX_TEST_DIR) && ../../$(LIA_BIN) install ../smoke-0.1.0.tar.gz
	cd $(PACKAGE_UX_TEST_DIR) && ../../$(LIA_BIN) list | grep -q 'smoke@0.1.0'
	cd $(PACKAGE_UX_TEST_DIR) && ../../$(LIA_BIN) info smoke | grep -q 'source: ../smoke-0.1.0.tar.gz'
	cd $(PACKAGE_UX_TEST_DIR) && ../../$(LIA_BIN) update smoke
	cd $(PACKAGE_UX_TEST_DIR) && ../../$(LIA_BIN) outdated | grep -q 'No registry packages'
	cd $(PACKAGE_UX_TEST_DIR) && ../../$(LIA_BIN) remove smoke
	test ! -d $(PACKAGE_UX_TEST_DIR)/packages/smoke
	test ! -e $(PACKAGE_UX_TEST_DIR)/packages/.bin/smoke-cli
	! grep -q '"smoke":' $(PACKAGE_UX_TEST_DIR)/lia.json
	! grep -q '"smoke":' $(PACKAGE_UX_TEST_DIR)/lia-lock.json
	cd $(PACKAGE_UX_TEST_DIR) && ! ../../$(LIA_BIN) info smoke
	rm -rf $(PHASE32_TEST_DIR)
	mkdir -p $(PHASE32_TEST_DIR)/src
	printf 'print("phase32-main")\n' > $(PHASE32_TEST_DIR)/src/main.lua
	printf '{\n  "name": "phase32-smoke",\n  "version": "0.1.0",\n  "main": "src/main.lua",\n  "scripts": {\n    "pretool": "echo pretool",\n    "tool": "smoke-cli",\n    "posttool": "echo posttool"\n  },\n  "dependencies": {},\n  "devDependencies": {}\n}\n' > $(PHASE32_TEST_DIR)/lia.json
	cd $(PHASE32_TEST_DIR) && ../../$(LIA_BIN) install --save-dev ../smoke-0.1.0.tar.gz
	grep -q '"devDependencies": {' $(PHASE32_TEST_DIR)/lia.json
	grep -q '"smoke": "../smoke-0.1.0.tar.gz"' $(PHASE32_TEST_DIR)/lia.json
	grep -q '"smoke": {"version": "0.1.0", "source": "../smoke-0.1.0.tar.gz", "integrity": "sha256:.*", "path": "packages/smoke", "dev": true}' $(PHASE32_TEST_DIR)/lia-lock.json
	cd $(PHASE32_TEST_DIR) && ../../$(LIA_BIN) list | grep -q 'smoke@0.1.0 \[dev\]'
	cd $(PHASE32_TEST_DIR) && ../../$(LIA_BIN) info smoke | grep -q 'dev: true'
	test -x $(PHASE32_TEST_DIR)/packages/.bin/smoke-cli
	cd $(PHASE32_TEST_DIR) && PATH=../../$(BUILD_DIR):$$PATH ../../$(LIA_BIN) run tool hello > run.log
	grep -q '^pretool$$' $(PHASE32_TEST_DIR)/run.log
	grep -q '^smoke-cli:hello$$' $(PHASE32_TEST_DIR)/run.log
	grep -q '^posttool$$' $(PHASE32_TEST_DIR)/run.log
	rm -rf $(PHASE32_TEST_DIR)/packages
	cd $(PHASE32_TEST_DIR) && ../../$(LIA_BIN) install --production
	test ! -e $(PHASE32_TEST_DIR)/packages/smoke/lia.json
	cd $(PHASE32_TEST_DIR) && ../../$(LIA_BIN) ci
	test -f $(PHASE32_TEST_DIR)/packages/smoke/lia.json
	grep -q '"smoke": {"version": "0.1.0", "source": "../smoke-0.1.0.tar.gz", "integrity": "sha256:.*", "path": "packages/smoke", "dev": true}' $(PHASE32_TEST_DIR)/lia-lock.json
	cp $(PHASE32_TEST_DIR)/lia.json $(PHASE32_TEST_DIR)/lia.json.ok
	sed -i 's#../smoke-0.1.0.tar.gz#../missing.tar.gz#' $(PHASE32_TEST_DIR)/lia.json
	cd $(PHASE32_TEST_DIR) && ! ../../$(LIA_BIN) ci
	mv $(PHASE32_TEST_DIR)/lia.json.ok $(PHASE32_TEST_DIR)/lia.json
	cd $(PHASE32_TEST_DIR) && ../../$(LIA_BIN) pack
	test -f $(PHASE32_TEST_DIR)/phase32-smoke-0.1.0.tar.gz
	tar -tzf $(PHASE32_TEST_DIR)/phase32-smoke-0.1.0.tar.gz | grep -q './lia.json'
	! tar -tzf $(PHASE32_TEST_DIR)/phase32-smoke-0.1.0.tar.gz | grep -q './packages/'
	cd $(PHASE32_TEST_DIR) && ../../$(LIA_BIN) remove smoke
	test ! -e $(PHASE32_TEST_DIR)/packages/.bin/smoke-cli
	rm -rf $(REGISTRY_TEST_DIR) $(REGISTRY_RANGE_TEST_DIR) $(REGISTRY_LATEST_TEST_DIR) $(PUBLISH_TEST_DIR) $(PUBLISH_INSTALL_TEST_DIR) $(PUBLISH_HOME) $(REGISTRY_ROOT) $(LIA_CACHE_DIR)
	mkdir -p $(REGISTRY_ROOT)/packages/smoke/0.1.0 $(REGISTRY_ROOT)/packages/smoke/0.2.0 $(REGISTRY_TEST_DIR) $(REGISTRY_RANGE_TEST_DIR) $(REGISTRY_LATEST_TEST_DIR)
	cp $(INSTALL_ARCHIVE) $(REGISTRY_ROOT)/packages/smoke/0.1.0/smoke-0.1.0.tar.gz
	cp $(SMOKE_LATEST_ARCHIVE) $(REGISTRY_ROOT)/packages/smoke/0.2.0/smoke-0.2.0.tar.gz
	( python3 tools/registry/server.py --root $(REGISTRY_ROOT) --host 127.0.0.1 --port $(REGISTRY_PORT) --token $(REGISTRY_TOKEN) > $(REGISTRY_ROOT)/server.log 2>&1 & \
		pid=$$!; \
		trap 'kill $$pid >/dev/null 2>&1 || true' EXIT; \
		ready=0; \
		for i in 1 2 3 4 5 6 7 8 9 10; do \
			if curl -fsSL http://127.0.0.1:$(REGISTRY_PORT)/health >/dev/null 2>&1; then ready=1; break; fi; \
			sleep 0.2; \
		done; \
		test $$ready -eq 1; \
		curl -fsSL http://127.0.0.1:$(REGISTRY_PORT)/packages/smoke/0.1.0 | grep -q '"name": "smoke"'; \
		( cd $(REGISTRY_TEST_DIR) && ../../$(LIA_BIN) init --name registry-smoke --main src/main.lua && \
			LIA_REGISTRY_URL=http://127.0.0.1:$(REGISTRY_PORT) ../../$(LIA_BIN) install smoke@0.1.0 && \
			LIA_REGISTRY_URL=http://127.0.0.1:$(REGISTRY_PORT) ../../$(LIA_BIN) outdated | grep -q 'smoke current=0.1.0 latest=0.2.0' && \
			test -f packages/smoke/lia.json && \
			grep -q '"smoke": "smoke@0.1.0"' lia.json && \
			grep -q '"smoke": {"version": "0.1.0", "source": "smoke@0.1.0", "integrity": "sha256:' lia-lock.json ); \
		( cd $(REGISTRY_RANGE_TEST_DIR) && ../../$(LIA_BIN) init --name registry-range-smoke --main src/main.lua && \
			LIA_CACHE_DIR=$(CURDIR)/$(LIA_CACHE_DIR) LIA_REGISTRY_URL=http://127.0.0.1:$(REGISTRY_PORT) ../../$(LIA_BIN) install 'smoke@^0.1.0' && \
			test -f packages/smoke/lia.json && \
			grep -q '"smoke": "smoke@^0.1.0"' lia.json && \
			grep -q '"smoke": {"version": "0.2.0", "source": "smoke@^0.1.0", "integrity": "sha256:' lia-lock.json && \
			rm -rf packages && \
			LIA_CACHE_DIR=$(CURDIR)/$(LIA_CACHE_DIR) LIA_REGISTRY_URL=http://127.0.0.1:1 ../../$(LIA_BIN) install && \
			test -f packages/smoke/lia.json && \
			grep -q '"version": "0.2.0"' packages/smoke/lia.json ); \
		( cd $(REGISTRY_LATEST_TEST_DIR) && ../../$(LIA_BIN) init --name registry-latest-smoke --main src/main.lua && \
			LIA_REGISTRY_URL=http://127.0.0.1:$(REGISTRY_PORT) ../../$(LIA_BIN) install smoke && \
			test -f packages/smoke/lia.json && \
			grep -q '"smoke": "smoke"' lia.json && \
			grep -q '"smoke": {"version": "0.2.0", "source": "smoke", "integrity": "sha256:' lia-lock.json ); \
		mkdir -p $(PUBLISH_TEST_DIR)/src $(PUBLISH_HOME) $(PUBLISH_INSTALL_TEST_DIR); \
		( cd $(PUBLISH_TEST_DIR) && ../../$(LIA_BIN) init --name published --main src/main.lua && \
			printf 'return { name = function() return "published-package" end }\n' > src/main.lua && \
			HOME=$(CURDIR)/$(PUBLISH_HOME) ../../$(LIA_BIN) login --registry http://127.0.0.1:$(REGISTRY_PORT) --token $(REGISTRY_TOKEN) && \
			HOME=$(CURDIR)/$(PUBLISH_HOME) ../../$(LIA_BIN) publish && \
			! HOME=$(CURDIR)/$(PUBLISH_HOME) ../../$(LIA_BIN) publish ); \
		curl -fsSL http://127.0.0.1:$(REGISTRY_PORT)/packages/published/0.1.0 | grep -q '"name": "published"'; \
		( cd $(PUBLISH_INSTALL_TEST_DIR) && ../../$(LIA_BIN) init --name publish-install-smoke --main src/main.lua && \
			LIA_REGISTRY_URL=http://127.0.0.1:$(REGISTRY_PORT) ../../$(LIA_BIN) install published@0.1.0 && \
			test -f packages/published/lia.json && \
			grep -q '"published": "published@0.1.0"' lia.json ); \
	)
	rm -rf $(MANIFEST_TEST_DIR)
	mkdir -p $(MANIFEST_TEST_DIR)
	printf '{"name":"bad"}\n' > $(MANIFEST_TEST_DIR)/lia.json
	cd $(MANIFEST_TEST_DIR) && ! ../../$(LIA_BIN) check
	printf '{"name":"bad","version":"0.1.0","main":"src/main.lua","scripts":{},"dependencies":{"bad":"../bad.tar.gz#^bad"}}\n' > $(MANIFEST_TEST_DIR)/lia.json
	cd $(MANIFEST_TEST_DIR) && ! ../../$(LIA_BIN) check

run: build
	./$(LIA_BIN) examples/hello.lua

clean:
	$(RM) $(BUILD_DIR)

distclean: clean
	$(RM) $(THIRD_PARTY_DIR)
	$(RM) $(DIST_DIR)
