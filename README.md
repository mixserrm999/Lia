# Lia

`lia` is the first step toward a Lua developer platform with a package manager
experience similar to Node.js and npm.

## Current Status

Lia now has the MVP runtime/package-manager flow plus the Phase 2 foundation:
modular C sources, npm-like package commands, registry version resolution,
archive cache/offline restore, hardened publish checks, release archives, and a
small runtime standard library.

```sh
make build
make install PREFIX="$HOME/.local"
./build/lia examples/hello.lua
./build/lia examples/runtime.lua
./build/lia init
./build/lia check
./build/lia run start
./build/lia install smoke@0.1.0
./build/lia install 'smoke@^0.1.0'
./build/lia list
./build/lia info smoke
./build/lia outdated
./build/lia update smoke
./build/lia remove smoke
./build/lia login --token dev-token
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
./build/lia publish
./build/lia install
./build/lia list
./build/lia info package-name
./build/lia outdated
./build/lia update
./build/lia update package-name
./build/lia remove package-name
./build/lia install package-name
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

Ubuntu/Linux local user install:

```sh
./scripts/install.sh
```

Ubuntu/Linux system install:

```sh
sudo ./scripts/install.sh --system
```

Manual install with `make`:

```sh
make build
make install PREFIX="$HOME/.local"
```

Windows PowerShell install:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -AddToPath
```

Windows uninstall from the default prefix:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Uninstall
```

`lia init` creates a `lia.json` manifest in the current directory and refuses to
overwrite an existing manifest unless `--force` is passed.

`lia check` validates the project manifest. It requires `name`, `version`, and
`main` string fields, plus `scripts` and `dependencies` objects.

`lia run <script>` reads a command from `scripts` in `lia.json` and passes any
extra arguments to that command.

`lia install <source>` expects the archive to contain a package-level `lia.json`
at its root, or inside the archive's single top-level package directory. It
installs the package into `packages/<name>`, updates `lia.json`, and writes
`lia-lock.json`.

`lia install` without a source restores packages from `lia-lock.json`. The
lockfile records each package source, resolved version, install path, and
`sha256:` archive integrity. Restore verifies integrity before extracting an
archive.

Registry installs use `LIA_REGISTRY_URL`, defaulting to
`http://127.0.0.1:7788`:

```sh
LIA_REGISTRY_URL=http://127.0.0.1:7788 lia install smoke@0.1.0
LIA_REGISTRY_URL=http://127.0.0.1:7788 lia install 'smoke@^0.1.0'
LIA_REGISTRY_URL=http://127.0.0.1:7788 lia install smoke
```

`lia login` verifies a Bearer token against the registry and stores credentials
at `$HOME/.lia/credentials.json`. `lia publish` packages the current project,
excluding build/cache/package directories, and uploads it to the configured
registry. The local registry rejects duplicate published versions and records
package ownership by token.

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
Windows artifacts for tag pushes and manual runs. The release script is
`tools/make_dist.py`.

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
