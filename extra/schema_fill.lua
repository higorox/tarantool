-- Super User ID
GUID = 0
SUID = 1
_schema = box.space[box.schema.SCHEMA_ID]
_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
_func = box.space[box.schema.FUNC_ID]
_user = box.space[box.schema.USER_ID]
_priv = box.space[box.schema.PRIV_ID]
-- define schema version
_schema:insert{'version', 1, 6}
-- define system spaces
_space:insert{_schema.n, SUID, 0, '_schema'}
_space:insert{_space.n, SUID, 0, '_space'}
_space:insert{_index.n, SUID, 0, '_index'}
_space:insert{_func.n, SUID, 0, '_func'}
_space:insert{_user.n, SUID, 0, '_user'}
_space:insert{_priv.n, SUID, 0, '_priv'}
-- define indexes
_index:insert{_schema.n, 0, 'primary', 'tree', 1, 1, 0, 'str'}

-- space name is unique
_index:insert{_space.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_space.n, 1, 'name', 'tree', 1, 1, 2, 'str'}

-- index name is unique within a space
_index:insert{_index.n, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num'}
_index:insert{_index.n, 1, 'name', 'tree', 1, 2, 0, 'num', 2, 'str'}
-- user name and id are unique
_index:insert{_user.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_user.n, 1, 'name', 'tree', 1, 1, 2, 'str'}
-- function name and id are unique
_index:insert{_func.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_func.n, 1, 'name', 'tree', 1, 1, 2, 'str'}
--
-- user id, object id, object type unique
_index:insert{_priv.n, 0, 'primary', 'tree', 1, 3, 0, 'num', 1, 'num', 2, 'str'}

-- 
-- Pre-create user and grants
_user:insert{GUID, 'guest'}
_user:insert{SUID, 'admin'}
