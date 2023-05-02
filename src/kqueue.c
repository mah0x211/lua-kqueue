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

static int check_event_status(lua_State *L, poll_event_t *ev)
{
    if (ev->reg_evt.flags & EV_ONESHOT) {
        // oneshot event must be removed from the event set table and manually
        // disable event
        poll_evset_del(L, ev);
        ev->enabled = 0;
        return EV_ONESHOT;
    } else if (ev->occ_evt.flags & (EV_EOF | EV_ERROR)) {
        // event should be disabled when error occurred or EV_EOF is set
        if (poll_unwatch_event(L, ev) == POLL_ERROR) {
            return POLL_ERROR;
        }
        return EV_EOF;
    }

    return POLL_OK;
}

static int consume_lua(lua_State *L)
{
    poll_t *p   = luaL_checkudata(L, 1, POLL_MT);
    event_t evt = {0};

RECONSUME:
    lua_settop(L, 1);

    if (p->nevt == 0) {
        lua_pushnil(L);
        return 1;
    }

    evt = p->evlist[p->cur++];
    if (p->cur >= p->nevt) {
        // free event list if all events are consumed
        p->nevt = 0;
    }

    // NOTE: if poll_evset_get() returns a poll_event_t instance, it is placed
    // on the stack top.
    poll_event_t *ev = poll_evset_get(L, p, &evt);
    if (!ev) {
        // event is already unwatched
        goto RECONSUME;
    }
    ev->occ_evt = evt;
    pushref(L, ev->ref_udata);

    // check event status
    switch (check_event_status(L, ev)) {
    case POLL_OK:
        return 2;

    case EV_ONESHOT:
    case EV_EOF:
        lua_pushboolean(L, 1);
        return 3;

    default:
        lua_pop(L, 2);
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
}

static int cleanup_unconsumed_events(lua_State *L, poll_t *p)
{
    while (p->cur < p->nevt) {
        event_t evt      = p->evlist[p->cur++];
        poll_event_t *ev = poll_evset_get(L, p, &evt);

        if (!ev) {
            // event is already unwatched
            continue;
        }
        ev->occ_evt = evt;

        switch (check_event_status(L, ev)) {
        case POLL_OK:
        case EV_ONESHOT:
        case EV_EOF:
            lua_pop(L, 1);
            continue;

        default:
            lua_pop(L, 1);
            return POLL_ERROR;
        }
    }
    p->cur  = 0;
    p->nevt = 0;

    return POLL_OK;
}

static int wait_lua(lua_State *L)
{
    poll_t *p        = luaL_checkudata(L, 1, POLL_MT);
    // default timeout: -1(never timeout)
    lua_Integer msec = luaL_optinteger(L, 2, -1);

    // cleanup current events
    if (cleanup_unconsumed_events(L, p) == POLL_ERROR) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    if (p->nreg == 0) {
        // do not wait the event occurrs if no registered events exists
        lua_pushinteger(L, 0);
        return 1;
    }

    // grow event list
    if (p->evsize < p->nreg) {
        p->evlist     = lua_newuserdata(L, sizeof(event_t) * p->nreg);
        p->ref_evlist = unref(L, p->ref_evlist);
        p->ref_evlist = getref(L);
        p->evsize     = p->nreg;
    }

    int nevt = 0;
    if (msec <= 0) {
        // wait event forever
        nevt = kevent(p->fd, NULL, 0, p->evlist, p->nreg, NULL);
    } else {
        // wait event until timeout occurs
        struct timespec ts = {
            .tv_sec  = msec / 1000,
            .tv_nsec = (msec % 1000) * 1000000,
        };
        nevt = kevent(p->fd, NULL, 0, p->evlist, p->nreg, &ts);
    }

    // return number of event
    if (nevt != -1) {
        p->nevt = nevt;
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
    poll_t *p        = luaL_checkudata(L, 1, POLL_MT);
    poll_event_t *ev = lua_newuserdata(L, sizeof(poll_event_t));

    *ev = (poll_event_t){
        .p         = p,
        .ref_poll  = getrefat(L, 1),
        .ref_udata = LUA_NOREF,
        .reg_evt   = (event_t){0},
        .occ_evt   = (event_t){0},
    };
    // set metatable
    luaL_getmetatable(L, POLL_EVENT_MT);
    lua_setmetatable(L, -2);

    return 1;
}

static int renew_lua(lua_State *L)
{
    poll_t *p = luaL_checkudata(L, 1, POLL_MT);

    // cleanup current events before renew
    if (cleanup_unconsumed_events(L, p) != POLL_OK) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    int fd = kqueue();
    if (fd == -1) {
        // got error
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // close unused descriptor
    close(p->fd);
    p->fd = fd;

    lua_pushboolean(L, 1);
    return 1;
}

static int len_lua(lua_State *L)
{
    poll_t *p = luaL_checkudata(L, 1, POLL_MT);
    lua_pushinteger(L, p->nreg);
    return 1;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, POLL_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int gc_lua(lua_State *L)
{
    poll_t *p = lua_touserdata(L, 1);

    close(p->fd);
    unref(L, p->ref_evset_read);
    unref(L, p->ref_evset_write);
    unref(L, p->ref_evset_signal);
    unref(L, p->ref_evset_timer);
    unref(L, p->ref_evlist);

    return 0;
}

static int new_lua(lua_State *L)
{
    poll_t *p = lua_newuserdata(L, sizeof(poll_t));

    *p = (poll_t){
        // create poll descriptor
        .fd               = kqueue(),
        .ref_evset_read   = LUA_NOREF,
        .ref_evset_write  = LUA_NOREF,
        .ref_evset_signal = LUA_NOREF,
        .ref_evset_timer  = LUA_NOREF,
        .ref_evlist       = LUA_NOREF,
    };
    if (p->fd == -1) {
        // got error
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
    luaL_getmetatable(L, POLL_MT);
    lua_setmetatable(L, -2);

    // create evset tables
    lua_newtable(L);
    p->ref_evset_read = getref(L);
    lua_newtable(L);
    p->ref_evset_write = getref(L);
    lua_newtable(L);
    p->ref_evset_signal = getref(L);
    lua_newtable(L);
    p->ref_evset_timer = getref(L);

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

    libopen_poll_event(L);
    libopen_poll_read(L);
    libopen_poll_write(L);
    libopen_poll_signal(L);
    libopen_poll_timer(L);

    // create metatable
    luaL_newmetatable(L, POLL_MT);
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
