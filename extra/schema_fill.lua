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
_space:insert{_schema.n, SUID, '_schema', 0}
_space:insert{_space.n, SUID, '_space', 0}
_space:insert{_index.n, SUID, '_index', 0}
_space:insert{_func.n, SUID, '_func', 0}
_space:insert{_user.n, SUID, '_user', 0}
_space:insert{_priv.n, SUID, '_priv', 0}
-- define indexes
_index:insert{_schema.n, 0, 'primary', 'tree', 1, 1, 0, 'str'}

-- stick to the following convention:
-- prefer user id (owner id) in field #1
-- prefer object name in field #2
-- index on owner id is index #1
-- index on object name is index #2
--
-- space name is unique
_index:insert{_space.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_space.n, 1, 'owner', 'tree', 0, 1, 1, 'num'}
_index:insert{_space.n, 2, 'name', 'tree', 1, 1, 2, 'str'}

-- index name is unique within a space
_index:insert{_index.n, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num'}
_index:insert{_index.n, 2, 'name', 'tree', 1, 2, 0, 'num', 2, 'str'}
-- user name and id are unique
_index:insert{_user.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_user.n, 2, 'name', 'tree', 1, 1, 2, 'str'}
-- function name and id are unique
_index:insert{_func.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_func.n, 1, 'owner', 'tree', 0, 1, 1, 'num'}
_index:insert{_func.n, 2, 'name', 'tree', 1, 1, 2, 'str'}
--
-- space schema is: grantor id, user id, object_id, object_type, privilege
-- primary key: user id, object id, object type
_index:insert{_priv.n, 0, 'primary', 'tree', 1, 3, 1, 'num', 2, 'num', 3, 'str'}
_index:insert{_priv.n, 1, 'owner', 'tree', 0, 1, 1, 'num'}

-- 
-- Pre-create user and grants
_user:insert{GUID, '', 'guest'}
_user:insert{SUID, '', 'admin'}
