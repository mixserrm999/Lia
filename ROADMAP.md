# Lia Roadmap

This file is the project memory for roadmap order and completion status.

## Goal

Build a Lua-based developer platform that feels similar to Node.js with npm:
a runtime, package manager, manifest file, lockfile, package registry, and easy
installers for Linux Ubuntu and Windows.

## Roadmap Status

1. Runtime command: `lia` can run Lua scripts.
   - Status: completed
   - Success criteria:
     - `make build` creates `build/lia`
     - `./build/lia --version` works
     - `./build/lia examples/hello.lua` runs a Lua file
     - script arguments are exposed through Lua's global `arg` table

2. Project initialization: `lia init`.
   - Status: completed
   - Success criteria:
     - creates `lia.json`
     - asks or defaults project name/version/main file

3. Package install from URL or GitHub: `lia install <source>`.
   - Status: completed
   - Success criteria:
     - downloads package into local package directory
     - updates `lia.json`
     - updates lockfile

4. Manifest file: `lia.json`.
   - Status: completed
   - Success criteria:
     - stores project name, version, main entry, scripts, dependencies

5. Dependency resolver.
   - Status: completed
   - Success criteria:
     - resolves direct and transitive dependencies
     - handles semver constraints

6. Lockfile: `lia-lock.json`.
   - Status: completed
   - Success criteria:
     - stores exact versions and package source integrity data
     - makes installs reproducible

7. Registry.
   - Status: completed
   - Success criteria:
     - exposes package metadata API
     - serves package archives

8. Publish and login.
   - Status: completed
   - Success criteria:
     - supports user authentication
     - supports `lia publish`

9. Linux and Windows installers.
   - Status: completed
   - Success criteria:
     - Ubuntu install script or `.deb`
     - Windows PowerShell installer

10. Custom Lua runtime changes.
    - Status: completed
    - Success criteria:
      - only starts after package/runtime UX is stable
      - changes are documented and tested

## Phase 2 Roadmap

Phase 2 starts after the MVP roadmap above is complete. The goal is to make Lia
maintainable, more npm-like in daily package workflows, and ready for real
releases.

11. Stabilize codebase.
    - Status: completed
    - Success criteria:
      - split `src/main.c` into focused modules such as CLI, manifest, install,
        registry, and runtime
      - keep behavior compatible with Steps 1-10
      - keep `make test` passing through the refactor

12. Package management UX.
    - Status: completed
    - Success criteria:
      - add npm-like commands such as `lia list`, `lia remove <package>`,
        `lia update`, `lia outdated`, and `lia info <package>`
      - update `lia.json` and `lia-lock.json` consistently
      - document each command

13. Registry version resolver.
    - Status: completed
    - Success criteria:
      - `lia install package` resolves the latest compatible version
      - `lia install package@^1.0.0` resolves a compatible registry version
      - resolver handles clear errors when no version matches

14. Cache and offline install.
    - Status: completed
    - Success criteria:
      - downloaded tarballs are stored in a local Lia cache
      - repeated installs reuse cached archives when integrity matches
      - offline restore works when required archives already exist in cache

15. Publish hardening.
    - Status: completed
    - Success criteria:
      - reject duplicate published versions
      - validate package manifest and archive contents before publish
      - add ownership and token scope checks to the registry

16. Release pipeline.
    - Status: completed
    - Success criteria:
      - build Linux and Windows artifacts automatically
      - produce release archives such as `.tar.gz` and `.zip`
      - include checksums for release artifacts
      - prepare room for `.deb` packaging

17. Runtime standard library.
    - Status: completed
    - Success criteria:
      - add Lia-owned modules such as `lia.fs`, `lia.path`, `lia.process`, or
        `lia.json`
      - document each runtime module
      - add tests for each runtime module

## Phase 3.1 Installer UX

Phase 3.1 makes Lia feel closer to installing Node.js with npm: one install
step should provide both the Lua runtime and the Lia package-manager CLI.

18. Node-like installer and release experience.
    - Status: completed
    - Success criteria:
      - Ubuntu/Linux users can install from the latest GitHub Release with one
        shell command
      - Windows users can install from the latest GitHub Release with one
        PowerShell command
      - installers detect platform and architecture before installing
      - installers download release archives, verify `SHA256SUMS` when
        installing from GitHub Releases, install `lia`, verify `lia --version`,
        and update or report PATH setup
      - installers support uninstall and source-checkout development installs
      - tag pushes create or update a GitHub Release with Linux and Windows
        archives plus a combined checksum file
      - tracked bootstrap archives provide a fallback install source when
        GitHub Releases are unavailable
      - smoke tests install from a generated release archive

## Phase 3.2 npm-like Daily Workflow

Phase 3.2 makes day-to-day Lia projects feel closer to Node.js with npm:
repeatable CI installs, separate development dependencies, package-provided
commands, script lifecycle hooks, and local package archives.

19. npm-like daily workflow.
    - Status: completed
    - Success criteria:
      - `lia ci` performs a strict lockfile restore and fails when `lia.json`
        and `lia-lock.json` disagree
      - `lia install --save-dev <source>` writes `devDependencies` and marks dev
        packages in `lia-lock.json`
      - `lia install --production` and `lia ci --production` skip dev packages
        during lockfile restore
      - packages can define `bin` commands that are installed into
        `packages/.bin`
      - `lia run <script>` prepends `packages/.bin` to `PATH` and runs
        `pre<script>` / `post<script>` lifecycle hooks
      - `lia pack` creates a local package archive and `lia publish` shares the
        same archive creation path
      - smoke tests cover CI restore, dev dependencies, bin shims, lifecycle
        scripts, and pack archives

## Latest Progress

- 2026-06-21: Started Step 1 by scaffolding a C-based `lia` runner that embeds
  Lua 5.4 source during build.
