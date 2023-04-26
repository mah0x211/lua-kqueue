# lua-kqueue

[![test](https://github.com/mah0x211/lua-kqueue/actions/workflows/test.yml/badge.svg)](https://github.com/mah0x211/lua-kqueue/actions/workflows/test.yml)
[![codecov](https://codecov.io/gh/mah0x211/lua-kqueue/branch/master/graph/badge.svg)](https://codecov.io/gh/mah0x211/lua-kqueue)

kqueue bindings for lua


## Installation

```
luarocks install kqueue
```


## ok = kqueue.usable()

it returns `true` if the kqueue is usable.

**Returns**

- `ok:boolean`: `true` on if the kqueue is usable.


## kq, err = kqueue.new()

create a new kqueue instance.

**Returns**

- `kq:kqueue`: kqueue instance.
- `err:string`: error string.
- `errno:number`: error number.


## Metamethods of kqueue instance

### __len

return the number of registered events.

### __tostring

return the string representation of the kqueue instance.

### __gc

close the kqueue descriptor that holds by the kqueue instance.
free the event-list that holds by the kqueue instance.


## ok, err, errno = kq:add( filter, ident [, msec] [, udata] )

register a new event.

**Parameters**

- `filter:integer`: one of the following filter types.
    - `kqueue.EVFILT_READ`
    - `kqueue.EVFILT_WRITE`
    - `kqueue.EVFILT_SIGNAL`
    - `kqueue.EVFILT_TIMER`
- `ident:integer`: indentify the target of the event as follows.
    - `kqueue.EVFILT_READ`: file descriptor.
    - `kqueue.EVFILT_WRITE`: file descriptor.
    - `kqueue.EVFILT_SIGNAL`: signal number.
    - `kqueue.EVFILT_TIMER`: an arbitrary number.
- `msec:number`: timeout in milliseconds if the filter type is `kqueue.EVFILT_TIMER`.
- `udata:any`: user data.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local kqueue = require('kqueue')
local kq = assert(kqueue.new())
-- register a new event for the file descriptor 0 (stdin)
assert(kq:add(kqueue.EVFILT_READ, 0, nil, 'hello'))
```


## ok, err, errno = kq:add_edge( filter, ident [, msec] [, udata] )

register a new event with edge trigger.

**NOTE**: edge trigger is a trigger that is activated only when the state of the target changes from inactive to active.


**Parameters**

same as `kq:add()`.

**Returns**

same as `kq:add()`.


## ok, err, errno = kq:add_oneshot( filter, ident [, msec] [, udata] )

register a new event with one-shot trigger.

**NOTE**: one-shot trigger is a trigger that is automatically removed after the event is activated.

**Parameters**

same as `kq:add()`.

**Returns**

same as `kq:add()`.


## ok, err, errno = kq:del( filter, ident )

unregister the event that is registered with the specified filter type and ident.

**Parameters**

same as `kq:add()`.

**Returns**

same as `kq:add()`.


## n, err, errno = kq:wait( [msec] )

wait for events. it consumes all remaining events before waiting for new events.

**Parameters**

- `msec:number`: timeout in milliseconds. if the value is `nil` or `<=0` then it waits forever.

**Returns**

- `n:number`: the number of events.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local kqueue = require('kqueue')
local kq = assert(kqueue.new())
-- register a new event for the file descriptor 0 (stdin)
assert(kq:add(kqueue.EVFILT_READ, 0, nil, 'hello'))
-- wait for events
local n, err, errno = kq:wait()
if err then
    print(err, errno)
else
    print('n:', n)
end
```


## ev, err, errno = kq:consume()

consume the event.

**Returns**

- `ev:table`: event table.
    - `filter:integer`: filter type.
    - `ident:integer`: indentify the target of the event.
    - `flags:integer`: flags.
    - `fflags:integer`: filter flags.
    - `data:integer`: data.
    - `udata:any`: user data.
    - `edge:boolean`: `true` if the event is edge trigger.
    - `oneshot:boolean`: `true` if the event is one-shot trigger.
    - `eof:boolean`: `true` if the filter sets the `EV_EOF` flag.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local dump = require('dump')
local kqueue = require('kqueue')
local kq = assert(kqueue.new())
-- register a new event for the file descriptor 0 (stdin)
assert(kq:add(kqueue.EVFILT_READ, 0, nil, 'hello'))
-- wait for events
local n, err, errno = kq:wait()
if err then
    print(err, errno)
else
    print('n:', n)
    -- consume the event
    local ev
    ev, err, errno = kq:consume()
    if err then
        print(err, errno)
    else
        print(dump(ev))
    end
end
```
