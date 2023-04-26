/**
 *  Copyright (C) 2023 Masatoshi Fukunaga
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

#include "config.h"
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <unistd.h>
// lualib
#include <lauxlib.h>

static inline int getrefat(lua_State *L, int idx)
{
    lua_pushvalue(L, idx);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static inline int unref(lua_State *L, int ref)
{
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    return LUA_NOREF;
}

static inline void pushref(lua_State *L, int ref)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
}

#define MODULE_MT "kqueue"

static sigset_t ALL_SIGNALS;

typedef struct {
    int fd;
    int nreg;
    int nevt;
    int cur;
    sigset_t ss;
    struct kevent *evlist;
} kq_t;

static int register_event(lua_State *L, kq_t *kq, int ident, int filter,
                          int flags, int fflags, int data, intptr_t ctx)
{
    struct kevent evt = {0};

    EV_SET(&evt, ident, filter, flags, fflags, data, (void *)ctx);
    lua_settop(L, 1);
    if (kevent(kq->fd, &evt, 1, NULL, 0, NULL) == -1) {
        // got error
        unref(L, ctx);
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    } else if (evt.flags & EV_ADD) {
        kq->nreg++;
    } else if (evt.flags & EV_DELETE) {
        kq->nreg--;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int del_lua(lua_State *L)
{
    kq_t *kq   = luaL_checkudata(L, 1, MODULE_MT);
    int filter = luaL_checkinteger(L, 2);

    switch (filter) {
    case EVFILT_READ:
    case EVFILT_WRITE:
    case EVFILT_TIMER: {
        int ident = luaL_checkinteger(L, 3);
        return register_event(L, kq, ident, filter, EV_DELETE, 0, 0, 0);
    }

    case EVFILT_SIGNAL: {
        int ident = luaL_checkinteger(L, 3);
        return register_event(L, kq, ident, filter, EV_DELETE, 0, 0, 0);
    }

    default:
        // unsupported filter
        errno = EINVAL;
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
}

static int add_event(lua_State *L, int extra_flags)
{
    kq_t *kq   = luaL_checkudata(L, 1, MODULE_MT);
    int filter = luaL_checkinteger(L, 2);

    switch (filter) {
    case EVFILT_READ:
    case EVFILT_WRITE: {
        int fd = luaL_checkinteger(L, 3);
        return register_event(L, kq, fd, filter, EV_ADD | extra_flags, 0, 0,
                              getrefat(L, 4));
    }

    case EVFILT_SIGNAL: {
        int signo = luaL_checkinteger(L, 3);

        // check if signal is valid
        if (sigismember(&ALL_SIGNALS, signo) == 0) {
            errno = EINVAL;
            lua_pushboolean(L, 0);
            lua_pushstring(L, strerror(errno));
            lua_pushinteger(L, errno);
            return 3;
        }

        return register_event(L, kq, signo, filter, EV_ADD | extra_flags, 0, 0,
                              getrefat(L, 4));
    }

    case EVFILT_TIMER: {
        int ident = luaL_checkinteger(L, 3);
        int msec  = luaL_checkinteger(L, 4);

        // check if msec is valid
        if (msec < 0) {
            errno = EINVAL;
            lua_pushboolean(L, 0);
            lua_pushstring(L, strerror(errno));
            lua_pushinteger(L, errno);
            return 3;
        }

        return register_event(L, kq, ident, filter, EV_ADD | extra_flags, 0,
                              msec, getrefat(L, 5));
    }

    default:
        // unsupported filter
        errno = EINVAL;
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
}

static int add_oneshot_lua(lua_State *L)
{
    return add_event(L, EV_ONESHOT);
}

static int add_edge_lua(lua_State *L)
{
    return add_event(L, EV_CLEAR);
}

static int add_lua(lua_State *L)
{
    return add_event(L, 0);
}

static int consume_lua(lua_State *L)
{
    kq_t *kq = luaL_checkudata(L, 1, MODULE_MT);

    if (!kq->evlist) {
        lua_pushnil(L);
        return 1;
    }

    struct kevent evt = kq->evlist[kq->cur++];
    if (kq->cur >= kq->nevt) {
        // free event list
        free(kq->evlist);
        kq->evlist = NULL;
    }

    int edge    = evt.flags & EV_CLEAR;
    int oneshot = evt.flags & EV_ONESHOT;
    int eof     = evt.flags & EV_EOF;
    int err     = evt.flags & EV_ERROR;
    evt.flags &= ~(EV_ADD | EV_CLEAR | EV_ONESHOT | EV_EOF | EV_ERROR);

    // push event
    lua_settop(L, 1);
    lua_createtable(L, 0, 9);
    pushref(L, (uintptr_t)evt.udata);
    lua_setfield(L, -2, "udata");

#define pushinteger(field)                                                     \
 do {                                                                          \
  lua_pushinteger(L, evt.field);                                               \
  lua_setfield(L, -2, #field);                                                 \
 } while (0)
    pushinteger(ident);
    pushinteger(filter);
    pushinteger(flags);
    pushinteger(fflags);
    pushinteger(data);
#undef pushinteger

    if (oneshot) {
        kq->nreg--;
        lua_pushboolean(L, oneshot);
        lua_setfield(L, -2, "oneshot");
        if (eof) {
            lua_pushboolean(L, eof);
            lua_setfield(L, -2, "eof");
        }
        if (err) {
            lua_pushstring(L, strerror(evt.data));
            lua_pushinteger(L, evt.data);
            return 3;
        }
        return 1;
    } else if (edge) {
        lua_pushboolean(L, edge);
        lua_setfield(L, -2, "edge");
    }

    int retval = 1;
    if (eof) {
        lua_pushboolean(L, eof);
        lua_setfield(L, -2, "eof");
        goto UNREGISTER;
    } else if (err) {
        retval = 3;
        lua_pushstring(L, strerror(evt.data));
        lua_pushinteger(L, evt.data);

UNREGISTER:
        kq->nreg--;
        evt.flags = EV_DELETE;
        while (kevent(kq->fd, &evt, 1, NULL, 0, NULL) == -1 && errno == EINTR) {
        }
        return retval;
    }

    return 1;
}

static int wait_lua(lua_State *L)
{
    kq_t *kq         = luaL_checkudata(L, 1, MODULE_MT);
    // default timeout: -1(never timeout)
    lua_Integer msec = luaL_optinteger(L, 2, -1);

    // cleanup current events
    if (kq->evlist) {
        while (kq->nevt) {
            struct kevent evt = kq->evlist[--kq->nevt];
            // remove from kernel event
            if (evt.flags & EV_ERROR) {
                evt.flags = EV_DELETE;
                kevent(kq->fd, &evt, 1, NULL, 0, NULL);
                kq->nreg--;
            }
        }
        free(kq->evlist);
        kq->evlist = NULL;
    }

    kq->nevt = 0;
    kq->cur  = 0;
    if (kq->nreg == 0) {
        // do not wait the event occurrs if no registered events exists
        lua_pushinteger(L, 0);
        return 1;
    }

    struct kevent *evlist = calloc(kq->nreg, sizeof(struct kevent));
    if (!evlist) {
        // got error
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    int nevt = 0;
    if (msec <= 0) {
        // wait event forever
        nevt = kevent(kq->fd, NULL, 0, evlist, kq->nreg, NULL);
    } else {
        // wait event until timeout occurs
        struct timespec ts = {
            .tv_sec  = msec / 1000,
            .tv_nsec = (msec % 1000) * 1000000,
        };
        nevt = kevent(kq->fd, NULL, 0, evlist, kq->nreg, &ts);
    }

    // return number of event
    if (nevt != -1) {
        kq->nevt   = nevt;
        kq->evlist = evlist;
        lua_pushinteger(L, nevt);
        return 1;
    }
    free(evlist);

    // got error
    switch (errno) {
    // ignore error
    case ENOENT:
    case EINTR:
        errno = 0;
        lua_pushinteger(L, 0);
        return 1;

    // return error
    default:
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
}

static int len_lua(lua_State *L)
{
    kq_t *kq = luaL_checkudata(L, 1, MODULE_MT);
    lua_pushinteger(L, kq->nreg);
    return 1;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, MODULE_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int gc_lua(lua_State *L)
{
    kq_t *kq = lua_touserdata(L, 1);

    // close if not invalid value
    if (kq->fd != -1) {
        close(kq->fd);
    }
    if (kq->evlist) {
        free(kq->evlist);
    }

    return 0;
}

static int new_lua(lua_State *L)
{
    kq_t *kq = lua_newuserdata(L, sizeof(kq_t));

    *kq = (kq_t){
        .fd     = -1,
        .evlist = NULL,
        .nreg   = 0,
    };

    // create event descriptor
    kq->fd = kqueue();
    if (kq->fd == -1) {
        // got error
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
    luaL_getmetatable(L, MODULE_MT);
    lua_setmetatable(L, -2);

    return 1;
}

static int usable_lua(lua_State *L)
{
    lua_pushboolean(L, 1);
    return 1;
}

LUALIB_API int luaopen_kqueue(lua_State *L)
{
    struct luaL_Reg mmethod[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {"__len",      len_lua     },
        {NULL,         NULL        }
    };
    struct luaL_Reg method[] = {
        {"wait",        wait_lua       },
        {"consume",     consume_lua    },
        {"add",         add_lua        },
        {"add_edge",    add_edge_lua   },
        {"add_oneshot", add_oneshot_lua},
        {"del",         del_lua        },
        {NULL,          NULL           }
    };

    // initialize all signals
    if (sigfillset(&ALL_SIGNALS) == -1) {
        return luaL_error(L,
                          "failed to initialization: "
                          "sigfillset: %s",
                          strerror(errno));
    }

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

    // create table
    lua_newtable(L);
    lua_pushcfunction(L, new_lua);
    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, usable_lua);
    lua_setfield(L, -2, "usable");

    // export event filters
    lua_pushinteger(L, EVFILT_READ);
    lua_setfield(L, -2, "EVFILT_READ");
    lua_pushinteger(L, EVFILT_WRITE);
    lua_setfield(L, -2, "EVFILT_WRITE");
    lua_pushinteger(L, EVFILT_SIGNAL);
    lua_setfield(L, -2, "EVFILT_SIGNAL");
    lua_pushinteger(L, EVFILT_TIMER);
    lua_setfield(L, -2, "EVFILT_TIMER");

    return 1;
}
