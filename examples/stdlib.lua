local lia = require("lia")
local fs = require("lia.fs")
local path = require("lia.path")
local process = require("lia.process")

local dir = ".lia-stdlib-smoke"
local file = path.join(dir, "message.txt")

assert(fs.mkdir(dir))
assert(fs.write(file, "hello stdlib"))
assert(fs.exists(file))
assert(fs.is_dir(dir))
assert(fs.read(file) == "hello stdlib")
assert(process.cwd() == lia.cwd())
assert(process.platform() == lia.platform)

print("stdlib=ok")
