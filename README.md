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


## kq, err, errno = kqueue.new()

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


## ok, err, errno = kq:renew()

disabled any events that have occurred and renew the file descriptor held by the kqueue instance.

**NOTE:** this method should be called after forking the process. additionally, you need to invoke the `renew` method of the event instance that was created by the `kqueue` instance.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ev = kq:new_event()

create a new `kqueue.event` instance.

**Returns**

- `ev:kqueue.event`: `kqueue.event` instance.


## n, err, errno = kq:wait( [sec] )

wait for events. it consumes all remaining events before waiting for new events.

**Parameters**

- `sec:number`: timeout in seconds. if the value is `nil` or `<0` then it waits forever.

**Returns**

- `n:number?`: the number of events, or `nil` if error occurred.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local kqueue = require('kqueue')
local kq = assert(kqueue.new())
-- register a new event for the file descriptor 0 (stdin)
local ev = assert(kq:new_event())
assert(ev:as_read(0))
-- wait until stdin is readable
local n, err, errno = kq:wait()
if err then
    print(err, errno)
    return
end
print('n:', n)
```


## ev, udata, errno = kq:consume()

consume the occurred event.

**NOTE:** if the `EV_EOF` flag is present in the event flags, the event is automatically unregisterd.

**Returns**

- `ev:kqueue.event?`: `kqueue.event` instance, or `nil` if error occurred.
- `udata:any`: udata will be treated as the following.
    - `any`: the event is occurred and the `udata` is set.
    - `nil`: the event is not occurred.
    - `string`: error message on error.
- `errno:number`: error number on error.

**Example**

```lua
local kqueue = require('kqueue')
local kq = assert(kqueue.new())
-- register a new event for the file descriptor 0 (stdin)
local ev = assert(kq:new_event())
assert(ev:as_read(0, 'hello'))
-- wait until stdin is readable
local n, err, errno = kq:wait()
if err then
    print('error:', err, errno)
    return
end

print('n:', n)
-- consume the event
local occurred, udata, errno = kq:consume()
if errno then
    print('error:', udata, errno)
elseif ocurred then
    print('event occurred:', ocurred, udata)
end
```


## `kqueue.event` instance

`kqueue.event` instance is used to register the following events.

- Read event: it watches the file descriptor until it becomes readable.
- Write event: it watches the file descriptor until it becomes writable.
- Signal event: it watches the signal until it becomes occurred.
- Timer event: it watches the timer until it becomes expired.


## ok, err, errno = ev:renew( [kq] )

rewatch the event. if the `kq` is specified then it rewatch the event with the specified kqueue instance.

**Parameters**

- `kq:kqueue`: `kqueue` instance.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ok = ev:is_level()

return `true` if the event trigger is level trigger.

**Returns**

- `ok:boolean`: `true` if the event trigger is level trigger.


## ok = ev:as_level()

change the event trigger to level trigger.

**NOTE:** the level trigger is a trigger that is activated while the state of the target is active.


**Returns**

- `ok:boolean`: `true` on success.


## ok = ev:is_edge()

return `true` if the event trigger is edge trigger.

**Returns**

- `ok:boolean`: `true` if the event trigger is edge trigger.


## ok = ev:as_edge()

change the event trigger to edge trigger.

**NOTE:** the edge trigger is a trigger that is activated only when the state of the target changes from inactive to active.

**Returns**

- `ok:boolean`: `true` on success.


## ok = ev:is_oneshot()

return `true` if the event type is one-shot event.

**Returns**

- `ok:boolean`: `true` if the event type is one-shot event.


## ok = ev:as_oneshot()

change the event type to one-shot event.

**NOTE:** the one-shot event is a event that is automatically removed after the event is activated.

**Returns**

- `ok:boolean`: `true` on success.


## ev, err, errno = ev:as_read( fd [, udata] )

register a event that watches the file descriptor until it becomes readable.

this method is change the meta-table of the `ev` to `kqueue.read`.

**Parameters**

- `fd:number`: file descriptor.
- `udata:any`: user data.

**Returns**

- `ev:kqueue.read?`: `kqueue.read` instance that is changed the meta-table of the `ev`, or `nil` if error occurred.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local kqueue = require('kqueue')
local kq = assert(kqueue.new())

-- register a new event for the file descriptor 0 (stdin)
local ev = assert(kq:new_event())
assert(ev:as_read(0, 'stdin is readable'))

-- wait until stdin is readable
local n, err, errno = kq:wait()
if err then
    print(err, errno)
    return
end
print('n:', n)

-- consume the event
while true do
    local occurred, udata, errno = kq:consume()
    if errno then
        print('error:', udata, errno)
    elseif not occurred then
        break
    end
    print('event occurred:', occurred, udata)
end
```


