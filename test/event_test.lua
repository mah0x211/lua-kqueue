local testcase = require('testcase')
local kqueue = require('kqueue')
local fileno = require('io.fileno')
local errno = require('errno')
local signal = require('signal')

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

    -- test that renew event with other kqueue
    assert(ev:renew(kq2))

    -- test that throws an error if invalid argument
    local err = assert.throws(function()
        ev:renew('invalid')
    end)
    assert.match(err, 'kqueue expected')
end

function testcase.as_level_is_level()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()

    -- test that remove the oneshot flags
    assert(ev:as_oneshot())
    assert.is_false(ev:is_level())
    assert.equal(ev:as_level(), ev)
    assert.is_true(ev:is_level())

    -- test that remove the edge flag
    assert(ev:as_edge())
    assert.is_false(ev:is_level())
    assert.equal(ev:as_level(), ev)
    assert.is_true(ev:is_level())
end

function testcase.as_edge_is_edge()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()

    -- test that set the edge flags
    assert.is_false(ev:is_edge())
    assert.equal(ev:as_edge(), ev)
    assert.is_true(ev:is_edge())
end

function testcase.as_oneshot_is_oneshot()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()

    -- test that set the oneshot flags
    assert.is_false(ev:is_oneshot())
    assert.equal(ev:as_oneshot(), ev)
    assert.is_true(ev:is_oneshot())
end

function testcase.as_read()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    local udata = {
        'context',
    }

    -- test that convert event to read event
    assert(ev:as_read(TMPFD, udata))
    assert.match(ev, '^kqueue%.read: ', false)
    assert.equal(ev:udata(), udata)
    assert.equal(#kq, 1)

    -- test that return an error if fd is already registered
    local ev2 = kq:new_event()
    local _, err, errnum = ev2:as_read(TMPFD, udata)
    assert.is_nil(_)
    assert.equal(err, errno.EEXIST.message)
    assert.equal(errnum, errno.EEXIST.code)
end

function testcase.as_write()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    local udata = {
        'context',
    }

    -- test that convert event to write event
    assert(ev:as_write(TMPFD, udata))
    assert.match(ev, '^kqueue%.write: ', false)
    assert.equal(ev:udata(), udata)
    assert.equal(#kq, 1)

    -- test that return an error if fd is already registered
    local ev2 = kq:new_event()
    local _, err, errnum = ev2:as_write(TMPFD, udata)
    assert.is_nil(_)
    assert.equal(err, errno.EEXIST.message)
    assert.equal(errnum, errno.EEXIST.code)
end

function testcase.as_signal()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    local udata = {
        'context',
    }

    -- test that convert event to signal event
    assert(ev:as_signal(signal.SIGINT, udata))
    assert.match(ev, '^kqueue%.signal: ', false)
    assert.equal(ev:udata(), udata)
    assert.equal(#kq, 1)

    -- test that return an error if signal is already registered
    local ev2 = kq:new_event()
    local _, err, errnum = ev2:as_signal(signal.SIGINT, udata)
    assert.is_nil(_)
    assert.equal(err, errno.EEXIST.message)
    assert.equal(errnum, errno.EEXIST.code)

    -- test that return error if unsupported signal number
    assert(ev:revert())
    _, err, errnum = ev:as_signal(1234567890, udata)
    assert.is_nil(_)
    assert.equal(err, errno.EINVAL.message)
    assert.equal(errnum, errno.EINVAL.code)

    -- test that throws an error if invalid signal
    err = assert.throws(function()
        ev:as_signal('invalid', udata)
    end)
    assert.match(err, 'number expected')
end

function testcase.as_timer()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    local udata = {
        'context',
    }

    -- test that convert event to timer event
    assert(ev:as_timer(123, 10, udata))
    assert.match(ev, '^kqueue%.timer: ', false)
    assert.equal(ev:udata(), udata)
    assert.equal(#kq, 1)

    -- test that return an error if ident is already registered
    local ev2 = kq:new_event()
    local _, err, errnum = ev2:as_timer(123, 10, udata)
    assert.is_nil(_)
    assert.equal(err, errno.EEXIST.message)
    assert.equal(errnum, errno.EEXIST.code)

    -- test that return error if invalid msec
    assert(ev:revert())
    _, err, errnum = ev:as_timer(123, -1, udata)
    assert.is_nil(_)
    assert.equal(err, errno.EINVAL.message)
    assert.equal(errnum, errno.EINVAL.code)

    -- test that throws an error if invalid timer
    err = assert.throws(function()
        ev:as_timer('invalid', udata)
    end)
    assert.match(err, 'number expected')
end
