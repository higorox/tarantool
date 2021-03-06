--# create server hot_standby with configuration='replication/cfg/hot_standby.cfg' with hot_master=default
--# create server replica with configuration='replication/cfg/replica.cfg' with rpl_master=default
--# start server hot_standby
--# start server replica

--# setopt delimiter ';'
--# set connection default, hot_standby, replica
do
    begin_lsn = box.info.lsn

    function _set_pri_lsn(_lsn)
        begin_lsn = _lsn
    end

    function _print_lsn()
        return (box.info.lsn - begin_lsn + 1)
    end

    function _insert(_begin, _end)
        local a = {}
        for i = _begin, _end do
            table.insert(a, box.space['tweedledum']:insert{i, 'the tuple '..i})
        end
        return unpack(a)
    end

    function _select(_begin, _end)
        local a = {}
        for i = _begin, _end do
            table.insert(a, box.space['tweedledum']:get{i})
        end
        return unpack(a)
    end

    function _wait_lsn(_lsnd)
        while box.info.lsn < _lsnd + begin_lsn do
            box.fiber.sleep(0.001)
        end
        begin_lsn = begin_lsn + _lsnd
    end
end;
--# setopt delimiter ''
--# set connection default

-- set begin lsn on master, replica and hot_standby.
--# set variable replica_port to 'replica.primary_port'
begin_lsn = box.info.lsn

a = box.net.box.new('127.0.0.1', replica_port)
a:call('_set_pri_lsn', box.info.lsn)
a:close()

space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })

_insert(1, 10)
_select(1, 10)

--# set connection replica
_wait_lsn(10)
_select(1, 10)

--# stop server default
box.fiber.sleep(0.2)

--# set variable hot_standby_port to 'hot_standby.primary_port'
a = box.net.box.new('127.0.0.1', hot_standby_port)
a:call('_set_pri_lsn', box.info.lsn)
a:close()

--# set connection hot_standby
_insert(11, 20)
_select(11, 20)

--# set connection replica
_wait_lsn(12)
_select(11, 20)

--# stop server hot_standby
--# stop server replica
--# cleanup server hot_standby
--# cleanup server replica
--# start server default
--# set connection default
box.space['tweedledum']:drop()
