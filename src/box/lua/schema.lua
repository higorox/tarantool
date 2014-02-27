-- schema.lua (internal file)
--
local ffi = require('ffi')
ffi.cdef[[
    struct space *space_by_id(uint32_t id);
    void space_run_triggers(struct space *space, bool yesno);
    void password_prepare(const char *password, int len,
		                  char *out, int out_len);
]]

local function user_resolve(user)
    local _user = box.space[box.schema.USER_ID]
    local tuple
    if type(user) == 'string' then
        tuple = _user.index['name']:select{user}
    else
        tuple = _user.index[primary]:select{user}
    end
    if tuple == nil then
        return nil
    end
    return tuple[0]
end

box.schema.space = {}
box.schema.space.create = function(name, options)
    local _space = box.space[box.schema.SPACE_ID]
    if options == nil then
        options = {}
    end
    local if_not_exists = options.if_not_exists

    local temporary = options.temporary and "temporary" or ""

    if box.space[name] then
        if options.if_not_exists then
            return box.space[name], "not created"
        else
            box.raise(box.error.ER_SPACE_EXISTS,
                     "Space '"..name.."' already exists")
        end
    end
    local id
    if options.id then
        id = options.id
    else
        id = _space.index[0]:max()[0]
        if id < box.schema.SYSTEM_ID_MAX then
            id = box.schema.SYSTEM_ID_MAX + 1
        else
            id = id + 1
        end
    end
    if options.arity == nil then
        options.arity = 0
    end
    local uid = nil
    if options.user then
        uid = user_resolve(options.user)
    end
    if uid == nil then
        uid = box.session.uid()
    end
    _space:insert{id, uid, name, options.arity, temporary}
    return box.space[id], "created"
end
box.schema.create_space = box.schema.space.create
box.schema.space.drop = function(space_id)
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local keys = { _index:select{space_id} }
    for i = #keys, 1, -1 do
        local v = keys[i]
        _index:delete{v[0], v[1]}
    end
    if _space:delete{space_id} == nil then
        box.raise(box.error.ER_NO_SUCH_SPACE,
                  "Space "..space_id.." does not exist")
    end
end
box.schema.space.rename = function(space_id, space_name)
    local _space = box.space[box.schema.SPACE_ID]
    _space:update(space_id, {{"=", 2, space_name}})
end

box.schema.index = {}

