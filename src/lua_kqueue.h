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

#ifndef lua_kqueue_h
#define lua_kqueue_h

#include "config.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/event.h>
#include <unistd.h>
// lualib
#include <lauxlib.h>

static inline int getref(lua_State *L)
{
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

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

typedef struct {
    int fd;
    int ref_evset_read;
    int ref_evset_write;
    int ref_evset_signal;
    int ref_evset_timer;
    int ref_evlist;
    int nreg;
    int nevt;
    int cur;
    int evsize;
    struct kevent *evlist;
} kq_t;

typedef struct {
    kq_t *kq;
    int ref_kq;
    int ref_udata;
    int enabled;
    struct kevent registered; // registered event
    struct kevent occurred;   // occurred event
} kq_event_t;

#define KQ_MT        "kqueue"
#define KQ_EVENT_MT  "kqueue.event"
#define KQ_READ_MT   "kqueue.read"
#define KQ_WRITE_MT  "kqueue.write"
#define KQ_SIGNAL_MT "kqueue.signal"
#define KQ_TIMER_MT  "kqueue.timer"

void luaopen_kqueue_event(lua_State *L);
void luaopen_kqueue_read(lua_State *L);
void luaopen_kqueue_write(lua_State *L);
void luaopen_kqueue_signal(lua_State *L);
void luaopen_kqueue_timer(lua_State *L);

int kqueue_raed_new(lua_State *L);
int kqueue_write_new(lua_State *L);
int kqueue_signal_new(lua_State *L);
int kqueue_timer_new(lua_State *L);

int kq_event_gc_lua(lua_State *L);
int kq_event_tostring_lua(lua_State *L, const char *tname);
int kq_event_renew_lua(lua_State *L, const char *tname);
int kq_event_revert_lua(lua_State *L, const char *tname);

kq_event_t *kq_evset_get(lua_State *L, kq_t *kq, struct kevent *evt);
void kq_evset_del(lua_State *L, kq_t *kq, struct kevent *evt);

#define KQ_ERROR   -1
#define KQ_OK      0
#define KQ_ALREADY 1

int kq_watch_event(lua_State *L, kq_event_t *ev, int kq_event_idx);
int kq_unwatch_event(lua_State *L, kq_event_t *ev);

int kq_event_watch_lua(lua_State *L, const char *tname);
int kq_event_unwatch_lua(lua_State *L, const char *tname);

int kq_event_kqueue_lua(lua_State *L, const char *tname);
int kq_event_is_enabled_lua(lua_State *L, const char *tname);
int kq_event_is_level_lua(lua_State *L, const char *tname);
int kq_event_as_level_lua(lua_State *L, const char *tname);
int kq_event_is_edge_lua(lua_State *L, const char *tname);
int kq_event_as_edge_lua(lua_State *L, const char *tname);
int kq_event_is_oneshot_lua(lua_State *L, const char *tname);
int kq_event_as_oneshot_lua(lua_State *L, const char *tname);
int kq_event_ident_lua(lua_State *L, const char *tname);
int kq_event_udata_lua(lua_State *L, const char *tname);
int kq_event_getinfo_lua(lua_State *L, const char *tname);

#endif
