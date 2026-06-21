# Lia

`lia` is the first step toward a Lua developer platform with a package manager
experience similar to Node.js and npm.

## Current Status

Lia now has the MVP runtime/package-manager flow plus the Phase 2 foundation:
modular C sources, npm-like package commands, registry version resolution,
archive cache/offline restore, hardened publish checks, release archives, and a
small runtime standard library. Phase 3 now includes installer UX plus daily
npm-like workflows: strict CI installs, dev dependencies, package bin commands,
script lifecycle hooks, and package archives.

```sh
make build
make install PREFIX="$HOME/.local"
./build/lia examples/hello.lua
./build/lia examples/runtime.lua
./build/lia init
./build/lia check
./build/lia run start
./build/lia install smoke@0.1.0
./build/lia install --save-dev smoke@0.1.0
./build/lia install 'smoke@^0.1.0'
./build/lia install --production
./build/lia ci
./build/lia list
./build/lia info smoke
./build/lia outdated
./build/lia update smoke
./build/lia remove smoke
./build/lia login --token dev-token
./build/lia pack
./build/lia publish
make dist
./build/lia install github:owner/repo@v0.1.0
./build/lia install
```

Useful commands:

```sh
make install PREFIX="$HOME/.local"
make uninstall PREFIX="$HOME/.local"
./build/lia init
./build/lia init --name my-app --main src/main.lua
./build/lia check
./build/lia run start
./build/lia run start --port 8080
./build/lia login --token dev-token
./build/lia login --registry http://127.0.0.1:7788 --token dev-token
./build/lia pack
./build/lia publish
./build/lia install
./build/lia install --production
./build/lia ci
./build/lia ci --production
./build/lia list
./build/lia info package-name
./build/lia outdated
./build/lia update
./build/lia update package-name
./build/lia remove package-name
./build/lia install package-name
./build/lia install --save-dev package-name
./build/lia install package-name@0.1.0
./build/lia install 'package-name@^1.0.0'
./build/lia install github:owner/repo@ref
./build/lia install https://example.com/package.tar.gz
./build/lia install https://example.com/package.tgz
./build/lia install https://example.com/package.zip
./build/lia install ./local-package.tar.gz
./build/lia --help
./build/lia --version
./build/lia path/to/script.lua arg1 arg2
make dist
```

## Runtime API

Lia scripts can use the built-in `lia` module. The same table is also available
as the `_LIA` global:

```lua
local lia = require("lia")

print(lia.version)
print(lia.lua_release)
print(lia.platform)
print(lia.cwd())
print(lia.env("HOME"))
print(lia.args[1])
```

Current runtime fields:

- `name`
- `version`
- `lua_version`
- `lua_release`
- `platform`
- `manifest_file`
- `lock_file`
- `packages_dir`
- `args`
- `cwd()`
- `env(name)`

Lia also includes small standard-library modules:

```lua
local fs = require("lia.fs")
local path = require("lia.path")
local process = require("lia.process")

local file = path.join("tmp", "hello.txt")
fs.mkdir("tmp")
fs.write(file, "hello")
print(fs.read(file))
print(process.cwd())
```

Current stdlib modules:

- `lia.fs`: `read`, `write`, `exists`, `is_dir`, `mkdir`
- `lia.path`: `join`
- `lia.process`: `cwd`, `env`, `platform`

## Installation

Lia release installs include the Lua runtime and the package-manager CLI in one
`lia` binary. Users do not need to install Lua separately. Installers download
GitHub Release assets first and fall back to the tracked bootstrap archives in
`dist/` if a GitHub Release has not been published yet.

Ubuntu/Linux one-line install:

```sh
curl -fsSL https://raw.githubusercontent.com/mixserrm999/Lia/main/scripts/install.sh | sh
```

Ubuntu/Linux system install:

```sh
curl -fsSL https://raw.githubusercontent.com/mixserrm999/Lia/main/scripts/install.sh | sudo sh -s -- --system
```

Windows PowerShell one-line install:

```powershell
powershell -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/mixserrm999/Lia/main/scripts/install.ps1 | iex"
```

After install, open a new terminal if PATH was changed, then run:

```sh
lia --version
lia init
lia install
lia run start
```

Install a specific release:

```sh
curl -fsSL https://raw.githubusercontent.com/mixserrm999/Lia/main/scripts/install.sh | sh -s -- --version v0.1.1
```

Install from a local release archive:

```sh
sh scripts/install.sh --archive dist/lia-0.1.1-linux-x64.tar.gz
```

Uninstall from the default Linux prefix:

```sh
sh scripts/install.sh --uninstall
```

Source checkout install for development:

```sh
./scripts/install.sh --from-source
```

Windows uninstall from the default prefix:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Uninstall
```

Windows source checkout install for development:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -FromSource
```

Manual install with `make` is still supported:

```sh
make build
make install PREFIX="$HOME/.local"
```

`lia init` creates a `lia.json` manifest in the current directory and refuses to
overwrite an existing manifest unless `--force` is passed.