## ev, err, errno = ev:as_write( fd [, udata] )

register a event that watches the file descriptor until it becomes writable.

this method is change the meta-table of the `ev` to `kqueue.write`.

**Parameters**

- `fd:number`: file descriptor.
- `udata:any`: user data.

**Returns**

- `ev:kqueue.write?`: `kqueue.write` instance that is changed the meta-table of the `ev`, or `nil` if error occurred.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local kqueue = require('kqueue')
local kq = assert(kqueue.new())

-- register a new event for the file descriptor 1 (stdout)
local ev = assert(kq:new_event())
assert(ev:as_write(1, 'stdout is writable'))

-- wait until stdout is writable
local n, err, errno = kq:wait()
if err then
    print(err, errno)
    return
end
print('n:', n)

-- consume the event
while true do
    local occurred, udata, errno = kq:consume()
    if errno then
        print('error:', udata, errno)
    elseif not occurred then
        break
    end
    print('event occurred:', occurred, udata)
end
```

## ev, err, errno = ev:as_signal( signo [, udata] )

register a event that watches the signal until it becomes occurred.

this method is change the meta-table of the `ev` to `kqueue.signal`.

**Parameters**

- `signo:number`: signal number.
- `udata:any`: user data.

**Returns**

- `ev:kqueue.signal?`: `kqueue.signal` instance that is changed the meta-table of the `ev`, or `nil` if error occurred.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local signal = require('signal')
local kqueue = require('kqueue')
local kq = assert(kqueue.new())

-- register a new event for the signal SIGINT
local ev = assert(kq:new_event())
assert(ev:as_signal(signal.SIGINT, 'SIGINT occurred'))

-- wait until SIGINT is occurred
signal.block(signal.SIGINT)
local n, err, errno = kq:wait()
if err then
    print(err, errno)
    return
end
print('n:', n)

-- consume the event
while true do
    local occurred, udata, errno = kq:consume()
    if errno then
        print('error:', udata, errno)
    elseif not occurred then
        break
    end
    print('event occurred:', occurred, udata)
end
```

## ev, err, errno = ev:as_timer( ident, sec [, udata] )

register a event that watches the timer until it becomes expired.

this method is change the meta-table of the `ev` to `kqueue.timer`.

**Parameters**

- `ident:number`: timer identifier.
- `sec:number`: timer interval in seconds.
- `udata:any`: user data.

**Returns**

- `ev:kqueue.timer?`: `kqueue.timer` instance that is changed the meta-table of the `ev`, or `nil` if error occurred.
- `err:string`: error string.
- `errno:number`: error number.

**Example**

```lua
local kqueue = require('kqueue')
local kq = assert(kqueue.new())

-- register a new event for the timer
local ev = assert(kq:new_event())
assert(ev:as_timer(123, 0.150, 'timer expired after 150 milliseconds'))

-- wait until the timer is expired
local n, err, errno = kq:wait()
if err then
    print(err, errno)
    return
end
print('n:', n)

-- consume the event
while true do
    local occurred, udata, errno = kq:consume()
    if errno then
        print('error:', udata, errno)
    elseif not occurred then
        break
    end
    print('event occurred:', occurred, udata)
end
```