- 2026-06-21: Completed Step 1. `make test` builds `build/lia`, prints the
  runtime version, runs `examples/hello.lua`, and exposes script arguments
  through the global `arg` table.
- 2026-06-21: Standardized the platform name as `Lia`. The CLI command and
  binary are `lia`; future manifest and lockfile names are `lia.json` and
  `lia-lock.json`.
- 2026-06-21: Completed Step 2. `lia init` now creates `lia.json` with project
  name, version, main entry, start script, and empty dependencies. It defaults
  the project name from the current directory, supports `--name`, `--main`, and
  `--force`, and refuses to overwrite an existing manifest unless forced.
- 2026-06-21: Completed Step 3. `lia install <source>` supports
  `github:owner/repo@ref`, direct `.tar.gz`/`.tgz`/`.zip` URLs, and local archive
  paths for tests/development. It extracts packages with root `lia.json`, copies
  them into `packages/<name>`, updates `lia.json` dependencies, writes a minimal
  `lia-lock.json`, and expands Lua module resolution for installed packages.
- 2026-06-21: Completed Step 4. `lia check` validates project `lia.json` fields,
  and `lia run <script> [args...]` executes commands from the manifest `scripts`
  object. `lia install` now validates the project manifest before updating
  dependencies.
- 2026-06-21: Completed Step 5. `lia install` now resolves package dependencies
  recursively, installs transitive packages into `packages/<name>`, keeps
  transitive dependencies out of the top-level `lia.json`, records them in
  `lia-lock.json`, detects circular dependency paths, and supports semver
  constraints with `source#constraint` syntax.
- 2026-06-21: Completed Step 6. `lia-lock.json` now records package source,
  exact version, optional requirement, install path, and `sha256:` archive
  integrity. `lia install` with no source restores packages from the lockfile and
  verifies integrity before extraction.
- 2026-06-21: Completed Step 7. Added a file-backed local registry server at
  `tools/registry/server.py` with health, package metadata, latest-version, and
  tarball endpoints. `lia install package@version` now resolves packages through
  `LIA_REGISTRY_URL` and installs them through the existing archive/integrity
  flow.
- 2026-06-21: Completed Step 8. Added token auth to the local registry,
  `GET /auth/check`, and `PUT /publish`. Added `lia login --token <token>` to
  verify and store registry credentials, and `lia publish` to package the current
  project and upload it to the registry.
- 2026-06-21: Completed Step 9. Added `make install` and `make uninstall`, a
  Linux/Ubuntu installer at `scripts/install.sh`, and a Windows PowerShell
  installer at `scripts/install.ps1`. The smoke test now validates the Makefile
  install/uninstall path, shell installer syntax, and PowerShell installer help
  output when `pwsh` is available.
- 2026-06-21: Completed Step 10. Added Lia's first custom runtime API:
  `require("lia")` and `_LIA` expose runtime metadata, Lua version information,
  platform, manifest/lockfile/package directory names, script arguments,
  `cwd()`, and `env(name)`. Added `examples/runtime.lua`, documentation, and
  smoke tests for the runtime API.
- 2026-06-21: Recorded Phase 2 roadmap as planned Steps 11-17: codebase
  stabilization, package management UX, registry version resolver, cache/offline
  installs, publish hardening, release pipeline, and Lia runtime standard
  library.
- 2026-06-21: Completed Step 11. Split the monolithic C CLI into focused source
  files: `common`, `json`, `manifest`, `install`, `registry`, `packages`,
  `runtime`, and `main`. `make test` stayed green after the refactor.
- 2026-06-21: Completed Step 12. Added npm-like package commands:
  `lia list`, `lia remove <package>`, `lia update [package]`,
  `lia outdated`, and `lia info <package>`.
- 2026-06-21: Completed Step 13. Registry installs now resolve `package`,
  exact `package@version`, and semver ranges such as `package@^1.0.0` through
  the registry versions index.
- 2026-06-21: Completed Step 14. Added archive cache support with
  `LIA_CACHE_DIR` override and lockfile offline restore from cache when
  `sha256:` integrity matches.
- 2026-06-21: Completed Step 15. Hardened publish by validating package
  manifests, checking the project main file before publish, rejecting duplicate
  registry versions, and recording package ownership by token in the local
  registry.
- 2026-06-21: Completed Step 16. Added `make dist`, `tools/make_dist.py`,
  `.tar.gz` and `.zip` release archives, `SHA256SUMS`, and a GitHub Actions
  workflow for Linux and Windows release artifacts.
- 2026-06-21: Completed Step 17. Added Lia runtime standard-library modules:
  `lia.fs`, `lia.path`, and `lia.process`, with `examples/stdlib.lua`,
  documentation, and smoke tests.
- 2026-06-21: Cleaned up lockfile restore UX so `lia install` restores each
  lockfile package once instead of recursively reinstalling transitive
  dependencies that are already represented in `lia-lock.json`.
- 2026-06-21: Completed Phase 3.1 installer UX. Linux and Windows installers
  now support one-line release installs, release checksum verification, PATH
  setup, uninstall, source-checkout installs, and release-archive smoke tests.
  The release workflow now publishes GitHub Release assets for tag pushes.
- 2026-06-21: Added bootstrap archive fallback for installers. If GitHub
  Releases are unavailable, installers can download checked-in `dist/` archives
  from the repository/tag and still verify `SHA256SUMS`.
- 2026-06-21: Completed Phase 3.2 npm-like daily workflow. Added `lia ci`,
  `devDependencies`, `lia install --save-dev`, production lockfile restores,
  package `bin` shims in `packages/.bin`, lifecycle scripts for `lia run`, and
  `lia pack` shared with publish archive creation.
