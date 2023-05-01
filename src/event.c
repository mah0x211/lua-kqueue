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

#define MODULE_MT KQ_EVENT_MT

static int as_oneshot_lua(lua_State *L)
{
    return kq_event_as_oneshot_lua(L, MODULE_MT);
}

static int is_oneshot_lua(lua_State *L)
{
    return kq_event_is_oneshot_lua(L, MODULE_MT);
}

static int as_edge_lua(lua_State *L)
{
    return kq_event_as_edge_lua(L, MODULE_MT);
}

static int is_edge_lua(lua_State *L)
{
    return kq_event_is_edge_lua(L, MODULE_MT);
}

static int as_level_lua(lua_State *L)
{
    return kq_event_as_level_lua(L, MODULE_MT);
}

static int is_level_lua(lua_State *L)
{
    return kq_event_is_level_lua(L, MODULE_MT);
}

static int renew_lua(lua_State *L)
{
    return kq_event_renew_lua(L, MODULE_MT);
}

static int tostring_lua(lua_State *L)
{
    return kq_event_tostring_lua(L, MODULE_MT);
}

static int gc_lua(lua_State *L)
{
    return kq_event_gc_lua(L);
}

void luaopen_kqueue_event(lua_State *L)
{
    struct luaL_Reg mmethod[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg method[] = {
        {"renew",      renew_lua        },
        {"is_level",   is_level_lua     },
        {"as_level",   as_level_lua     },
        {"is_edge",    is_edge_lua      },
        {"as_edge",    as_edge_lua      },
        {"is_oneshot", is_oneshot_lua   },
        {"as_oneshot", as_oneshot_lua   },
        {"as_read",    kqueue_raed_new  },
        {"as_write",   kqueue_write_new },
        {"as_signal",  kqueue_signal_new},
        {"as_timer",   kqueue_timer_new },
        {NULL,         NULL             }
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