`lia check` validates the project manifest. It requires `name`, `version`, and
`main` string fields, plus `scripts` and `dependencies` objects. It also
validates optional `devDependencies` and `bin` fields when they are present.

`lia run <script>` reads a command from `scripts` in `lia.json`, runs optional
`pre<script>` and `post<script>` lifecycle hooks, prepends `packages/.bin` to
`PATH`, sets `LIA_PACKAGE_BIN_DIR`, and passes extra arguments to the main
script command.

`lia install <source>` expects the archive to contain a package-level `lia.json`
at its root, or inside the archive's single top-level package directory. It
installs the package into `packages/<name>`, updates `lia.json`, and writes
`lia-lock.json`.

`lia install` without a source restores packages from `lia-lock.json`. The
lockfile records each package source, resolved version, install path, and
`sha256:` archive integrity. Restore verifies integrity before extracting an
archive.

`lia install --save-dev <source>` records a direct package under
`devDependencies` and marks the package plus its transitive dependencies as
`"dev": true` in `lia-lock.json`. `lia install --production` restores from the
lockfile while skipping dev packages.

`lia ci` is the strict CI restore path. It checks that `lia.json` and
`lia-lock.json` agree for direct dependencies, removes `packages/`, and restores
from the lockfile. `lia ci --production` applies the same strict check while
skipping dev packages during restore.

Registry installs use `LIA_REGISTRY_URL`, defaulting to
`http://127.0.0.1:7788`:

```sh
LIA_REGISTRY_URL=http://127.0.0.1:7788 lia install smoke@0.1.0
LIA_REGISTRY_URL=http://127.0.0.1:7788 lia install 'smoke@^0.1.0'
LIA_REGISTRY_URL=http://127.0.0.1:7788 lia install smoke
```

`lia login` verifies a Bearer token against the registry and stores credentials
at `$HOME/.lia/credentials.json`. `lia pack` creates a local
`<name>-<version>.tar.gz` package archive. `lia publish` uses the same archive
logic, excluding `.git`, build/cache/package directories, and uploads it to the
configured registry. The local registry rejects duplicate published versions and
records package ownership by token.

Packages can expose command-line tools through `bin`. Lia creates executable
shims in `packages/.bin` when a package is installed:

```json
{
  "name": "smoke",
  "version": "0.1.0",
  "main": "src/smoke.lua",
  "bin": {
    "smoke-cli": "bin/smoke-cli.lua"
  },
  "dependencies": {}
}
```

Downloaded archives are cached under `$HOME/.lia/cache` by default. Set
`LIA_CACHE_DIR` to override this location. `lia install` from `lia-lock.json`
can restore from cache without contacting the original source when the cached
archive matches the lockfile `sha256:` integrity.

Package dependencies can point to install sources. During install, Lia resolves
transitive dependencies recursively:

```json
{
  "dependencies": {
    "base": "https://example.com/base-0.1.0.tar.gz#^0.1.0"
  }
}
```

The optional `#<constraint>` suffix checks the installed package version. Current
constraints support exact versions, `^`, `~`, and comparisons such as
`>=1.0.0 <2.0.0`.

Supported install sources:

```sh
lia install package-name
lia install package-name@0.1.0
lia install 'package-name@^1.0.0'
lia install github:owner/repo@ref
lia install https://example.com/package.tar.gz
lia install https://example.com/package.tgz
lia install https://example.com/package.zip
lia install ./local-package.tar.gz
```

## Release Archives

Create local release archives and checksums:

```sh
make dist
cat dist/SHA256SUMS
```

The GitHub Actions workflow in `.github/workflows/release.yml` builds Linux and
Windows artifacts for tag pushes and manual runs. On tag pushes, it also creates
or updates the matching GitHub Release and uploads `.tar.gz`, `.zip`, and a
combined `SHA256SUMS` file. The release script is `tools/make_dist.py`.

The repository also tracks bootstrap release archives under `dist/` so the
one-line installers still work before GitHub Actions has published a formal
Release. Regenerate them with:

```sh
rm -rf dist
make dist DIST_PLATFORM=linux-x64
make dist DIST_PLATFORM=windows-x64 OS=Windows_NT CC=x86_64-w64-mingw32-gcc
cd dist && sha256sum -c SHA256SUMS
```

## Registry

Run a local file-backed registry:

```sh
python3 tools/registry/server.py --root registry --host 127.0.0.1 --port 7788 --token dev-token
```

Storage layout:

```txt
registry/
  packages/
    smoke/
      0.1.0/
        smoke-0.1.0.tar.gz
```

Registry API:

```txt
GET /health
GET /packages/<name>
GET /packages/<name>/<version>
GET /packages/<name>/latest
GET /tarballs/<name>/<version>/<archive>
GET /auth/check
PUT /publish
```

## Requirements

For Ubuntu/Linux:

- `gcc`
- `make`
- `curl`
- `tar`
- `python3` for the local registry server
- `unzip` for `.zip` package installs

For Windows, use a C compiler such as MinGW-w64/MSYS2. The PowerShell installer
looks for `make`, `mingw32-make`, or `gmake`.

## Roadmap Memory

The active roadmap and progress are saved in `ROADMAP.md`.
