/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#include "lua_kqueue.h"
#include <sys/mman.h>

#define MODULE_MT POLL_TRIGGER_MT

// atomic operation macros: prefer C11 stdatomic if available,
// fall back to GCC/Clang built-ins for C99 compatibility
#if __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#  include <stdatomic.h>
#  define ATOMIC_FETCH_ADD(ptr, val) \
       atomic_fetch_add((_Atomic uint64_t *)(ptr), (uint64_t)(val))
#  define ATOMIC_FETCH_SUB(ptr, val) \
       atomic_fetch_sub((_Atomic uint64_t *)(ptr), (uint64_t)(val))
#  define ATOMIC_STORE(ptr, val) \
       atomic_store((_Atomic uint64_t *)(ptr), (uint64_t)(val))
#elif defined(__GNUC__) || defined(__clang__)
#  define ATOMIC_FETCH_ADD(ptr, val) __sync_fetch_and_add((ptr), (val))
#  define ATOMIC_FETCH_SUB(ptr, val) __sync_fetch_and_sub((ptr), (val))
#  define ATOMIC_STORE(ptr, val)     __sync_lock_test_and_set((ptr), (val))
#else
#  error "Atomic operations not supported on this compiler"
#endif

int poll_trigger_consume(poll_event_t *ev)
{
    int fd      = (int)ev->reg_evt.ident;
    char buf[1] = {0};
    ssize_t n;

    if (ev->trigger.semaphore) {
        // semaphore mode: decrement counter; read pipe only when it reaches 0
        uint64_t old = ATOMIC_FETCH_SUB(ev->trigger.counter, 1);
        if (old == 1) {
            // counter reached 0, drain one byte from pipe to stop EVFILT_READ
            do {
                n = read(fd, buf, 1);
            } while (n == -1 && errno == EINTR);
            if (n == -1) {
                // roll back counter to avoid inconsistent state
                ATOMIC_FETCH_ADD(ev->trigger.counter, 1);
                return -1;
            }
        }
        return 0;
    }

    // counter mode: drain pipe, then reset counter to 0
    do {
        n = read(fd, buf, 1);
    } while (n == -1 && errno == EINTR);
    if (n == -1) {
        return -1;
    }
    ATOMIC_STORE(ev->trigger.counter, 0);
    return 0;
}

