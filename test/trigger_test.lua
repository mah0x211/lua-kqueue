local testcase = require('testcase')
local assert = require('assert')
local kqueue = require('kqueue')
local errno = require('errno')

if not kqueue.usable() then
    return
end

function testcase.type()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger())

    -- test that get the event type
    assert.equal(ev:type(), 'trigger')
end

function testcase.renew()
    local kq1 = assert(kqueue.new())
    local kq2 = assert(kqueue.new())
    local ev = kq1:new_event()
    assert(ev:as_trigger())

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
    assert(ev:as_trigger())
    assert.match(ev, '^kqueue%.trigger: ', false)

    -- test that revert event to initial state
    assert(ev:revert())
    assert.match(ev, '^kqueue%.event: ', false)
end

function testcase.watch_unwatch()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger())

    -- test that return false without error if event is already watched
    local ok, err, errnum = ev:watch()
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)

    -- test that return true if event is unwatched
    ok, err, errnum = ev:unwatch()
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)

    -- test that return false without error if event is already unwatched
    ok, err, errnum = ev:unwatch()
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)

    -- test that return true if event will be re-watched
    ok, err, errnum = ev:watch()
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(errnum)
end

function testcase.is_enabled()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger())

    -- test that return true if event is enabled
    assert.is_true(ev:is_enabled())

    -- test that return false if event is not enabled
    assert(ev:unwatch())
    assert.is_false(ev:is_enabled())
end

function testcase.as_level_is_level()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger())
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
    assert(ev:as_trigger())
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
    assert(ev:as_trigger())
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
    assert(ev:as_trigger())

    -- test that return ident (pipe read fd, a non-negative integer)
    local id = ev:ident()
    assert.is_int(id)
    assert(id >= 0)
end

function testcase.udata()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger(false, 'mydata'))

    -- test that set udata and return previous udata
    assert.equal(ev:udata(nil), 'mydata')

    -- test that return nil
    assert.is_nil(ev:udata())
end

function testcase.getinfo()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger())

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

function testcase.trigger_counter_mode()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger(false, 'ctx'))

    -- test that trigger fires kqueue event
    assert(ev:trigger())
    local nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    local oev, udata = assert(kq:consume())
    assert.equal(oev, ev)
    assert.equal(udata, 'ctx')

    -- test that event does not fire after consume
    nevt = assert(kq:wait(0))
    assert.equal(nevt, 0)
end

function testcase.trigger_counter_mode_multiple()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger())

    -- test that multiple triggers are coalesced into one event
    assert(ev:trigger())
    assert(ev:trigger())
    assert(ev:trigger())
    local nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    local oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- test that event does not fire after consume (all triggers consumed)
    nevt = assert(kq:wait(0))
    assert.equal(nevt, 0)
end

function testcase.trigger_semaphore_mode()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger(true))

    -- test that three triggers fire three separate consume events
    assert(ev:trigger())
    assert(ev:trigger())
    assert(ev:trigger())

    -- first consume
    local nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    local oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- second consume
    nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- third consume
    nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- no more events
    nevt = assert(kq:wait(0))
    assert.equal(nevt, 0)
end

function testcase.trigger_counter_mode_oneshot_rearm()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger(false))
    assert(ev:unwatch())
    assert(ev:as_oneshot())
    assert(ev:watch())

    -- fire the oneshot with multiple triggers
    assert(ev:trigger())
    assert(ev:trigger())
    assert(ev:trigger())
    local nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    local oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- test that re-watching does not immediately fire (pipe must be drained on consume)
    assert(ev:watch())
    nevt = assert(kq:wait(0))
    assert.equal(nevt, 0)

    -- test that a subsequent trigger fires correctly after re-watch
    assert(ev:trigger())
    nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    oev = assert(kq:consume())
    assert.equal(oev, ev)
end

function testcase.trigger_semaphore_mode_oneshot_rearm()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger(true))
    assert(ev:unwatch())
    assert(ev:as_oneshot())
    assert(ev:watch())

    -- fire the oneshot with three triggers (counter=3)
    assert(ev:trigger())
    assert(ev:trigger())
    assert(ev:trigger())
    local nevt = assert(kq:wait(0.1))
    assert.equal(nevt, 1)
    local oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- test that re-watching fires immediately because counter=2 remains (Linux-compatible)
    assert(ev:watch())
    nevt = assert(kq:wait(0))
    assert.equal(nevt, 1)
    oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- test that re-watching fires again because counter=1 remains
    assert(ev:watch())
    nevt = assert(kq:wait(0))
    assert.equal(nevt, 1)
    oev = assert(kq:consume())
    assert.equal(oev, ev)

    -- test that re-watching does not fire because counter=0 (pipe was drained)
    assert(ev:watch())
    nevt = assert(kq:wait(0))
    assert.equal(nevt, 0)
end

function testcase.trigger_returns_error_when_unwatched()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(ev:as_trigger())
    assert(ev:unwatch())

    -- test that trigger returns error if event is not enabled
    local ok, err, errnum = ev:trigger()
    assert.is_false(ok)
    assert.equal(err, errno.EINPROGRESS.message)
    assert.equal(errnum, errno.EINPROGRESS.code)
end
