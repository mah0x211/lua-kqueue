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

int kq_event_gc_lua(lua_State *L)
{
    kq_event_t *ev = lua_touserdata(L, 1);
    unref(L, ev->ref_kq);
    unref(L, ev->ref_udata);
    return 0;
}

int kq_event_tostring_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    lua_pushfstring(L, "%s: %p", tname, ev);
    return 1;
}

int kq_event_renew_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    kq_t *kq       = ev->kq;

    if (lua_gettop(L) > 1) {
        kq = luaL_checkudata(L, 2, KQ_MT);
    }

    int rc = kq_unwatch_event(L, ev);
    if (rc == KQ_ERROR) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // replace kqueue instance
    if (ev->kq != kq) {
        ev->kq     = kq;
        ev->ref_kq = unref(L, ev->ref_kq);
        lua_settop(L, 2);
        ev->ref_kq = getref(L);
    }

    // watch event again in new kqueue
    if (rc == KQ_OK) {
        return kq_event_watch_lua(L, tname);
    }

    lua_pushboolean(L, 1);
    return 1;
}

int kq_event_revert_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);

    if (kq_unwatch_event(L, ev) == KQ_ERROR) {
        // got error
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
    ev->registered = (struct kevent){0};
    ev->occurred   = (struct kevent){0};
    ev->ref_udata  = unref(L, ev->ref_udata);
    lua_settop(L, 1);
    luaL_getmetatable(L, KQ_EVENT_MT);
    lua_setmetatable(L, -2);
    lua_settop(L, 1);
    return 1;
}

kq_event_t *kq_evset_get(lua_State *L, kq_t *kq, struct kevent *evt)
{
    int ref_evset = LUA_NOREF;

    // get event set table reference
    switch (evt->filter) {
    case EVFILT_READ:
        ref_evset = kq->ref_evset_read;
        break;
    case EVFILT_WRITE:
        ref_evset = kq->ref_evset_write;
        break;
    case EVFILT_SIGNAL:
        ref_evset = kq->ref_evset_signal;
        break;
    case EVFILT_TIMER:
        ref_evset = kq->ref_evset_timer;
        break;

    default:
        luaL_error(L, "unsupported event filter: %d", evt->filter);
    }

    // get kq_event_t at the ident index
    pushref(L, ref_evset);
    lua_rawgeti(L, -1, evt->ident);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return NULL;
    }
    // remove event set table
    lua_replace(L, -2);
    return lua_touserdata(L, -1);
}

static int evset_add(lua_State *L, kq_t *kq, struct kevent *evt,
                     int kq_event_idx)
{
    int ref_evset = LUA_NOREF;

    // get event set table reference
    switch (evt->filter) {
    case EVFILT_READ:
        ref_evset = kq->ref_evset_read;
        break;
    case EVFILT_WRITE:
        ref_evset = kq->ref_evset_write;
        break;
    case EVFILT_SIGNAL:
        ref_evset = kq->ref_evset_signal;
        break;
    case EVFILT_TIMER:
        ref_evset = kq->ref_evset_timer;
        break;

    default:
        luaL_error(L, "unsupported event filter: %d", evt->filter);
    }

    // set kq_event_t at the ident index
    pushref(L, ref_evset);
    lua_rawgeti(L, -1, evt->ident);
    if (!lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return KQ_ALREADY;
    }
    lua_pop(L, 1);
    // set kq_event_t at the ident index
    lua_pushvalue(L, kq_event_idx);
    lua_rawseti(L, -2, evt->ident);
    // increment registered event counter
    kq->nreg++;
    lua_pop(L, 1);
    return KQ_OK;
}

int kq_watch_event(lua_State *L, kq_event_t *ev, int kq_event_idx)
{
    struct kevent evt = ev->registered;

    // check event is not already registered
    if (ev->enabled || evset_add(L, ev->kq, &evt, kq_event_idx) != KQ_OK) {
        // return error if already registered
        errno = EEXIST;
        return KQ_ALREADY;
    }

    // register event
    evt.flags |= EV_ADD;
    while (kevent(ev->kq->fd, &evt, 1, NULL, 0, NULL) == -1) {
        if (errno != EINTR) {
            kq_evset_del(L, ev->kq, &evt);
            return KQ_ERROR;
        }
    }
    ev->enabled = 1;

    return KQ_OK;
}

void kq_evset_del(lua_State *L, kq_t *kq, struct kevent *evt)
{
    int ref_evset = LUA_NOREF;

    // get event set table reference
    switch (evt->filter) {
    case EVFILT_READ:
        ref_evset = kq->ref_evset_read;
        break;
    case EVFILT_WRITE:
        ref_evset = kq->ref_evset_write;
        break;
    case EVFILT_SIGNAL:
        ref_evset = kq->ref_evset_signal;
        break;
    case EVFILT_TIMER:
        ref_evset = kq->ref_evset_timer;
        break;

    default:
        luaL_error(L, "unsupported event filter: %d", evt->filter);
    }

    // get kq_event_t at the ident index
    pushref(L, ref_evset);
    lua_pushnil(L);
    lua_rawseti(L, -2, evt->ident);
    kq->nreg--;
    lua_pop(L, 1);
}

