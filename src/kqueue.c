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

#include "lua_kqueue.h"

static int consume_lua(lua_State *L)
{
    kq_t *kq          = luaL_checkudata(L, 1, KQ_MT);
    struct kevent evt = {0};

RECONSUME:
    lua_settop(L, 1);

    if (kq->nevt == 0) {
        lua_pushnil(L);
        return 1;
    }

    evt = kq->evlist[kq->cur++];
    if (kq->cur >= kq->nevt) {
        // free event list if all events are consumed
        kq->nevt = 0;
    }

    // NOTE: if kq_evset_get() returns a kq_event_t instance, it is placed on
    // the stack top.
    kq_event_t *ev = kq_evset_get(L, kq, &evt);
    if (!ev) {
        // event is already unwatched
        goto RECONSUME;
    }
    ev->occurred = evt;
    pushref(L, ev->ref_udata);

    if (evt.flags & EV_ONESHOT) {
        // oneshot event must be removed from the event set table and manually
        // disable event
        kq_evset_del(L, kq, &evt);
        ev->enabled = 0;
        lua_pushboolean(L, 1);
        return 3;
    } else if (evt.flags & (EV_EOF | EV_ERROR)) {
        // event should be disabled when error occurred or EV_EOF is set
        if (kq_unwatch_event(L, ev) == KQ_ERROR) {
            lua_pushnil(L);
            lua_pushstring(L, strerror(errno));
            lua_pushinteger(L, errno);
            return 3;
        }
        lua_pushboolean(L, 1);
        return 3;
    }

    return 2;
}

static int wait_lua(lua_State *L)
{
    kq_t *kq         = luaL_checkudata(L, 1, KQ_MT);
    // default timeout: -1(never timeout)
    lua_Integer msec = luaL_optinteger(L, 2, -1);

    // cleanup current events
    while (kq->cur < kq->nevt) {
        struct kevent evt = kq->evlist[kq->cur++];
        kq_event_t *ev    = evt.udata;

        // oneshot event
        if (evt.flags & EV_ONESHOT) {
            kq_evset_del(L, kq, &evt);
            ev->enabled = 0;
        } else if (evt.flags & EV_ERROR || evt.flags & EV_EOF) {
            if (kq_unwatch_event(L, ev) == KQ_ERROR) {
                lua_pushnil(L);
                lua_pushstring(L, strerror(errno));
                lua_pushinteger(L, errno);
                return 3;
            }
        }
    }

    kq->cur  = 0;
    kq->nevt = 0;
    if (kq->nreg == 0) {
        // do not wait the event occurrs if no registered events exists
        lua_pushinteger(L, 0);
        return 1;
    }

    // grow event list
    if (kq->evsize < kq->nreg) {
        kq->evlist     = lua_newuserdata(L, sizeof(struct kevent) * kq->nreg);
        kq->ref_evlist = unref(L, kq->ref_evlist);
        kq->ref_evlist = getref(L);
        kq->evsize     = kq->nreg;
    }

    int nevt = 0;
    if (msec <= 0) {
        // wait event forever
        nevt = kevent(kq->fd, NULL, 0, kq->evlist, kq->nreg, NULL);
    } else {
        // wait event until timeout occurs
        struct timespec ts = {
            .tv_sec  = msec / 1000,
            .tv_nsec = (msec % 1000) * 1000000,
        };
        nevt = kevent(kq->fd, NULL, 0, kq->evlist, kq->nreg, &ts);
    }

    // return number of event
    if (nevt != -1) {
        kq->nevt = nevt;
        lua_pushinteger(L, nevt);
        return 1;
    }

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

static int new_event_lua(lua_State *L)
{
    kq_t *kq       = luaL_checkudata(L, 1, KQ_MT);
    kq_event_t *ev = lua_newuserdata(L, sizeof(kq_event_t));

    *ev = (kq_event_t){
        .kq         = kq,
        .ref_kq     = getrefat(L, 1),
        .ref_udata  = LUA_NOREF,
        .registered = (struct kevent){0},
        .occurred   = (struct kevent){0},
    };
    // set metatable
    luaL_getmetatable(L, KQ_EVENT_MT);
    lua_setmetatable(L, -2);

    return 1;
}

static int renew_lua(lua_State *L)
{
    kq_t *kq = luaL_checkudata(L, 1, KQ_MT);

    // cleanup current events before renew
    while (kq->cur < kq->nevt) {
        struct kevent evt = kq->evlist[kq->cur++];
        kq_event_t *ev    = evt.udata;

        // oneshot event
        if (evt.flags & EV_ONESHOT) {
            kq_evset_del(L, kq, &evt);
            ev->enabled = 0;
        } else if (kq_unwatch_event(L, ev) == KQ_ERROR) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, strerror(errno));
            lua_pushinteger(L, errno);
            return 3;
        }
    }
    kq->cur  = 0;
    kq->nevt = 0;

    int fd = kqueue();
    if (fd == -1) {
        // got error
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // close unused descriptor
    close(kq->fd);
    kq->fd = fd;

    lua_pushboolean(L, 1);
    return 1;
}

static int len_lua(lua_State *L)
{
    kq_t *kq = luaL_checkudata(L, 1, KQ_MT);
    lua_pushinteger(L, kq->nreg);
    return 1;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, KQ_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int gc_lua(lua_State *L)
{
    kq_t *kq = lua_touserdata(L, 1);

    close(kq->fd);
    unref(L, kq->ref_evset_read);
    unref(L, kq->ref_evset_write);
    unref(L, kq->ref_evset_signal);
    unref(L, kq->ref_evset_timer);
    unref(L, kq->ref_evlist);

    return 0;
}

static int new_lua(lua_State *L)
{
    kq_t *kq = lua_newuserdata(L, sizeof(kq_t));

    *kq = (kq_t){
        // create kqueue descriptor
        .fd               = kqueue(),
        .ref_evset_read   = LUA_NOREF,
        .ref_evset_write  = LUA_NOREF,
        .ref_evset_signal = LUA_NOREF,
        .ref_evset_timer  = LUA_NOREF,
        .ref_evlist       = LUA_NOREF,
    };
    if (kq->fd == -1) {
        // got error
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
    luaL_getmetatable(L, KQ_MT);
    lua_setmetatable(L, -2);

    // create evset tables
    lua_newtable(L);
    kq->ref_evset_read = getref(L);
    lua_newtable(L);
    kq->ref_evset_write = getref(L);
    lua_newtable(L);
    kq->ref_evset_signal = getref(L);
    lua_newtable(L);
    kq->ref_evset_timer = getref(L);

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
        {"renew",     renew_lua    },
        {"new_event", new_event_lua},
        {"wait",      wait_lua     },
        {"consume",   consume_lua  },
        {NULL,        NULL         }
    };

    luaopen_kqueue_event(L);
    luaopen_kqueue_read(L);
    luaopen_kqueue_write(L);
    luaopen_kqueue_signal(L);
    luaopen_kqueue_timer(L);

    // create metatable
    luaL_newmetatable(L, KQ_MT);
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

    return 1;
}
