local base = require("base")

local feature = {}

function feature.name()
  return "feature-package+" .. base.name()
end

return feature

