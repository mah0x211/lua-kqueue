local testcase = require('testcase')
local kqueue = require('kqueue')
local fileno = require('io.fileno')
local errno = require('errno')

if not kqueue.usable() then
    return
end

local TMPFILE
local TMPFD

function testcase.before_each()
    if TMPFILE then
        TMPFILE:close()
        TMPFILE = nil
        TMPFD = nil
    end

    TMPFILE = assert(io.tmpfile())
    TMPFD = fileno(TMPFILE)
end

function testcase.renew()
    local kq1 = assert(kqueue.new())
    local kq2 = assert(kqueue.new())
    local ev = kq1:new_event()
    assert(ev:as_write(TMPFD))

    -- test that renew event with other kqueue
    assert(ev:renew(kq2))

    -- test that throws an error if invalid argument
    local err = assert.throws(function()
        ev:renew('invalid')
    end)
    assert.match(err, 'kqueue expected')
end

function testcase.revert()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))
    assert.match(ev, '^kqueue%.write: ', false)

    -- test that revert event to initial state
    assert(ev:revert())
    assert.match(ev, '^kqueue%.event: ', false)
end

function testcase.watch_unwatch()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))

    -- test that return false without error if event is already watched
    local ok, err, errnum = ev:watch()
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)

    -- test that event occurs when fd is writable
    local nevt = assert(kq:wait())
    assert.equal(nevt, 1)
    local oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- test that return true if event is watched
    ok, err, errnum = ev:unwatch()
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)

    -- test that return false without error if event is already unwatched
    ok, err, errnum = ev:unwatch()
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)

    -- test that return true if event will be watched
    ok, err, errnum = ev:watch()
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)
end

function testcase.is_enabled()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))

    -- test that return true if event is enabled
    assert.is_true(ev:is_enabled())

    -- test that return false if event is not enabled
    assert(ev:unwatch())
    assert.is_false(ev:is_enabled())
end

function testcase.as_level_is_level()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))
    ev:unwatch()

    -- test that remove the oneshot flags
    assert(ev:as_oneshot())
    assert.is_false(ev:is_level())
    assert(ev:as_level())
    assert.is_true(ev:is_level())

    -- test that remove the edge flag
    assert(ev:as_edge())
    assert.is_false(ev:is_level())
    assert(ev:as_level())
    assert.is_true(ev:is_level())

    -- test that return error if event is in-progress
    assert(ev:watch())
    local err, errnum
    ev, err, errnum = ev:as_level()
    assert.is_nil(ev)
    assert.equal(err, errno.EINPROGRESS.message)
    assert.equal(errnum, errno.EINPROGRESS.code)
end

function testcase.as_edge_is_edge()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))
    ev:unwatch()

    -- test that set the edge flags
    assert.is_false(ev:is_edge())
    assert(ev:as_edge())
    assert.is_true(ev:is_edge())

    -- test that return error if event is in-progress
    assert(ev:watch())
    local err, errnum
    ev, err, errnum = ev:as_edge()
    assert.is_nil(ev)
    assert.equal(err, errno.EINPROGRESS.message)
    assert.equal(errnum, errno.EINPROGRESS.code)
end

function testcase.as_oneshot_is_oneshot()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))
    ev:unwatch()

    -- test that set the oneshot flags
    assert.is_false(ev:is_oneshot())
    assert(ev:as_oneshot())
    assert.is_true(ev:is_oneshot())

    -- test that return error if event is in-progress
    assert(ev:watch())
    local err, errnum
    ev, err, errnum = ev:as_oneshot()
    assert.is_nil(ev)
    assert.equal(err, errno.EINPROGRESS.message)
    assert.equal(errnum, errno.EINPROGRESS.code)
end

function testcase.ident()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))

    -- test that return ident
    assert.equal(ev:ident(), TMPFD)
end

function testcase.udata()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD, 'test'))

    -- test that set udata and return previous udata
    assert.equal(ev:udata(nil), 'test')

    -- test that return nil
    assert.is_nil(ev:udata())
end

function testcase.getinfo()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_write(TMPFD))

    -- test that get info of registered event
    assert.is_table(ev:getinfo('registered'))

    -- test that get info of occurred event
    assert.is_table(ev:getinfo('occurred'))

    -- test that throws an error if invalid info name
    local err = assert.throws(function()
        ev:getinfo('invalid')
    end)
    assert.match(err, 'invalid option')
end