## Common Methods

the following methods are common methods of the `kqueue.read`, `kqueue.write`, `kqueue.signal` and `kqueue.timer` instances.


## t = ev:type()

return the event type.

**Returns**

- `t:string`: event type as follows.
  - `event`: type of the `epoll.event` instance.
  - `read`: type of the `epoll.read` instance.
  - `write`: type of the `epoll.write` instance.
  - `signal`: type of the `epoll.signal` instance.
  - `timer`: type of the `epoll.timer` instance.


## ok, err, errno = ev:renew( [kq] )

rewatch the event. if the `kq` is specified then it rewatch the event with the specified kqueue instance.

**Parameters**

- `kq:kqueue`: `kqueue` instance.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ev, err, errno = ev:revert()

revert the event to the `kqueue.event` instance. if the event is enabled then it disable the event.

**Returns**

- `ev:kqueue.event?`: `kqueue.event` instance that is reverted the meta-table of the `ev`, or `nil` if error occurred.
- `err:string`: error string.
- `errno:number`: error number.


## ok, err, errno = ev:watch()

watch the event.

**NOTE:** the event is managed by its type and a unique identifier pair. If this pair has already been watched, then the method will return `false`.

- `kqueue.read`: file descriptor used as the identifier.
- `kqueue.write`: file descriptor used as the identifier.
- `kqueue.signal`: signal number used as the identifier.
- `kqueue.timer`: timer identifier used as the identifier.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ok, err, errno = ev:unwatch()

unwatch the event.

**NOTE:** if the event is enabled then it disable the event.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ok = ev:is_enabled()

return `true` if the event is enabled (watching).

**Returns**

- `ok:boolean`: `true` if the event is enabled (watching).


## ok = ev:is_level()

return `true` if the event trigger is level trigger.

**Returns**

- `ok:boolean`: `true` if the event trigger is level trigger.


## ok, err, errno = ev:as_level()

change the event trigger to level trigger.

**NOTE:** if the event is enabled, it can not be changed.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ok = ev:is_edge()

return `true` if the event trigger is edge trigger.

**Returns**

- `ok:boolean`: `true` if the event trigger is edge trigger.


## ok, err, errno = ev:as_edge()

change the event trigger to edge trigger.

**NOTE:** if the event is enabled, it can not be changed.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ok = ev:is_oneshot()

return `true` if the event type is one-shot event.

**Returns**

- `ok:boolean`: `true` if the event type is one-shot event.


## ok, err, errno = ev:as_oneshot()

change the event type to one-shot event.

**NOTE:** if the event is enabled, it can not be changed.

**Returns**

- `ok:boolean`: `true` on success.
- `err:string`: error string.
- `errno:number`: error number.


## ident = ev:ident()

return the identifier of the event.

**Returns**

- `ident:number`: identifier of the event.


## udata = ev:udata( [udata] )

set or return the user data of the event. 

if the `udata` is specified then it set the user data of the event and return the previous user data.

**Returns**

- `udata:any`: user data of the event.


## info, err, errno = ev:getinfo( event )

get the information of the specified event.

**NOTE:** if the `EV_ERROR` flag is present in the flags, then the `data` treated as the `errno`.

**Parameters**

- `event:string`: event name as follows.
  - `registered`: return the information of the event that is registered.
  - `occurred`: return the information of the event that is occurred.
  
**Returns**

- `info:table`: information of the event.
  - `ident:number`: identifier of the event.
  - `flags:string`: flags of the event.
  - `fflags:string`: filter flags of the event.
  - `data:number`: data of the event.
  - `udata:any`: user data of the event.
  - `edge:boolean`: `true` if the event trigger is edge trigger.
  - `oneshot:boolean`: `true` if the event type is one-shot event.
- `err:string`: error string.
- `errno:number`: error number.

