local lia = require("lia")

assert(_LIA == lia, "require('lia') should return the _LIA runtime table")
assert(lia.name == "Lia", "runtime name should be Lia")
assert(type(lia.version) == "string", "runtime version should be available")
assert(type(lia.lua_release) == "string", "Lua release should be available")
assert(type(lia.cwd()) == "string", "cwd() should return a path")

print("runtime=" .. lia.name .. " " .. lia.version)
print("lua=" .. lia.lua_release)
print("platform=" .. lia.platform)
print("arg1=" .. tostring(lia.args[1]))
