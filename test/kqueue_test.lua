local testcase = require('testcase')
local kqueue = require('kqueue')
local fileno = require('io.fileno')
local pipe = require('os.pipe.io')

if not kqueue.usable() then
    function testcase.usable()
        -- test that return true on supported platform
        assert.is_false(kqueue.usable())
    end

    function testcase.new()
        -- test that create a new kqueue
        local err = assert.throws(kqueue.new)
        assert.match(err, 'kqueue is not supported on this platform')
    end

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

function testcase.usable()
    -- test that return true on supported platform
    assert.is_true(kqueue.usable())
end

function testcase.new()
    -- test that create a new kqueue
    local kq = assert(kqueue.new())
    assert.match(kq, '^kqueue: ', false)
end

function testcase.renew()
    local kq = assert(kqueue.new())
    local ev1 = kq:new_event()
    assert(ev1:as_oneshot())
    assert(ev1:as_write(TMPFD))

    local nevt = assert(kq:wait())
    assert.equal(nevt, 1)
    assert.is_true(ev1:is_enabled())

    -- test that renew a kqueue file descriptor and unconsumed events will be disabled
    assert(kq:renew())
    assert.is_false(ev1:is_enabled())
    assert.is_nil(kq:consume())

end

function testcase.new_event()
    local kq = assert(kqueue.new())

    -- test that create a new event
    local ev = kq:new_event()
    assert.match(ev, '^kqueue%.event: ', false)
end

function testcase.len()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()

    -- test that return number of registered events
    assert.equal(#kq, 0)
    assert(ev:as_read(TMPFD))
    assert.equal(#kq, 1)
    ev:unwatch()
    assert.equal(#kq, 0)
end

function testcase.wait()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    local ctx = {
        'context',
    }
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')

    -- test that return 0 if no events registered
    local nevt = assert(kq:wait())
    assert.equal(nevt, 0)

    -- test that return 1
    assert(ev:as_read(TMPFD, ctx))
    nevt = assert(kq:wait())
    assert.equal(nevt, 1)

    -- test that return 0 if timeout
    assert(TMPFILE:seek('end'))
    nevt = assert(kq:wait(0.01))
    assert.equal(nevt, 0)

    -- test that event occurred repeatedly as default
    assert(TMPFILE:seek('set'))
    nevt = assert(kq:wait())
    assert.equal(nevt, 1)
end

function testcase.unconsumed_events_will_be_consumed_in_wait()
    local kq = assert(kqueue.new())
    local p = assert(pipe())
    local ev1 = kq:new_event()
    local ev2 = kq:new_event()
    assert(p:write('test'))

    assert(ev1:as_read(p.reader:fd()))
    assert(ev2:as_oneshot())
    assert(ev2:as_write(TMPFD))
    p:closewr()

    local nevt = assert(kq:wait())
    assert.equal(nevt, 2)
    assert.is_true(ev1:is_enabled())
    assert.is_true(ev2:is_enabled())

    -- test that wait() will consume unconsumed events
    nevt = assert(kq:wait())
    assert.equal(nevt, 0)
    assert.is_false(ev1:is_enabled())
    assert.is_false(ev2:is_enabled())
end

function testcase.consume()
    local kq = assert(kqueue.new())
    local ev = kq:new_event()
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')
    assert(ev:as_read(TMPFD, {
        'context',
    }))

    -- test that return number of occurred events
    assert.equal(assert(kq:wait()), 1)
    local oev, ctx, disabled, eof = assert(kq:consume())
    assert.equal(oev, ev)
    assert.equal(ctx, {
        'context',
    })
    assert.is_nil(disabled)
    assert.is_nil(eof)

    -- test that return nil if consumed all events
    oev = kq:consume()
    assert.is_nil(oev)
end

function testcase.eof_event_will_be_disabled_in_consume()
    local kq = assert(kqueue.new())
    local p = assert(pipe())
    assert(p:write('test'))
    local ev = kq:new_event()
    assert(ev:as_read(p.reader:fd(), {
        'context',
    }))

    -- test that return number of occurred events
    p:closewr()
    assert.equal(assert(kq:wait()), 1)
    local oev, ctx, disabled, eof = assert(kq:consume())
    assert.equal(oev, ev)
    assert.equal(ctx, {
        'context',
    })
    assert.is_true(disabled)
    assert.is_true(eof)
    assert.equal(#kq, 0)
    assert.equal(ev:getinfo('occurred'), {
        ident = p.reader:fd(),
        flags = 0,
        fflags = 0,
        data = 4,
        eof = true,
        udata = {
            'context',
        },
    })

    -- test that return nil if consumed all events
    ev = kq:consume()
    assert.is_nil(ev)
end

function testcase.oneshot_event_will_be_disabled_in_consume()
    local kq = assert(kqueue.new())
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')
    local ev = kq:new_event()
    ev:as_oneshot()
    assert(ev:as_read(TMPFD, {
        'context',
    }))

    -- test that oneshot-trigger event
    assert.equal(assert(kq:wait()), 1)
    local oev, ctx, disabled, eof = assert(kq:consume())
    assert.equal(oev, ev)
    assert.equal(ctx, {
        'context',
    })
    assert.is_true(disabled)
    assert.is_nil(eof)
    assert.equal(#kq, 0)
    assert.equal(ev:getinfo('occurred'), {
        ident = TMPFD,
        flags = 0,
        fflags = 0,
        data = 4,
        oneshot = true,
        udata = {
            'context',
        },
    })

    -- test that onshot-event will be deleted after event occurred
    assert.equal(#kq, 0)
    TMPFILE:seek('set', 1)
    assert.equal(assert(kq:wait(10)), 0)
end

function testcase.edge_triggered_event_will_not_repeat()
    local kq = assert(kqueue.new())
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')
    local ev = kq:new_event()
    ev:as_edge()
    assert(ev:as_read(TMPFD, {
        'context',
    }))

    -- test that edge-trigger event
    assert.equal(assert(kq:wait()), 1)
    local oev, ctx, disabled, eof = assert(kq:consume())
    assert.equal(oev, ev)
    assert.equal(ctx, {
        'context',
    })
    assert.is_nil(disabled)
    assert.is_nil(eof)
    assert.equal(ev:getinfo('occurred'), {
        ident = TMPFD,
        flags = 0,
        fflags = 0,
        data = 4,
        edge = true,
        udata = {
            'context',
        },
    })
    assert.equal(#kq, 1)

    -- test that event does not occur repeatedly
    assert.equal(assert(kq:wait(0.01)), 0)

    -- test that event occur if descriptor has changed
    TMPFILE:seek('set', 1)
    assert.equal(assert(kq:wait()), 1)
end

