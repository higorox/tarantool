-- user id for a Lua session is admin - 1
box.session.uid()
-- extra arguments are ignored
box.session.uid(nil)
-- admin
box.session.user()
-- extra argumentes are ignored
box.session.user(nil)
-- password() is a function which returns base64(sha1(sha1(password))
-- a string to store in _user table
box.schema.user.password('test')
box.schema.user.password('test1')
-- admin can create any user
box.schema.user.create('test', { password = 'test' })
-- su() let's you change the user of the session
-- the user will be unabe to change back unless he/she
-- is granted access to 'su'
box.session.su('test')
-- you can't create spaces unless you have a write access on
-- system space _space
-- in future we may  introduce a separate privilege
box.schema.create_space('test')