box.schema.index.create = function(space_id, name, options)
    local _index = box.space[box.schema.INDEX_ID]
    if options == nil then
        options = {}
    end
    if options.type == nil then
        options.type = "tree"
    end
    if options.parts == nil then
        options.parts = { 0, "num" }
    end
    if options.unique == nil then
        options.unique = true
    end
    local index_type = options.type
    local unique = options.unique and 1 or 0
    local part_count = bit.rshift(#options.parts, 1)
    local parts = options.parts
    local iid = 0
    -- max
    local tuple = _index.index[0]
        :eselect(space_id, { limit = 1, iterator = 'LE' })
    tuple = tuple[1]
    if tuple then
        local id = tuple[0]
        if id == space_id then
            iid = tuple[1] + 1
        end
    end
    if options.id then
        iid = options.id
    end
    _index:insert{space_id, iid, name, index_type, unique, part_count, unpack(parts)}
end
box.schema.index.drop = function(space_id, index_id)
    local _index = box.space[box.schema.INDEX_ID]
    _index:delete{space_id, index_id}
end
box.schema.index.rename = function(space_id, index_id, name)
    local _index = box.space[box.schema.INDEX_ID]
    _index:update({space_id, index_id}, {{"=", 2, name}})
end
box.schema.index.alter = function(space_id, index_id, options)
    if space_id == nil or index_id == nil then
        box.raise(box.error.ER_PROC_LUA, "Usage: index:alter{opts}")
    end
    if box.space[space_id] == nil then
        box.raise(box.error.ER_NO_SUCH_SPACE,
                  "Space "..space_id.." does not exist")
    end
    if box.space[space_id].index[index_id] == nil then
        box.raise(box.error.ER_NO_SUCH_INDEX,
                  "Index "..index_id.." not found in space"..space_id)
    end
    if options == nil then
        return
    end
    if type(space_id) == "string" then
        space_id = box.space[space_id].n
    end
    if type(index_id) == "string" then
        index_id = box.space[space_id].index[index_id].id
    end
    local _index = box.space[box.schema.INDEX_ID]
    if options.unique ~= nil then
        options.unique = options.unique and 1 or 0
    end
    if options.id ~= nil then
        if options.parts ~= nil then
            box.raise(box.error.ER_PROC_LUA,
                      "Don't know how to update both id and parts")
        end
        ops = {}
        local function add_op(value, field_no)
            if value then
                table.insert(ops, {'=', field_no, value})
            end
        end
        add_op(options.id, 1)
        add_op(options.name, 2)
        add_op(options.type, 3)
        add_op(options.unique, 4)
        _index:update({space_id, index_id}, ops)
        return
    end
    local tuple = _index:select{space_id, index_id}
    if options.name == nil then
        options.name = tuple[2]
    end
    if options.type == nil then
        options.type = tuple[3]
    end
    if options.unique == nil then
        options.unique = tuple[4]
    end
    if options.parts == nil then
        options.parts = {tuple:slice(6)} -- not part count
    end
    _index:replace{space_id, index_id, options.name, options.type,
                   options.unique, #options.parts/2, unpack(options.parts)}
end

local function keify(key)
    if key == nil then
        return {}
    end
    if type(key) == "table" then
        return key
    end
    return {key}
end

function box.schema.space.bless(space)
    local index_mt = {}
    -- __len and __index
    index_mt.len = function(index) return #index.idx end
    index_mt.__newindex = function(table, index)
        return error('Attempt to modify a read-only table') end
    index_mt.__index = index_mt
    -- min and max
    index_mt.min = function(index, key)
        if index.type == 'HASH' then
            box.raise(box.error.ER_UNSUPPORTED, 'HASH does not support min()')
        end
        local lst = index:eselect(keify(key), { iterator = 'GE', limit = 1 })
        if lst[1] ~= nil then
            return lst[1]
        else
            return
        end
    end
    index_mt.max = function(index, key)
        if index.type == 'HASH' then
            box.raise(box.error.ER_UNSUPPORTED, 'HASH does not support max()')
        end
        local lst = index:eselect(keify(key), { iterator = 'LE', limit = 1 })
        if lst[1] ~= nil then
            return lst[1]
        else
            return
        end
    end
    index_mt.random = function(index, rnd) return index.idx:random(rnd) end
    -- iteration
    index_mt.iterator = function(index, key, opts)
        if opts == nil then
            opts = {}
        elseif type(opts) ~= 'table' then
            error("usage: index:iterator(key[, { option = value, ... })")
        end

        if type(opts.iterator) == 'string' then
            if box.index[ opts.iterator ] == nil then
                box.raise(box.error.ER_UNKNOWN_ITERATOR_TYPE,
                         "Unknown iterator type '"..opts.iterator.."'")
            end
            opts.iterator = box.index[ opts.iterator ]
        end

        return index.idx:iterator(key, opts)
    end
    --
    -- pairs
    index_mt.pairs = function(index)
        return index.idx.next, index.idx, nil
    end
    -- index subtree size
    index_mt.count = function(index, key, opts)
        local count = 0
        local iterator

        if opts and opts.iterator ~= nil then
            iterator = opts.iterator
        else
            iterator = 'EQ'
        end

        key = keify(key)

        if #key == 0 then
            return #index.idx
        end

        for tuple in index:iterator(key, { iterator = iterator }) do
            count = count + 1
        end
        return count
    end

    local function check_index(space, index_id)
        if space.index[index_id] == nil then
            box.raise(box.error.ER_NO_SUCH_INDEX,
                string.format("No index #%d is defined in space %d", index_id,
                    space.n))
        end
    end

    -- eselect
    index_mt.eselect = function(index, key, opts)
        -- user can catch link to index
        check_index(box.space[index.n], index.id)

        if opts == nil then
            opts = {}
        end

        local iterator = opts.iterator

        if iterator == nil then
            iterator = box.index.EQ
        end
        if type(iterator) == 'string' then
            if box.index[ iterator ] == nil then
                error(string.format("Wrong iterator: %s", tostring(iterator)))
            end
            iterator = box.index[ iterator ]
        end

        local result = {}
        local offset = 0
        local skip = 0
        local count = 0
        if opts.offset ~= nil then
            offset = tonumber(opts.offset)
        end
        local limit = opts.limit
        local grep = opts.grep
        local map = opts.map

        if limit == 0 then
            return result
        end

        for tuple in index:iterator(keify(key), { iterator = iterator }) do
            if grep == nil or grep(tuple) then
                if skip < offset then
                    skip = skip + 1
                else
                    if map == nil then
                        table.insert(result, tuple)
                    else
                        table.insert(result, map(tuple))
                    end
                    count = count + 1

                    if limit == nil then
                        if count > 1 then
                            box.raise(box.error.ER_MORE_THAN_ONE_TUPLE,
                                "More than one tuple found without 'limit'")
                        end
                    elseif count >= limit then
                        break
                    end
                end
            end
        end
        if limit == nil then
            return unpack(result)
        end
        return result
    end

    --
    index_mt.select = function(index, key)
        return box._select(index.n, index.id, keify(key))
    end
    index_mt.update = function(index, key, ops)
        return box._update(index.n, index.id, keify(key), ops);
    end
    index_mt.delete = function(index, key)
        return box._delete(index.n, index.id, keify(key));
    end
    index_mt.drop = function(index)
        return box.schema.index.drop(index.n, index.id)
    end
    index_mt.rename = function(index, name)
        return box.schema.index.rename(index.n, index.id, name)
    end
    index_mt.alter= function(index, options)
        return box.schema.index.alter(index.n, index.id, options)
    end
    --
    local space_mt = {}
    space_mt.len = function(space) return space.index[0]:len() end
    space_mt.__newindex = index_mt.__newindex

    space_mt.eselect = function(space, key, opts)
        check_index(space, 0)
        return space.index[0]:eselect(key, opts)
    end

    space_mt.select = function(space, key)
        check_index(space, 0)
        return space.index[0]:select(key)
    end
    space_mt.insert = function(space, tuple)
        return box._insert(space.n, tuple);
    end
    space_mt.replace = function(space, tuple)
        return box._replace(space.n, tuple);
    end
    space_mt.update = function(space, key, ops)
        check_index(space, 0)
        return space.index[0]:update(key, ops)
    end
    space_mt.delete = function(space, key)
        check_index(space, 0)
        return space.index[0]:delete(key)
    end
-- Assumes that spaceno has a TREE (NUM) primary key
-- inserts a tuple after getting the next value of the
-- primary key and returns it back to the user
    space_mt.auto_increment = function(space, tuple)
        local max_tuple = space.index[0]:max()
        local max = 0
        if max_tuple ~= nil then
            max = max_tuple[0]
        end
        table.insert(tuple, 1, max + 1)
        return space:insert(tuple)
    end

    space_mt.truncate = function(space)
        check_index(space, 0)
        local pk = space.index[0]
        while #pk.idx > 0 do
            for t in pk:iterator() do
                local key = {}
                -- ipairs does not work because pk.key_field is zero-indexed
                for _k2, key_field in pairs(pk.key_field) do
                    table.insert(key, t[key_field.fieldno])
                end
                space:delete(key)
            end
        end
    end
    space_mt.pairs = function(space) return space.index[0]:pairs() end
    space_mt.drop = function(space)
        return box.schema.space.drop(space.n)
    end
    space_mt.rename = function(space, name)
        return box.schema.space.rename(space.n, name)
    end
    space_mt.create_index = function(space, name, options)
        return box.schema.index.create(space.n, name, options)
    end
    space_mt.run_triggers = function(space, yesno)
        local space = ffi.C.space_by_id(space.n)
        if space == nil then
            box.raise(box.error.ER_NO_SUCH_SPACE, "Space not found")
        end
        ffi.C.space_run_triggers(space, yesno)
    end
    space_mt.__index = space_mt

    setmetatable(space, space_mt)
    if type(space.index) == 'table' and space.enabled then
        for j, index in pairs(space.index) do
            if type(j) == 'number' then
                rawset(index, 'idx', box.index.bind(space.n, j))
                setmetatable(index, index_mt)
            end
        end
    end
end

box.schema.user = {}

local function privilege_resolve(privilege)
    local numeric = 0
    if type(privilege) == 'string' then
        if string.find(privilege, 'read') then
            numeric = numeric + 1
        end
        if string.find(privilege, 'write') then
            numeric = numeric + 2
        end
        if string.find(privilege, 'execute') then
            numeric = numeric + 4
        end
    else
        numeric = privilege
    end
    return numeric
end

box.schema.user.password = function(password)
    local BUF_SIZE = 128
    local buf = ffi.new("char[?]", BUF_SIZE)
    ffi.C.password_prepare(password, #password, buf, BUF_SIZE)
    return ffi.string(buf)
end

box.schema.user.passwd = function(new_password)
    local uid = box.session.uid()
    local _user = box.space[box.schema.USER_ID]
    _user:update({uid}, { "=", 3, box.schema.user.password(new_password) })
end

box.schema.user.create = function(name, opts)
    local user = user_resolve(name)
    if user then
        box.raise(box.error.ER_USER_EXISTS,
                  "User '"..user.."' already exists")
    end
    if opts == nil then
        opts = {}
    end
    if opts.password then
        opts.password = box.schema.user.password(opts.password)
    else
        opts.password = ""
    end
    local _user = box.space[box.schema.USER_ID]
    _user:auto_increment{'', name, opts.password}
end

box.schema.user.drop = function(name)
    local _user = box.space[box.schema.USER_ID]
    local uid = user_resolve(user)
    if uid == nil then
        box.raise(box.error.ER_NO_SUCH_USER, "User "..name.."not found")
    end
    -- todo recursive delete of user data
    _user:delete{uid}
end

box.schema.user.grant = function(user_name, privilege, object_type, object_name, grantor)
    local uid = user_resolve(user_name)
    if uid == nil then
        box.raise(box.error.ER_NO_SUCH_USER,
                  "User '"..user_name.."' does not exists")
    end
    privilege = privilege_resolve(privilege)
    local oid = object_resolve(object_type, object_name)
    if grantor == nil then
        grantor = box.session.uid()
    end
    local _priv = box.space[box.schema.PRIV_ID]
    _priv:replace{uid, object_type, object, grantor, privilege}
end

box.schema.user.revoke = function(user_name, privilege, object_type, object_name)
    local uid = user_resolve(name)
    if uid == nil then
        box.raise(box.error.ER_NO_SUCH_USER,
                  "User '"..name.."' does not exists")
    end
    privilege = privilege_resolve(privilege)
    local oid = object_resolve(object_type, object)
    local _priv = box.space[box.schema.PRIV_ID]
    local tuple = _priv:select{uid, object_type, oid}
    if tuple == nil then
        return
    end
    local old_privilege = tuple[4]
    if old_privilege ~= privilege then
        privilege = bit.band(old_privilege, bit.bnot(privilege))
        _priv:update({uid, object_type, oid}, { "=", 4, privilege})
    else
        _priv:delete{uid, object_type, oid}
    end
end
