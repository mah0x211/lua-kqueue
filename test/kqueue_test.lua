local unpack = unpack or table.unpack
local testcase = require('testcase')
local kqueue = require('kqueue')
local fileno = require('io.fileno')
local errno = require('errno')
local signal = require('signal')
local pipe = require('pipe.io')

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

function testcase.add_and_del()
    local kq = assert(kqueue.new())

    -- test that add events
    for i, args in ipairs({
        {
            kqueue.EVFILT_READ,
            TMPFD,
        },
        {
            kqueue.EVFILT_WRITE,
            TMPFD,
        },
        {
            kqueue.EVFILT_SIGNAL,
            signal.SIGINT,
        },
        {
            kqueue.EVFILT_TIMER,
            123,
            10,
        },
    }) do
        local ok, err, errcode = kq:add(unpack(args))
        assert(ok, err)
        assert.is_nil(errcode)
        assert.equal(#kq, i)
    end

    -- test that del events
    local nreg = #kq
    for i, args in ipairs({
        {
            kqueue.EVFILT_READ,
            TMPFD,
        },
        {
            kqueue.EVFILT_WRITE,
            TMPFD,
        },
        {
            kqueue.EVFILT_SIGNAL,
            signal.SIGINT,
        },
        {
            kqueue.EVFILT_TIMER,
            123,
        },
    }) do
        local ok, err, errcode = kq:del(unpack(args))
        assert(ok, err)
        assert.is_nil(errcode)
        assert.equal(#kq, nreg - i)
    end

    -- test that return error if arguments is invalid
    for _, v in ipairs({
        {
            args = {
                kqueue.EVFILT_READ,
                -1,
            },
            res = {
                false,
                errno.EBADF.message,
                errno.EBADF.code,
            },
        },
        {
            args = {
                kqueue.EVFILT_WRITE,
                -1,
            },
            res = {
                false,
                errno.EBADF.message,
                errno.EBADF.code,
            },
        },
        {
            args = {
                kqueue.EVFILT_SIGNAL,
                1234567890,
            },
            res = {
                false,
                errno.EINVAL.message,
                errno.EINVAL.code,
            },
        },
        {
            args = {
                kqueue.EVFILT_TIMER,
                123,
                -1,
            },
            res = {
                false,
                errno.EINVAL.message,
                errno.EINVAL.code,
            },
        },
        {
            -- unsupported filter
            args = {
                123457890,
                123,
            },
            res = {
                false,
                errno.EINVAL.message,
                errno.EINVAL.code,
            },
        },
    }) do
        local ok, err, errcode = kq:add(unpack(v.args))
        assert.equal({
            ok,
            err,
            errcode,
        }, v.res)
        assert.equal(#kq, 0)
    end

    -- test that return error if unsupported filter
    for _, method in ipairs({
        kq.add,
        kq.del,
    }) do
        local ok, err, errcode = method(kq, 123457890, 123)
        assert.equal({
            ok,
            err,
            errcode,
        }, {
            false,
            errno.EINVAL.message,
            errno.EINVAL.code,
        })
        assert.equal(#kq, 0)
    end

end

function testcase.wait()
    local kq = assert(kqueue.new())
    local ctx = {
        'context',
    }
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')

    -- test that return 0 if no events registered
    local nevt = assert(kq:wait())
    assert.equal(nevt, 0)

    -- test that return 1
    assert(kq:add(kqueue.EVFILT_READ, TMPFD, ctx))
    nevt = assert(kq:wait())
    assert.equal(nevt, 1)

    -- test that return 0 if timeout
    assert(TMPFILE:seek('end'))
    nevt = assert(kq:wait(10))
    assert.equal(nevt, 0)

    -- test that event occurred repeatedly as default
    assert(TMPFILE:seek('set'))
    nevt = assert(kq:wait())
    assert.equal(nevt, 1)
end

function testcase.consume()
    local kq = assert(kqueue.new())
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')
    assert(kq:add(kqueue.EVFILT_READ, TMPFD, {
        'context',
    }))

    -- test that return number of occurred events
    assert.equal(assert(kq:wait()), 1)
    local ev = assert(kq:consume())
    assert.is_table(ev)
    assert.equal(ev, {
        ident = TMPFD,
        filter = kqueue.EVFILT_READ,
        flags = 0,
        fflags = 0,
        data = 4,
        udata = {
            'context',
        },
    })

    -- test that return nil if consumed all events
    ev = kq:consume()
    assert.is_nil(ev)
end

function testcase.eof_event()
    local kq = assert(kqueue.new())
    local p = assert(pipe())
    assert(p:write('test'))
    assert(kq:add(kqueue.EVFILT_READ, p.reader:fd(), {
        'context',
    }))

    -- test that return number of occurred events
    p:closewr()
    assert.equal(assert(kq:wait()), 1)
    local ev = assert(kq:consume())
    assert.is_table(ev)
    assert.equal(ev, {
        ident = p.reader:fd(),
        filter = kqueue.EVFILT_READ,
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

function testcase.add_edge()
    local kq = assert(kqueue.new())
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')
    assert(kq:add_edge(kqueue.EVFILT_READ, TMPFD, {
        'context',
    }))

    -- test that edge-trigger event
    assert.equal(assert(kq:wait()), 1)
    local ev = assert(kq:consume())
    assert.is_table(ev)
    assert.equal(ev, {
        ident = TMPFD,
        filter = kqueue.EVFILT_READ,
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
    assert.equal(assert(kq:wait(10)), 0)

    -- test that event occur if descriptor has changed
    TMPFILE:seek('set', 1)
    assert.equal(assert(kq:wait()), 1)
end

function testcase.add_oneshot()
    local kq = assert(kqueue.new())
    assert(TMPFILE:write('test'))
    TMPFILE:seek('set')
    assert(kq:add_oneshot(kqueue.EVFILT_READ, TMPFD, {
        'context',
    }))

    -- test that oneshot-trigger event
    assert.equal(assert(kq:wait()), 1)
    local ev = assert(kq:consume())
    assert.is_table(ev)
    assert.equal(ev, {
        ident = TMPFD,
        filter = kqueue.EVFILT_READ,
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

