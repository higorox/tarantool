--------------------------------------------------------------------------------
-- # luafun integration
--------------------------------------------------------------------------------

space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })
for i = 1,5,1 do space:replace({i, i}) end

fun = require('fun')

-- print all methods from metatable
methods = iter(getmetatable(range(5)).__index):totable()
table.sort(methods)
methods

-- test global functions
iter == fun.iter
range == fun.range
map == fun.map
filter == fun.filter
reduce == fun.reduce
foreach == fun.foreach

-- iter on arrays
iter({1, 2, 3}):totable()
iter({2, 4, 6, 8}):all(function(x) return x % 2 == 1 end)

-- iter on hashes
iter({a = 1, b = 2, c = 3}):tomap()

-- iter on tuple
iter(box.tuple.new({1, 2, 3}):pairs()):totable()

-- iter on space (using __ipairs)
function pred(t) return t[1] % 2 == 0 end
iter(space):totable()
iter(space:pairs()):totable()
space:pairs():filter(pred):drop(2):take(3):totable()

-- iter on index (using __ipairs)
iter(space.index[0]):totable()
iter(space.index[0]:pairs()):totable()
space.index[0]:pairs():drop(2):take(3):totable()

-- test global functions
--# setopt delimiter ';'
reduce(function(acc, val) return acc + val end, 0,
    filter(function(x) return x % 11 == 0 end,
    map(function(x) return 2 * x end, range(1000))));

--# setopt delimiter ''

t = {}
foreach(function(x) table.insert(t, x) end, "abcde")
t

space:drop()
