-- init.lua (internal file)

local fun = require('fun')

-- export some functions to the global namespace
_G.iter    = fun.iter
_G.range   = fun.range
_G.map     = fun.map
_G.filter  = fun.filter
_G.reduce  = fun.reduce
_G.foreach = fun.foreach