int poll_trigger_new(lua_State *L)
{
    poll_event_t *ev = luaL_checkudata(L, 1, POLL_EVENT_MT);
    int semaphore    = lua_toboolean(L, 2);
    int fds[2]       = {-1, -1};

    // keep udata reference (arg 3)
    if (!lua_isnoneornil(L, 3)) {
        ev->ref_udata = getrefat(L, 3);
    }

    // create pipe
    if (pipe(fds) == -1) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // create shared mmap counter (survives fork)
    void *mem = mmap(NULL, sizeof(uint64_t), PROT_READ | PROT_WRITE,
                     MAP_ANON | MAP_SHARED, -1, 0);
    if (mem == MAP_FAILED) {
        close(fds[0]);
        close(fds[1]);
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
    *(uint64_t *)mem = 0;

    ev->trigger.write_fd  = fds[1];
    ev->trigger.counter   = (uint64_t *)mem;
    ev->trigger.semaphore = semaphore;
    ev->type              = POLL_EVENT_TRIGGER;

    // register pipe read end with kqueue as EVFILT_READ
    EV_SET(&ev->reg_evt, fds[0], EVFILT_READ, ev->reg_evt.flags, 0, 0, NULL);
    if (poll_watch_event(L, ev, 1) != POLL_OK) {
        int err = errno;
        close(fds[0]);
        close(fds[1]);
        munmap(mem, sizeof(uint64_t));
        ev->trigger = (poll_event_trigger_t){0};
        ev->type    = POLL_EVENT_NONE;
        ev->reg_evt = (event_t){0};
        errno       = err;
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    lua_settop(L, 1);
    luaL_getmetatable(L, MODULE_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int trigger_lua(lua_State *L)
{
    poll_event_t *ev = luaL_checkudata(L, 1, MODULE_MT);

    if (!ev->enabled) {
        errno = EINPROGRESS;
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // atomically increment: write to pipe only when counter goes 0 -> 1
    uint64_t old = ATOMIC_FETCH_ADD(ev->trigger.counter, 1);
    if (old == 0) {
        char buf = '\x01';
        while (write(ev->trigger.write_fd, &buf, 1) == -1) {
            if (errno == EINTR) {
                continue;
            }
            // write failed: roll back counter increment
            ATOMIC_FETCH_SUB(ev->trigger.counter, 1);
            lua_pushboolean(L, 0);
            lua_pushstring(L, strerror(errno));
            lua_pushinteger(L, errno);
            return 3;
        }
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int revert_lua(lua_State *L)
{
    return poll_event_revert_lua(L, MODULE_MT);
}

static int renew_lua(lua_State *L)
{
    return poll_event_renew_lua(L, MODULE_MT);
}

static int watch_lua(lua_State *L)
{
    return poll_event_watch_lua(L, MODULE_MT);
}

static int unwatch_lua(lua_State *L)
{
    return poll_event_unwatch_lua(L, MODULE_MT);
}

static int is_enabled_lua(lua_State *L)
{
    return poll_event_is_enabled_lua(L, MODULE_MT);
}

static int is_eof_lua(lua_State *L)
{
    return poll_event_is_eof_lua(L, MODULE_MT);
}

static int is_level_lua(lua_State *L)
{
    return poll_event_is_level_lua(L, MODULE_MT);
}

static int as_level_lua(lua_State *L)
{
    return poll_event_as_level_lua(L, MODULE_MT);
}

static int is_edge_lua(lua_State *L)
{
    return poll_event_is_edge_lua(L, MODULE_MT);
}

static int as_edge_lua(lua_State *L)
{
    return poll_event_as_edge_lua(L, MODULE_MT);
}

static int is_oneshot_lua(lua_State *L)
{
    return poll_event_is_oneshot_lua(L, MODULE_MT);
}

static int as_oneshot_lua(lua_State *L)
{
    return poll_event_as_oneshot_lua(L, MODULE_MT);
}

static int ident_lua(lua_State *L)
{
    return poll_event_ident_lua(L, MODULE_MT);
}

static int udata_lua(lua_State *L)
{
    return poll_event_udata_lua(L, MODULE_MT);
}

static int getinfo_lua(lua_State *L)
{
    return poll_event_getinfo_lua(L, MODULE_MT);
}

static int type_lua(lua_State *L)
{
    lua_pushliteral(L, "trigger");
    return 1;
}

static int tostring_lua(lua_State *L)
{
    return poll_event_tostring_lua(L, MODULE_MT);
}

static int gc_lua(lua_State *L)
{
    return poll_event_gc_lua(L);
}

void libopen_poll_trigger(lua_State *L)
{
    struct luaL_Reg mmethod[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg method[] = {
        {"type",       type_lua      },
        {"renew",      renew_lua     },
        {"revert",     revert_lua    },
        {"watch",      watch_lua     },
        {"unwatch",    unwatch_lua   },
        {"trigger",    trigger_lua   },
        {"is_enabled", is_enabled_lua},
        {"is_eof",     is_eof_lua    },
        {"is_level",   is_level_lua  },
        {"as_level",   as_level_lua  },
        {"is_edge",    is_edge_lua   },
        {"as_edge",    as_edge_lua   },
        {"is_oneshot", is_oneshot_lua},
        {"as_oneshot", as_oneshot_lua},
        {"ident",      ident_lua     },
        {"udata",      udata_lua     },
        {"getinfo",    getinfo_lua   },
        {NULL,         NULL          }
    };

    // create metatable
    luaL_newmetatable(L, MODULE_MT);
    // metamethods
    for (struct luaL_Reg *ptr = mmethod; ptr->name; ptr++) {
        lua_pushcfunction(L, ptr->func);
        lua_setfield(L, -2, ptr->name);
    }
    // methods
    lua_newtable(L);
    for (struct luaL_Reg *ptr = method; ptr->name; ptr++) {
        lua_pushcfunction(L, ptr->func);
        lua_setfield(L, -2, ptr->name);
    }
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}