int kq_unwatch_event(lua_State *L, kq_event_t *ev)
{
    if (!ev->enabled) {
        // not watched
        return KQ_ALREADY;
    }

    // unregister event
    struct kevent evt = ev->registered;
    evt.flags         = EV_DELETE;
    while (kevent(ev->kq->fd, &evt, 1, NULL, 0, NULL) != 0) {
        if (errno != EINTR) {
            return KQ_ERROR;
        }
    }
    ev->enabled = 0;
    kq_evset_del(L, ev->kq, &evt);

    return KQ_OK;
}

int kq_event_watch_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);

    switch (kq_watch_event(L, ev, 1)) {
    case KQ_OK:
        // success
        lua_pushboolean(L, 1);
        return 1;

    case KQ_ALREADY:
        // already watched
        lua_pushboolean(L, 0);
        return 1;

    default:
        // got error
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
}

int kq_event_unwatch_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);

    switch (kq_unwatch_event(L, ev)) {
    case KQ_OK:
        // success
        lua_pushboolean(L, 1);
        return 1;

    case KQ_ALREADY:
        // already unwatched
        lua_pushboolean(L, 0);
        return 1;

    default:
        // got error
        lua_pushboolean(L, 0);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int kq_event_is_enabled_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    lua_pushboolean(L, ev->enabled);
    return 1;
}

int kq_event_is_level_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    lua_pushboolean(L, !(ev->registered.flags & (EV_ONESHOT | EV_CLEAR)));
    return 1;
}

int kq_event_as_level_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);

    if (ev->enabled) {
        // event is in use
        errno = EINPROGRESS;
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // treat event as level-triggered event
    ev->registered.flags &= ~(EV_ONESHOT | EV_CLEAR);
    lua_settop(L, 1);
    return 1;
}

int kq_event_is_edge_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    lua_pushboolean(L, ev->registered.flags & EV_CLEAR);
    return 1;
}

int kq_event_as_edge_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);

    if (ev->enabled) {
        // event is in use
        errno = EINPROGRESS;
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // treat event as edge-triggered event
    ev->registered.flags &= ~EV_ONESHOT;
    ev->registered.flags |= EV_CLEAR;
    lua_settop(L, 1);
    return 1;
}

int kq_event_is_oneshot_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    lua_pushboolean(L, ev->registered.flags & EV_ONESHOT);
    return 1;
}

int kq_event_as_oneshot_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);

    if (ev->enabled) {
        // event is in use
        errno = EINPROGRESS;
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        lua_pushinteger(L, errno);
        return 3;
    }

    // treat event as oneshot event
    ev->registered.flags &= ~EV_CLEAR;
    ev->registered.flags |= EV_ONESHOT;
    lua_settop(L, 1);
    return 1;
}

int kq_event_ident_lua(lua_State *L, const char *tname)
{
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    lua_pushinteger(L, ev->registered.ident);
    return 1;
}

int kq_event_udata_lua(lua_State *L, const char *tname)
{
    int narg       = lua_gettop(L);
    kq_event_t *ev = luaL_checkudata(L, 1, tname);

    if (ev->ref_udata == LUA_NOREF) {
        lua_pushnil(L);
    } else {
        pushref(L, ev->ref_udata);
    }

    if (narg > 1) {
        if (lua_isnoneornil(L, 2)) {
            // release udata reference
            ev->ref_udata = unref(L, ev->ref_udata);
        } else {
            // replace new udata
            int ref       = getrefat(L, 2);
            ev->ref_udata = unref(L, ev->ref_udata);
            ev->ref_udata = ref;
        }
    }

    return 1;
}

static int push_event(lua_State *L, struct kevent evt, int ref_udata)
{
    int edge    = evt.flags & EV_CLEAR;
    int oneshot = evt.flags & EV_ONESHOT;
    int eof     = evt.flags & EV_EOF;
    int err     = evt.flags & EV_ERROR;
    evt.flags &= ~(EV_ADD | EV_CLEAR | EV_ONESHOT | EV_EOF | EV_ERROR);

    // push event
    lua_createtable(L, 0, 9);
    pushref(L, ref_udata);
    lua_setfield(L, -2, "udata");

#define pushinteger(field)                                                     \
 do {                                                                          \
  lua_pushinteger(L, evt.field);                                               \
  lua_setfield(L, -2, #field);                                                 \
 } while (0)
    pushinteger(ident);
    pushinteger(flags);
    pushinteger(fflags);
    pushinteger(data);
#undef pushinteger

    if (edge) {
        lua_pushboolean(L, edge);
        lua_setfield(L, -2, "edge");
    }
    if (oneshot) {
        lua_pushboolean(L, oneshot);
        lua_setfield(L, -2, "oneshot");
    }
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
}

int kq_event_getinfo_lua(lua_State *L, const char *tname)
{
    static const char *const opts[] = {
        "registered",
        "occurred",
        NULL,
    };
    kq_event_t *ev = luaL_checkudata(L, 1, tname);
    int selected   = luaL_checkoption(L, 2, NULL, opts);

    switch (selected) {
    case 0:
        return push_event(L, ev->registered, ev->ref_udata);
    default:
        return push_event(L, ev->occurred, ev->ref_udata);
    }
}
