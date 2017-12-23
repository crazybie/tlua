/*=========================================================================*\
* Auxiliar routines for class hierarchy manipulation
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>
#include <stdio.h>

#include "luasocket.h"
#ifdef _MSC_VER
#include <Ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#undef gai_strerror
#define gai_strerror gai_strerrorA
#endif


/*=========================================================================*\
* Exported functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Initializes the module
\*-------------------------------------------------------------------------*/
int auxiliar_open(lua_State *L) {
    (void) L;
    return 0;
}

/*-------------------------------------------------------------------------*\
* Creates a new class with given methods
* Methods whose names start with __ are passed directly to the metatable.
\*-------------------------------------------------------------------------*/
void auxiliar_newclass(lua_State *L, const char *classname, luaL_Reg *func) {
    luaL_newmetatable(L, classname); /* mt */
    /* create __index table to place methods */
    lua_pushstring(L, "__index");    /* mt,"__index" */
    lua_newtable(L);                 /* mt,"__index",it */
    /* put class name into class metatable */
    lua_pushstring(L, "class");      /* mt,"__index",it,"class" */
    lua_pushstring(L, classname);    /* mt,"__index",it,"class",classname */
    lua_rawset(L, -3);               /* mt,"__index",it */
    /* pass all methods that start with _ to the metatable, and all others
     * to the index table */
    for (; func->name; func++) {     /* mt,"__index",it */
        lua_pushstring(L, func->name);
        lua_pushcfunction(L, func->func);
        lua_rawset(L, func->name[0] == '_' ? -5: -3);
    }
    lua_rawset(L, -3);               /* mt */
    lua_pop(L, 1);
}

/*-------------------------------------------------------------------------*\
* Prints the value of a class in a nice way
\*-------------------------------------------------------------------------*/
int auxiliar_tostring(lua_State *L) {
    char buf[32];
    if (!lua_getmetatable(L, 1)) goto error;
    lua_pushstring(L, "__index");
    lua_gettable(L, -2);
    if (!lua_istable(L, -1)) goto error;
    lua_pushstring(L, "class");
    lua_gettable(L, -2);
    if (!lua_isstring(L, -1)) goto error;
    sprintf(buf, "%p", lua_touserdata(L, 1));
    lua_pushfstring(L, "%s: %s", lua_tostring(L, -1), buf);
    return 1;
error:
    lua_pushstring(L, "invalid object passed to 'auxiliar.c:__tostring'");
    lua_error(L);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Insert class into group
\*-------------------------------------------------------------------------*/
void auxiliar_add2group(lua_State *L, const char *classname, const char *groupname) {
    luaL_getmetatable(L, classname);
    lua_pushstring(L, groupname);
    lua_pushboolean(L, 1);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

/*-------------------------------------------------------------------------*\
* Make sure argument is a boolean
\*-------------------------------------------------------------------------*/
int auxiliar_checkboolean(lua_State *L, int objidx) {
    if (!lua_isboolean(L, objidx))
        auxiliar_typeerror(L, objidx, lua_typename(L, LUA_TBOOLEAN));
    return lua_toboolean(L, objidx);
}

/*-------------------------------------------------------------------------*\
* Return userdata pointer if object belongs to a given class, abort with
* error otherwise
\*-------------------------------------------------------------------------*/
void *auxiliar_checkclass(lua_State *L, const char *classname, int objidx) {
    void *data = auxiliar_getclassudata(L, classname, objidx);
    if (!data) {
        char msg[45];
        sprintf(msg, "%.35s expected", classname);
        luaL_argerror(L, objidx, msg);
    }
    return data;
}

/*-------------------------------------------------------------------------*\
* Return userdata pointer if object belongs to a given group, abort with
* error otherwise
\*-------------------------------------------------------------------------*/
void *auxiliar_checkgroup(lua_State *L, const char *groupname, int objidx) {
    void *data = auxiliar_getgroupudata(L, groupname, objidx);
    if (!data) {
        char msg[45];
        sprintf(msg, "%.35s expected", groupname);
        luaL_argerror(L, objidx, msg);
    }
    return data;
}

/*-------------------------------------------------------------------------*\
* Set object class
\*-------------------------------------------------------------------------*/
void auxiliar_setclass(lua_State *L, const char *classname, int objidx) {
    luaL_getmetatable(L, classname);
    if (objidx < 0) objidx--;
    lua_setmetatable(L, objidx);
}

/*-------------------------------------------------------------------------*\
* Get a userdata pointer if object belongs to a given group. Return NULL
* otherwise
\*-------------------------------------------------------------------------*/
void *auxiliar_getgroupudata(lua_State *L, const char *groupname, int objidx) {
    if (!lua_getmetatable(L, objidx))
        return NULL;
    lua_pushstring(L, groupname);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return NULL;
    } else {
        lua_pop(L, 2);
        return lua_touserdata(L, objidx);
    }
}

/*-------------------------------------------------------------------------*\
* Get a userdata pointer if object belongs to a given class. Return NULL
* otherwise
\*-------------------------------------------------------------------------*/
void *auxiliar_getclassudata(lua_State *L, const char *classname, int objidx) {
    return luaL_checkudata(L, objidx, classname);
}

/*-------------------------------------------------------------------------*\
* Throws error when argument does not have correct type.
* Used to be part of lauxlib in Lua 5.1, was dropped from 5.2.
\*-------------------------------------------------------------------------*/
int auxiliar_typeerror (lua_State *L, int narg, const char *tname) {
  const char *msg = lua_pushfstring(L, "%s expected, got %s", tname,
      luaL_typename(L, narg));
  return luaL_argerror(L, narg, msg);
}


/*=========================================================================*\
* Input/Output interface for Lua programs
* LuaSocket toolkit
\*=========================================================================*/
#include "lua.h"
//#include "lauxlib.h"


/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int recvraw(p_buffer buf, size_t wanted, luaL_Buffer *b);
static int recvline(p_buffer buf, luaL_Buffer *b);
static int recvall(p_buffer buf, luaL_Buffer *b);
static int buffer_get(p_buffer buf, const char **data, size_t *count);
static void buffer_skip(p_buffer buf, size_t count);
static int sendraw(p_buffer buf, const char *data, size_t count, size_t *sent);

/* min and max macros */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? x : y)
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? x : y)
#endif

/*=========================================================================*\
* Exported functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int buffer_open(lua_State *L) {
    (void) L;
    return 0;
}

/*-------------------------------------------------------------------------*\
* Initializes C structure
\*-------------------------------------------------------------------------*/
void buffer_init(p_buffer buf, p_io io, p_timeout tm) {
    buf->first = buf->last = 0;
    buf->io = io;
    buf->tm = tm;
    buf->received = buf->sent = 0;
    buf->birthday = timeout_gettime();
}

/*-------------------------------------------------------------------------*\
* object:getstats() interface
\*-------------------------------------------------------------------------*/
int buffer_meth_getstats(lua_State *L, p_buffer buf) {
    lua_pushnumber(L, (lua_Number) buf->received);
    lua_pushnumber(L, (lua_Number) buf->sent);
    lua_pushnumber(L, timeout_gettime() - buf->birthday);
    return 3;
}

/*-------------------------------------------------------------------------*\
* object:setstats() interface
\*-------------------------------------------------------------------------*/
int buffer_meth_setstats(lua_State *L, p_buffer buf) {
    buf->received = (long) luaL_optnumber(L, 2, (lua_Number) buf->received);
    buf->sent = (long) luaL_optnumber(L, 3, (lua_Number) buf->sent);
    if (lua_isnumber(L, 4)) buf->birthday = timeout_gettime() - lua_tonumber(L, 4);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* object:send() interface
\*-------------------------------------------------------------------------*/
int buffer_meth_send(lua_State *L, p_buffer buf) {
    int top = lua_gettop(L);
    int err = IO_DONE;
    size_t size = 0, sent = 0;
    const char *data = luaL_checklstring(L, 2, &size);
    long start = (long) luaL_optnumber(L, 3, 1);
    long end = (long) luaL_optnumber(L, 4, -1);
    timeout_markstart(buf->tm);
    if (start < 0) start = (long) (size+start+1);
    if (end < 0) end = (long) (size+end+1);
    if (start < 1) start = (long) 1;
    if (end > (long) size) end = (long) size;
    if (start <= end) err = sendraw(buf, data+start-1, end-start+1, &sent);
    /* check if there was an error */
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, buf->io->error(buf->io->ctx, err));
        lua_pushnumber(L, (lua_Number) (sent+start-1));
    } else {
        lua_pushnumber(L, (lua_Number) (sent+start-1));
        lua_pushnil(L);
        lua_pushnil(L);
    }
#ifdef LUASOCKET_DEBUG
    /* push time elapsed during operation as the last return value */
    lua_pushnumber(L, timeout_gettime() - timeout_getstart(buf->tm));
#endif
    return lua_gettop(L) - top;
}

/*-------------------------------------------------------------------------*\
* object:receive() interface
\*-------------------------------------------------------------------------*/
int buffer_meth_receive(lua_State *L, p_buffer buf) {
    int err = IO_DONE, top = lua_gettop(L);
    luaL_Buffer b;
    size_t size;
    const char *part = luaL_optlstring(L, 3, "", &size);
    timeout_markstart(buf->tm);
    /* initialize buffer with optional extra prefix
     * (useful for concatenating previous partial results) */
    luaL_buffinit(L, &b);
    luaL_addlstring(&b, part, size);
    /* receive new patterns */
    if (!lua_isnumber(L, 2)) {
        const char *p= luaL_optstring(L, 2, "*l");
        if (p[0] == '*' && p[1] == 'l') err = recvline(buf, &b);
        else if (p[0] == '*' && p[1] == 'a') err = recvall(buf, &b);
        else luaL_argcheck(L, 0, 2, "invalid receive pattern");
    /* get a fixed number of bytes (minus what was already partially
     * received) */
    } else {
        double n = lua_tonumber(L, 2);
        size_t wanted = (size_t) n;
        luaL_argcheck(L, n >= 0, 2, "invalid receive pattern");
        if (size == 0 || wanted > size)
            err = recvraw(buf, wanted-size, &b);
    }
    /* check if there was an error */
    if (err != IO_DONE) {
        /* we can't push anyting in the stack before pushing the
         * contents of the buffer. this is the reason for the complication */
        luaL_pushresult(&b);
        lua_pushstring(L, buf->io->error(buf->io->ctx, err));
        lua_pushvalue(L, -2);
        lua_pushnil(L);
        lua_replace(L, -4);
    } else {
        luaL_pushresult(&b);
        lua_pushnil(L);
        lua_pushnil(L);
    }
#ifdef LUASOCKET_DEBUG
    /* push time elapsed during operation as the last return value */
    lua_pushnumber(L, timeout_gettime() - timeout_getstart(buf->tm));
#endif
    return lua_gettop(L) - top;
}

/*-------------------------------------------------------------------------*\
* Determines if there is any data in the read buffer
\*-------------------------------------------------------------------------*/
int buffer_isempty(p_buffer buf) {
    return buf->first >= buf->last;
}

/*=========================================================================*\
* Internal functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Sends a block of data (unbuffered)
\*-------------------------------------------------------------------------*/
#define STEPSIZE 8192
static int sendraw(p_buffer buf, const char *data, size_t count, size_t *sent) {
    p_io io = buf->io;
    p_timeout tm = buf->tm;
    size_t total = 0;
    int err = IO_DONE;
    while (total < count && err == IO_DONE) {
        size_t done = 0;
        size_t step = (count-total <= STEPSIZE)? count-total: STEPSIZE;
        err = io->send(io->ctx, data+total, step, &done, tm);
        total += done;
    }
    *sent = total;
    buf->sent += total;
    return err;
}

/*-------------------------------------------------------------------------*\
* Reads a fixed number of bytes (buffered)
\*-------------------------------------------------------------------------*/
static int recvraw(p_buffer buf, size_t wanted, luaL_Buffer *b) {
    int err = IO_DONE;
    size_t total = 0;
    while (err == IO_DONE) {
        size_t count; const char *data;
        err = buffer_get(buf, &data, &count);
        count = MIN(count, wanted - total);
        luaL_addlstring(b, data, count);
        buffer_skip(buf, count);
        total += count;
        if (total >= wanted) break;
    }
    return err;
}

/*-------------------------------------------------------------------------*\
* Reads everything until the connection is closed (buffered)
\*-------------------------------------------------------------------------*/
static int recvall(p_buffer buf, luaL_Buffer *b) {
    int err = IO_DONE;
    size_t total = 0;
    while (err == IO_DONE) {
        const char *data; size_t count;
        err = buffer_get(buf, &data, &count);
        total += count;
        luaL_addlstring(b, data, count);
        buffer_skip(buf, count);
    }
    if (err == IO_CLOSED) {
        if (total > 0) return IO_DONE;
        else return IO_CLOSED;
    } else return err;
}

/*-------------------------------------------------------------------------*\
* Reads a line terminated by a CR LF pair or just by a LF. The CR and LF
* are not returned by the function and are discarded from the buffer
\*-------------------------------------------------------------------------*/
static int recvline(p_buffer buf, luaL_Buffer *b) {
    int err = IO_DONE;
    while (err == IO_DONE) {
        size_t count, pos; const char *data;
        err = buffer_get(buf, &data, &count);
        pos = 0;
        while (pos < count && data[pos] != '\n') {
            /* we ignore all \r's */
            if (data[pos] != '\r') luaL_addchar(b, data[pos]);
            pos++;
        }
        if (pos < count) { /* found '\n' */
            buffer_skip(buf, pos+1); /* skip '\n' too */
            break; /* we are done */
        } else /* reached the end of the buffer */
            buffer_skip(buf, pos);
    }
    return err;
}

/*-------------------------------------------------------------------------*\
* Skips a given number of bytes from read buffer. No data is read from the
* transport layer
\*-------------------------------------------------------------------------*/
static void buffer_skip(p_buffer buf, size_t count) {
    buf->received += count;
    buf->first += count;
    if (buffer_isempty(buf))
        buf->first = buf->last = 0;
}

/*-------------------------------------------------------------------------*\
* Return any data available in buffer, or get more data from transport layer
* if buffer is empty
\*-------------------------------------------------------------------------*/
static int buffer_get(p_buffer buf, const char **data, size_t *count) {
    int err = IO_DONE;
    p_io io = buf->io;
    p_timeout tm = buf->tm;
    if (buffer_isempty(buf)) {
        size_t got;
        err = io->recv(io->ctx, buf->data, BUF_SIZE, &got, tm);
        buf->first = 0;
        buf->last = got;
    }
    *count = buf->last - buf->first;
    *data = buf->data + buf->first;
    return err;
}


#if LUA_VERSION_NUM==501
/*
** Adapted from Lua 5.2
*/
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
  luaL_checkstack(L, nup+1, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    int i;
    lua_pushstring(L, l->name);
    for (i = 0; i < nup; i++)  /* copy upvalues to the top */
      lua_pushvalue(L, -(nup+1));
    lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    lua_settable(L, -(nup + 3));
  }
  lua_pop(L, nup);  /* remove upvalues */
}
#endif

/*=========================================================================*\
* Simple exception support
* LuaSocket toolkit
\*=========================================================================*/
#include <stdio.h>

#include "lua.h"
//#include "lauxlib.h"


#if LUA_VERSION_NUM < 502
#define lua_pcallk(L, na, nr, err, ctx, cont) \
    (((void)ctx),((void)cont),lua_pcall(L, na, nr, err))
#endif

#if LUA_VERSION_NUM < 503
typedef int lua_KContext;
#endif

/*=========================================================================*\
* Internal function prototypes.
\*=========================================================================*/
static int global_protect(lua_State *L);
static int global_newtry(lua_State *L);
static int protected_(lua_State *L);
static int finalize(lua_State *L);
static int do_nothing(lua_State *L);

/* except functions */
static luaL_Reg excptfunc[] = {
    {"newtry",    global_newtry},
    {"protect",   global_protect},
    {NULL,        NULL}
};

/*-------------------------------------------------------------------------*\
* Try factory
\*-------------------------------------------------------------------------*/
static void wrap(lua_State *L) {
    lua_createtable(L, 1, 0);
    lua_pushvalue(L, -2);
    lua_rawseti(L, -2, 1);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setmetatable(L, -2);
}

static int finalize(lua_State *L) {
    if (!lua_toboolean(L, 1)) {
        lua_pushvalue(L, lua_upvalueindex(2));
        lua_call(L, 0, 0);
        lua_settop(L, 2);
        wrap(L);
        lua_error(L);
        return 0;
    } else return lua_gettop(L);
}

static int do_nothing(lua_State *L) {
    (void) L;
    return 0;
}

static int global_newtry(lua_State *L) {
    lua_settop(L, 1);
    if (lua_isnil(L, 1)) lua_pushcfunction(L, do_nothing);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, -2);
    lua_pushcclosure(L, finalize, 2);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Protect factory
\*-------------------------------------------------------------------------*/
static int unwrap(lua_State *L) {
    if (lua_istable(L, -1) && lua_getmetatable(L, -1)) {
        int r = lua_rawequal(L, -1, lua_upvalueindex(1));
        lua_pop(L, 1);
        if (r) {
            lua_pushnil(L);
            lua_rawgeti(L, -2, 1);
            return 1;
        }
    }
    return 0;
}

static int protected_finish(lua_State *L, int status, lua_KContext ctx) {
    (void)ctx;
    if (status != 0 && status != LUA_YIELD) {
        if (unwrap(L)) return 2;
        else return lua_error(L);
    } else return lua_gettop(L);
}

#if LUA_VERSION_NUM == 502
static int protected_cont(lua_State *L) {
    int ctx = 0;
    int status = lua_getctx(L, &ctx);
    return protected_finish(L, status, ctx);
}
#else
#define protected_cont protected_finish
#endif

static int protected_(lua_State *L) {
    int status;
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    status = lua_pcallk(L, lua_gettop(L) - 1, LUA_MULTRET, 0, 0, protected_cont);
    return protected_finish(L, status, 0);
}

static int global_protect(lua_State *L) {
    lua_settop(L, 1);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_pushcclosure(L, protected_, 2);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Init module
\*-------------------------------------------------------------------------*/
int except_open(lua_State *L) {
    lua_newtable(L); /* metatable for wrapped exceptions */
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
    luaL_setfuncs(L, excptfunc, 1);
    return 0;
}

/*=========================================================================*\
* Internet domain functions
* LuaSocket toolkit
\*=========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
//#include "lauxlib.h"



/*=========================================================================*\
* Internal function prototypes.
\*=========================================================================*/
static int inet_global_toip(lua_State *L);
static int inet_global_getaddrinfo(lua_State *L);
static int inet_global_tohostname(lua_State *L);
static int inet_global_getnameinfo(lua_State *L);
static void inet_pushresolved(lua_State *L, struct hostent *hp);
static int inet_global_gethostname(lua_State *L);

/* DNS functions */
static luaL_Reg inetfunc[] = {
    { "toip", inet_global_toip},
    { "getaddrinfo", inet_global_getaddrinfo},
    { "tohostname", inet_global_tohostname},
    { "getnameinfo", inet_global_getnameinfo},
    { "gethostname", inet_global_gethostname},
    { NULL, NULL}
};

/*=========================================================================*\
* Exported functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int inet_open(lua_State *L)
{
    lua_pushstring(L, "dns");
    lua_newtable(L);
    luaL_setfuncs(L, inetfunc, 0);
    lua_settable(L, -3);
    return 0;
}

/*=========================================================================*\
* Global Lua functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Returns all information provided by the resolver given a host name
* or ip address
\*-------------------------------------------------------------------------*/
static int inet_gethost(const char *address, struct hostent **hp) {
    struct in_addr addr;
    if (inet_aton(address, &addr))
        return socket_gethostbyaddr((char *) &addr, sizeof(addr), hp);
    else
        return socket_gethostbyname(address, hp);
}

/*-------------------------------------------------------------------------*\
* Returns all information provided by the resolver given a host name
* or ip address
\*-------------------------------------------------------------------------*/
static int inet_global_tohostname(lua_State *L) {
    const char *address = luaL_checkstring(L, 1);
    struct hostent *hp = NULL;
    int err = inet_gethost(address, &hp);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, socket_hoststrerror(err));
        return 2;
    }
    lua_pushstring(L, hp->h_name);
    inet_pushresolved(L, hp);
    return 2;
}

static int inet_global_getnameinfo(lua_State *L) {
    char hbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];
    int i, ret;
    struct addrinfo hints;
    struct addrinfo *resolved, *iter;
    const char *host = luaL_optstring(L, 1, NULL);
    const char *serv = luaL_optstring(L, 2, NULL);

    if (!(host || serv))
        luaL_error(L, "host and serv cannot be both nil");

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    ret = getaddrinfo(host, serv, &hints, &resolved);
    if (ret != 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_gaistrerror(ret));
        return 2;
    }

    lua_newtable(L);
    for (i = 1, iter = resolved; iter; i++, iter = iter->ai_next) {
        getnameinfo(iter->ai_addr, (socklen_t) iter->ai_addrlen,
            hbuf, host? (socklen_t) sizeof(hbuf): 0,
            sbuf, serv? (socklen_t) sizeof(sbuf): 0, 0);
        if (host) {
            lua_pushnumber(L, i);
            lua_pushstring(L, hbuf);
            lua_settable(L, -3);
        }
    }
    freeaddrinfo(resolved);

    if (serv) {
        lua_pushstring(L, sbuf);
        return 2;
    } else {
        return 1;
    }
}

/*-------------------------------------------------------------------------*\
* Returns all information provided by the resolver given a host name
* or ip address
\*-------------------------------------------------------------------------*/
static int inet_global_toip(lua_State *L)
{
    const char *address = luaL_checkstring(L, 1);
    struct hostent *hp = NULL;
    int err = inet_gethost(address, &hp);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, socket_hoststrerror(err));
        return 2;
    }
    lua_pushstring(L, inet_ntoa(*((struct in_addr *) hp->h_addr)));
    inet_pushresolved(L, hp);
    return 2;
}

int inet_optfamily(lua_State* L, int narg, const char* def)
{
    static const char* optname[] = { "unspec", "inet", "inet6", NULL };
    static int optvalue[] = { AF_UNSPEC, AF_INET, AF_INET6, 0 };

    return optvalue[luaL_checkoption(L, narg, def, optname)];
}

int inet_optsocktype(lua_State* L, int narg, const char* def)
{
    static const char* optname[] = { "stream", "dgram", NULL };
    static int optvalue[] = { SOCK_STREAM, SOCK_DGRAM, 0 };

    return optvalue[luaL_checkoption(L, narg, def, optname)];
}

static int inet_global_getaddrinfo(lua_State *L)
{
    const char *hostname = luaL_checkstring(L, 1);
    struct addrinfo *iterator = NULL, *resolved = NULL;
    struct addrinfo hints;
    int i = 1, ret = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    ret = getaddrinfo(hostname, NULL, &hints, &resolved);
    if (ret != 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_gaistrerror(ret));
        return 2;
    }
    lua_newtable(L);
    for (iterator = resolved; iterator; iterator = iterator->ai_next) {
        char hbuf[NI_MAXHOST];
        ret = getnameinfo(iterator->ai_addr, (socklen_t) iterator->ai_addrlen,
            hbuf, (socklen_t) sizeof(hbuf), NULL, 0, NI_NUMERICHOST);
        if (ret){
          freeaddrinfo(resolved);
          lua_pushnil(L);
          lua_pushstring(L, socket_gaistrerror(ret));
          return 2;
        }
        lua_pushnumber(L, i);
        lua_newtable(L);
        switch (iterator->ai_family) {
            case AF_INET:
                lua_pushliteral(L, "family");
                lua_pushliteral(L, "inet");
                lua_settable(L, -3);
                break;
            case AF_INET6:
                lua_pushliteral(L, "family");
                lua_pushliteral(L, "inet6");
                lua_settable(L, -3);
                break;
            case AF_UNSPEC:
                lua_pushliteral(L, "family");
                lua_pushliteral(L, "unspec");
                lua_settable(L, -3);
                break;
            default:
                lua_pushliteral(L, "family");
                lua_pushliteral(L, "unknown");
                lua_settable(L, -3);
                break;
        }
        lua_pushliteral(L, "addr");
        lua_pushstring(L, hbuf);
        lua_settable(L, -3);
        lua_settable(L, -3);
        i++;
    }
    freeaddrinfo(resolved);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Gets the host name
\*-------------------------------------------------------------------------*/
static int inet_global_gethostname(lua_State *L)
{
    char name[257];
    name[256] = '\0';
    if (gethostname(name, 256) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
        return 2;
    } else {
        lua_pushstring(L, name);
        return 1;
    }
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Retrieves socket peer name
\*-------------------------------------------------------------------------*/
int inet_meth_getpeername(lua_State *L, p_socket ps, int family)
{
    int err;
    struct sockaddr_storage peer;
    socklen_t peer_len = sizeof(peer);
    char name[INET6_ADDRSTRLEN];
    char port[6]; /* 65535 = 5 bytes + 0 to terminate it */
    if (getpeername(*ps, (SA *) &peer, &peer_len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
        return 2;
    }
	err = getnameinfo((struct sockaddr *) &peer, peer_len,
        name, INET6_ADDRSTRLEN,
        port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, gai_strerror(err));
        return 2;
    }
    lua_pushstring(L, name);
    lua_pushinteger(L, (int) strtol(port, (char **) NULL, 10));
    switch (family) {
        case AF_INET: lua_pushliteral(L, "inet"); break;
        case AF_INET6: lua_pushliteral(L, "inet6"); break;
        case AF_UNSPEC: lua_pushliteral(L, "unspec"); break;
        default: lua_pushliteral(L, "unknown"); break;
    }
    return 3;
}

/*-------------------------------------------------------------------------*\
* Retrieves socket local name
\*-------------------------------------------------------------------------*/
int inet_meth_getsockname(lua_State *L, p_socket ps, int family)
{
    int err;
    struct sockaddr_storage peer;
    socklen_t peer_len = sizeof(peer);
    char name[INET6_ADDRSTRLEN];
    char port[6]; /* 65535 = 5 bytes + 0 to terminate it */
    if (getsockname(*ps, (SA *) &peer, &peer_len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
        return 2;
    }
	err=getnameinfo((struct sockaddr *)&peer, peer_len,
		name, INET6_ADDRSTRLEN, port, 6, NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, gai_strerror(err));
        return 2;
    }
    lua_pushstring(L, name);
    lua_pushstring(L, port);
    switch (family) {
        case AF_INET: lua_pushliteral(L, "inet"); break;
        case AF_INET6: lua_pushliteral(L, "inet6"); break;
        case AF_UNSPEC: lua_pushliteral(L, "unspec"); break;
        default: lua_pushliteral(L, "unknown"); break;
    }
    return 3;
}

/*=========================================================================*\
* Internal functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Passes all resolver information to Lua as a table
\*-------------------------------------------------------------------------*/
static void inet_pushresolved(lua_State *L, struct hostent *hp)
{
    char **alias;
    struct in_addr **addr;
    int i, resolved;
    lua_newtable(L); resolved = lua_gettop(L);
    lua_pushstring(L, "name");
    lua_pushstring(L, hp->h_name);
    lua_settable(L, resolved);
    lua_pushstring(L, "ip");
    lua_pushstring(L, "alias");
    i = 1;
    alias = hp->h_aliases;
    lua_newtable(L);
    if (alias) {
        while (*alias) {
            lua_pushnumber(L, i);
            lua_pushstring(L, *alias);
            lua_settable(L, -3);
            i++; alias++;
        }
    }
    lua_settable(L, resolved);
    i = 1;
    lua_newtable(L);
    addr = (struct in_addr **) hp->h_addr_list;
    if (addr) {
        while (*addr) {
            lua_pushnumber(L, i);
            lua_pushstring(L, inet_ntoa(**addr));
            lua_settable(L, -3);
            i++; addr++;
        }
    }
    lua_settable(L, resolved);
}

/*-------------------------------------------------------------------------*\
* Tries to create a new inet socket
\*-------------------------------------------------------------------------*/
const char *inet_trycreate(p_socket ps, int family, int type, int protocol) {
    const char *err = socket_strerror(socket_create(ps, family, type, protocol));
    if (err == NULL && family == AF_INET6) {
        int yes = 1;
        setsockopt(*ps, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&yes, sizeof(yes));
    }
    return err;
}

/*-------------------------------------------------------------------------*\
* "Disconnects" a DGRAM socket
\*-------------------------------------------------------------------------*/
const char *inet_trydisconnect(p_socket ps, int family, p_timeout tm)
{
    switch (family) {
        case AF_INET: {
            struct sockaddr_in sin;
            memset((char *) &sin, 0, sizeof(sin));
            sin.sin_family = AF_UNSPEC;
            sin.sin_addr.s_addr = INADDR_ANY;
            return socket_strerror(socket_connect(ps, (SA *) &sin,
                sizeof(sin), tm));
        }
        case AF_INET6: {
            struct sockaddr_in6 sin6;
            struct in6_addr addrany = IN6ADDR_ANY_INIT;
            memset((char *) &sin6, 0, sizeof(sin6));
            sin6.sin6_family = AF_UNSPEC;
            sin6.sin6_addr = addrany;
            return socket_strerror(socket_connect(ps, (SA *) &sin6,
                sizeof(sin6), tm));
        }
    }
    return NULL;
}

/*-------------------------------------------------------------------------*\
* Tries to connect to remote address (address, port)
\*-------------------------------------------------------------------------*/
const char *inet_tryconnect(p_socket ps, int *family, const char *address,
        const char *serv, p_timeout tm, struct addrinfo *connecthints)
{
    struct addrinfo *iterator = NULL, *resolved = NULL;
    const char *err = NULL;
    int current_family = *family;
    /* try resolving */
    err = socket_gaistrerror(getaddrinfo(address, serv,
                connecthints, &resolved));
    if (err != NULL) {
        if (resolved) freeaddrinfo(resolved);
        return err;
    }
    for (iterator = resolved; iterator; iterator = iterator->ai_next) {
        timeout_markstart(tm);
        /* create new socket if necessary. if there was no
         * bind, we need to create one for every new family
         * that shows up while iterating. if there was a
         * bind, all families will be the same and we will
         * not enter this branch. */
        if (current_family != iterator->ai_family || *ps == SOCKET_INVALID) {
            socket_destroy(ps);
            err = inet_trycreate(ps, iterator->ai_family,
                iterator->ai_socktype, iterator->ai_protocol);
            if (err) continue;
            current_family = iterator->ai_family;
            /* set non-blocking before connect */
            socket_setnonblocking(ps);
        }
        /* try connecting to remote address */
        err = socket_strerror(socket_connect(ps, (SA *) iterator->ai_addr,
            (socklen_t) iterator->ai_addrlen, tm));
        /* if success or timeout is zero, break out of loop */
        if (err == NULL || timeout_iszero(tm)) {
            *family = current_family;
            break;
        }
    }
    freeaddrinfo(resolved);
    /* here, if err is set, we failed */
    return err;
}

/*-------------------------------------------------------------------------*\
* Tries to accept a socket
\*-------------------------------------------------------------------------*/
const char *inet_tryaccept(p_socket server, int family, p_socket client,
    p_timeout tm) {
	socklen_t len;
	t_sockaddr_storage addr;
    switch (family) {
        case AF_INET6: len = sizeof(struct sockaddr_in6); break;
        case AF_INET: len = sizeof(struct sockaddr_in); break;
        default: len = sizeof(addr); break;
    }
	return socket_strerror(socket_accept(server, client, (SA *) &addr,
        &len, tm));
}

/*-------------------------------------------------------------------------*\
* Tries to bind socket to (address, port)
\*-------------------------------------------------------------------------*/
const char *inet_trybind(p_socket ps, int *family, const char *address,
    const char *serv, struct addrinfo *bindhints) {
    struct addrinfo *iterator = NULL, *resolved = NULL;
    const char *err = NULL;
    int current_family = *family;
    /* translate luasocket special values to C */
    if (strcmp(address, "*") == 0) address = NULL;
    if (!serv) serv = "0";
    /* try resolving */
    err = socket_gaistrerror(getaddrinfo(address, serv, bindhints, &resolved));
    if (err) {
        if (resolved) freeaddrinfo(resolved);
        return err;
    }
    /* iterate over resolved addresses until one is good */
    for (iterator = resolved; iterator; iterator = iterator->ai_next) {
        if (current_family != iterator->ai_family || *ps == SOCKET_INVALID) {
            socket_destroy(ps);
            err = inet_trycreate(ps, iterator->ai_family,
                        iterator->ai_socktype, iterator->ai_protocol);
            if (err) continue;
            current_family = iterator->ai_family;
        }
        /* try binding to local address */
        err = socket_strerror(socket_bind(ps, (SA *) iterator->ai_addr,
            (socklen_t) iterator->ai_addrlen));
        /* keep trying unless bind succeeded */
        if (err == NULL) {
            *family = current_family;
            /* set to non-blocking after bind */
            socket_setnonblocking(ps);
            break;
        }
    }
    /* cleanup and return error */
    freeaddrinfo(resolved);
    /* here, if err is set, we failed */
    return err;
}

/*-------------------------------------------------------------------------*\
* Some systems do not provide these so that we provide our own.
\*-------------------------------------------------------------------------*/
#ifdef LUASOCKET_INET_ATON
int inet_aton(const char *cp, struct in_addr *inp)
{
    unsigned int a = 0, b = 0, c = 0, d = 0;
    int n = 0, r;
    unsigned long int addr = 0;
    r = sscanf(cp, "%u.%u.%u.%u%n", &a, &b, &c, &d, &n);
    if (r == 0 || n == 0) return 0;
    cp += n;
    if (*cp) return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    if (inp) {
        addr += a; addr <<= 8;
        addr += b; addr <<= 8;
        addr += c; addr <<= 8;
        addr += d;
        inp->s_addr = htonl(addr);
    }
    return 1;
}
#endif

#ifdef LUASOCKET_INET_PTON
int inet_pton(int af, const char *src, void *dst)
{
    struct addrinfo hints, *res;
    int ret = 1;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;
    hints.ai_flags = AI_NUMERICHOST;
    if (getaddrinfo(src, NULL, &hints, &res) != 0) return -1;
    if (af == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *) res->ai_addr;
        memcpy(dst, &in->sin_addr, sizeof(in->sin_addr));
    } else if (af == AF_INET6) {
        struct sockaddr_in6 *in = (struct sockaddr_in6 *) res->ai_addr;
        memcpy(dst, &in->sin6_addr, sizeof(in->sin6_addr));
    } else {
        ret = -1;
    }
    freeaddrinfo(res);
    return ret;
}

#endif

/*=========================================================================*\
* Input/Output abstraction
* LuaSocket toolkit
\*=========================================================================*/

/*=========================================================================*\
* Exported functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Initializes C structure
\*-------------------------------------------------------------------------*/
void io_init(p_io io, p_send send, p_recv recv, p_error error, void *ctx) {
    io->send = send;
    io->recv = recv;
    io->error = error;
    io->ctx = ctx;
}

/*-------------------------------------------------------------------------*\
* I/O error strings
\*-------------------------------------------------------------------------*/
const char *io_strerror(int err) {
    switch (err) {
        case IO_DONE: return NULL;
        case IO_CLOSED: return "closed";
        case IO_TIMEOUT: return "timeout";
        default: return "unknown error";
    }
}

/*=========================================================================*\
* LuaSocket toolkit
* Networking support for the Lua language
* Diego Nehab
* 26/11/1999
*
* This library is part of an  effort to progressively increase the network
* connectivity  of  the Lua  language.  The  Lua interface  to  networking
* functions follows the Sockets API  closely, trying to simplify all tasks
* involved in setting up both  client and server connections. The provided
* IO routines, however, follow the Lua  style, being very similar  to the
* standard Lua read and write functions.
\*=========================================================================*/

/*=========================================================================*\
* Standard include files
\*=========================================================================*/
#include "lua.h"
//#include "lauxlib.h"

/*=========================================================================*\
* LuaSocket includes
\*=========================================================================*/

/*-------------------------------------------------------------------------*\
* Internal function prototypes
\*-------------------------------------------------------------------------*/
static int global_skip(lua_State *L);
static int global_unload(lua_State *L);
static int base_open(lua_State *L);

/*-------------------------------------------------------------------------*\
* Modules and functions
\*-------------------------------------------------------------------------*/
static const luaL_Reg socketmod[] = {
    {"auxiliar", auxiliar_open},
    {"except", except_open},
    {"timeout", timeout_open},
    {"buffer", buffer_open},
    {"inet", inet_open},
    {"tcp", tcp_open},
    {"udp", udp_open},
    {"select", select_open},
    {NULL, NULL}
};

static luaL_Reg socketfunc[] = {
    {"skip",      global_skip},
    {"__unload",  global_unload},
    {NULL,        NULL}
};

/*-------------------------------------------------------------------------*\
* Skip a few arguments
\*-------------------------------------------------------------------------*/
static int global_skip(lua_State *L) {
    int amount = luaL_checkinteger(L, 1);
    int ret = lua_gettop(L) - amount - 1;
    return ret >= 0 ? ret : 0;
}

/*-------------------------------------------------------------------------*\
* Unloads the library
\*-------------------------------------------------------------------------*/
static int global_unload(lua_State *L) {
    (void) L;
    socket_close();
    return 0;
}

/*-------------------------------------------------------------------------*\
* Setup basic stuff.
\*-------------------------------------------------------------------------*/
static int base_open(lua_State *L) {
    if (socket_open()) {
        /* export functions (and leave namespace table on top of stack) */
        //lua_newtable(L);
        //luaL_setfuncs(L, socketfunc, 0);
        luaL_openlib(L, "socket", socketfunc, 0);
#ifdef LUASOCKET_DEBUG
        lua_pushstring(L, "_DEBUG");
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
#endif
        /* make version string available to scripts */
        lua_pushstring(L, "_VERSION");
        lua_pushstring(L, LUASOCKET_VERSION);
        lua_rawset(L, -3);
        return 1;
    } else {
        lua_pushstring(L, "unable to initialize library");
        lua_error(L);
        return 0;
    }
}

/*-------------------------------------------------------------------------*\
* Initializes all library modules.
\*-------------------------------------------------------------------------*/
LUASOCKET_API int luaopen_socket_core(lua_State *L) {
    int i;
    base_open(L);
    for (i = 0; socketmod[i].name; i++) socketmod[i].func(L);
    return 1;
}

/*=========================================================================*\
* MIME support functions
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>

#include "lua.h"
//#include "lauxlib.h"


/*=========================================================================*\
* Don't want to trust escape character constants
\*=========================================================================*/
typedef unsigned char UC;
static const char CRLF[] = "\r\n";
static const char EQCRLF[] = "=\r\n";

/*=========================================================================*\
* Internal function prototypes.
\*=========================================================================*/
static int mime_global_wrp(lua_State *L);
static int mime_global_b64(lua_State *L);
static int mime_global_unb64(lua_State *L);
static int mime_global_qp(lua_State *L);
static int mime_global_unqp(lua_State *L);
static int mime_global_qpwrp(lua_State *L);
static int mime_global_eol(lua_State *L);
static int mime_global_dot(lua_State *L);

static size_t dot(int c, size_t state, luaL_Buffer *buffer);
static void b64setup(UC *base);
static size_t b64encode(UC c, UC *input, size_t size, luaL_Buffer *buffer);
static size_t b64pad(const UC *input, size_t size, luaL_Buffer *buffer);
static size_t b64decode(UC c, UC *input, size_t size, luaL_Buffer *buffer);

static void qpsetup(UC *class_, UC *unbase);
static void qpquote(UC c, luaL_Buffer *buffer);
static size_t qpdecode(UC c, UC *input, size_t size, luaL_Buffer *buffer);
static size_t qpencode(UC c, UC *input, size_t size,
        const char *marker, luaL_Buffer *buffer);
static size_t qppad(UC *input, size_t size, luaL_Buffer *buffer);

/* code support functions */
static luaL_Reg mimefunc[] = {
    { "dot", mime_global_dot },
    { "b64", mime_global_b64 },
    { "eol", mime_global_eol },
    { "qp", mime_global_qp },
    { "qpwrp", mime_global_qpwrp },
    { "unb64", mime_global_unb64 },
    { "unqp", mime_global_unqp },
    { "wrp", mime_global_wrp },
    { NULL, NULL }
};

/*-------------------------------------------------------------------------*\
* Quoted-printable globals
\*-------------------------------------------------------------------------*/
static UC qpclass[256];
static UC qpbase[] = "0123456789ABCDEF";
static UC qpunbase[256];
enum {QP_PLAIN, QP_QUOTED, QP_CR, QP_IF_LAST};

/*-------------------------------------------------------------------------*\
* Base64 globals
\*-------------------------------------------------------------------------*/
static const UC b64base[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static UC b64unbase[256];

/*=========================================================================*\
* Exported functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
MIME_API int luaopen_mime_core(lua_State *L)
{
    lua_newtable(L);
    luaL_setfuncs(L, mimefunc, 0);
    /* make version string available to scripts */
    lua_pushstring(L, "_VERSION");
    lua_pushstring(L, MIME_VERSION);
    lua_rawset(L, -3);
    /* initialize lookup tables */
    qpsetup(qpclass, qpunbase);
    b64setup(b64unbase);
    return 1;
}

/*=========================================================================*\
* Global Lua functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Incrementaly breaks a string into lines. The string can have CRLF breaks.
* A, n = wrp(l, B, length)
* A is a copy of B, broken into lines of at most 'length' bytes.
* 'l' is how many bytes are left for the first line of B.
* 'n' is the number of bytes left in the last line of A.
\*-------------------------------------------------------------------------*/
static int mime_global_wrp(lua_State *L)
{
    size_t size = 0;
    int left = (int) luaL_checknumber(L, 1);
    const UC *input = (const UC *) luaL_optlstring(L, 2, NULL, &size);
    const UC *last = input + size;
    int length = (int) luaL_optnumber(L, 3, 76);
    luaL_Buffer buffer;
    /* end of input black-hole */
    if (!input) {
        /* if last line has not been terminated, add a line break */
        if (left < length) lua_pushstring(L, CRLF);
        /* otherwise, we are done */
        else lua_pushnil(L);
        lua_pushnumber(L, length);
        return 2;
    }
    luaL_buffinit(L, &buffer);
    while (input < last) {
        switch (*input) {
            case '\r':
                break;
            case '\n':
                luaL_addstring(&buffer, CRLF);
                left = length;
                break;
            default:
                if (left <= 0) {
                    left = length;
                    luaL_addstring(&buffer, CRLF);
                }
                luaL_addchar(&buffer, *input);
                left--;
                break;
        }
        input++;
    }
    luaL_pushresult(&buffer);
    lua_pushnumber(L, left);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Fill base64 decode map.
\*-------------------------------------------------------------------------*/
static void b64setup(UC *unbase)
{
    int i;
    for (i = 0; i <= 255; i++) unbase[i] = (UC) 255;
    for (i = 0; i < 64; i++) unbase[b64base[i]] = (UC) i;
    unbase['='] = 0;
}

/*-------------------------------------------------------------------------*\
* Acumulates bytes in input buffer until 3 bytes are available.
* Translate the 3 bytes into Base64 form and append to buffer.
* Returns new number of bytes in buffer.
\*-------------------------------------------------------------------------*/
static size_t b64encode(UC c, UC *input, size_t size,
        luaL_Buffer *buffer)
{
    input[size++] = c;
    if (size == 3) {
        UC code[4];
        unsigned long value = 0;
        value += input[0]; value <<= 8;
        value += input[1]; value <<= 8;
        value += input[2];
        code[3] = b64base[value & 0x3f]; value >>= 6;
        code[2] = b64base[value & 0x3f]; value >>= 6;
        code[1] = b64base[value & 0x3f]; value >>= 6;
        code[0] = b64base[value];
        luaL_addlstring(buffer, (char *) code, 4);
        size = 0;
    }
    return size;
}

/*-------------------------------------------------------------------------*\
* Encodes the Base64 last 1 or 2 bytes and adds padding '='
* Result, if any, is appended to buffer.
* Returns 0.
\*-------------------------------------------------------------------------*/
static size_t b64pad(const UC *input, size_t size,
        luaL_Buffer *buffer)
{
    unsigned long value = 0;
    UC code[4] = {'=', '=', '=', '='};
    switch (size) {
        case 1:
            value = input[0] << 4;
            code[1] = b64base[value & 0x3f]; value >>= 6;
            code[0] = b64base[value];
            luaL_addlstring(buffer, (char *) code, 4);
            break;
        case 2:
            value = input[0]; value <<= 8;
            value |= input[1]; value <<= 2;
            code[2] = b64base[value & 0x3f]; value >>= 6;
            code[1] = b64base[value & 0x3f]; value >>= 6;
            code[0] = b64base[value];
            luaL_addlstring(buffer, (char *) code, 4);
            break;
        default:
            break;
    }
    return 0;
}

/*-------------------------------------------------------------------------*\
* Acumulates bytes in input buffer until 4 bytes are available.
* Translate the 4 bytes from Base64 form and append to buffer.
* Returns new number of bytes in buffer.
\*-------------------------------------------------------------------------*/
static size_t b64decode(UC c, UC *input, size_t size,
        luaL_Buffer *buffer)
{
    /* ignore invalid characters */
    if (b64unbase[c] > 64) return size;
    input[size++] = c;
    /* decode atom */
    if (size == 4) {
        UC decoded[3];
        int valid, value = 0;
        value =  b64unbase[input[0]]; value <<= 6;
        value |= b64unbase[input[1]]; value <<= 6;
        value |= b64unbase[input[2]]; value <<= 6;
        value |= b64unbase[input[3]];
        decoded[2] = (UC) (value & 0xff); value >>= 8;
        decoded[1] = (UC) (value & 0xff); value >>= 8;
        decoded[0] = (UC) value;
        /* take care of paddding */
        valid = (input[2] == '=') ? 1 : (input[3] == '=') ? 2 : 3;
        luaL_addlstring(buffer, (char *) decoded, valid);
        return 0;
    /* need more data */
    } else return size;
}

/*-------------------------------------------------------------------------*\
* Incrementally applies the Base64 transfer content encoding to a string
* A, B = b64(C, D)
* A is the encoded version of the largest prefix of C .. D that is
* divisible by 3. B has the remaining bytes of C .. D, *without* encoding.
* The easiest thing would be to concatenate the two strings and
* encode the result, but we can't afford that or Lua would dupplicate
* every chunk we received.
\*-------------------------------------------------------------------------*/
static int mime_global_b64(lua_State *L)
{
    UC atom[3];
    size_t isize = 0, asize = 0;
    const UC *input = (const UC *) luaL_optlstring(L, 1, NULL, &isize);
    const UC *last = input + isize;
    luaL_Buffer buffer;
    /* end-of-input blackhole */
    if (!input) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* make sure we don't confuse buffer stuff with arguments */
    lua_settop(L, 2);
    /* process first part of the input */
    luaL_buffinit(L, &buffer);
    while (input < last)
        asize = b64encode(*input++, atom, asize, &buffer);
    input = (const UC *) luaL_optlstring(L, 2, NULL, &isize);
    /* if second part is nil, we are done */
    if (!input) {
        size_t osize = 0;
        asize = b64pad(atom, asize, &buffer);
        luaL_pushresult(&buffer);
        /* if the output is empty  and the input is nil, return nil */
        lua_tolstring(L, -1, &osize);
        if (osize == 0) lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* otherwise process the second part */
    last = input + isize;
    while (input < last)
        asize = b64encode(*input++, atom, asize, &buffer);
    luaL_pushresult(&buffer);
    lua_pushlstring(L, (char *) atom, asize);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Incrementally removes the Base64 transfer content encoding from a string
* A, B = b64(C, D)
* A is the encoded version of the largest prefix of C .. D that is
* divisible by 4. B has the remaining bytes of C .. D, *without* encoding.
\*-------------------------------------------------------------------------*/
static int mime_global_unb64(lua_State *L)
{
    UC atom[4];
    size_t isize = 0, asize = 0;
    const UC *input = (const UC *) luaL_optlstring(L, 1, NULL, &isize);
    const UC *last = input + isize;
    luaL_Buffer buffer;
    /* end-of-input blackhole */
    if (!input) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* make sure we don't confuse buffer stuff with arguments */
    lua_settop(L, 2);
    /* process first part of the input */
    luaL_buffinit(L, &buffer);
    while (input < last)
        asize = b64decode(*input++, atom, asize, &buffer);
    input = (const UC *) luaL_optlstring(L, 2, NULL, &isize);
    /* if second is nil, we are done */
    if (!input) {
        size_t osize = 0;
        luaL_pushresult(&buffer);
        /* if the output is empty  and the input is nil, return nil */
        lua_tolstring(L, -1, &osize);
        if (osize == 0) lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* otherwise, process the rest of the input */
    last = input + isize;
    while (input < last)
        asize = b64decode(*input++, atom, asize, &buffer);
    luaL_pushresult(&buffer);
    lua_pushlstring(L, (char *) atom, asize);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Quoted-printable encoding scheme
* all (except CRLF in text) can be =XX
* CLRL in not text must be =XX=XX
* 33 through 60 inclusive can be plain
* 62 through 126 inclusive can be plain
* 9 and 32 can be plain, unless in the end of a line, where must be =XX
* encoded lines must be no longer than 76 not counting CRLF
* soft line-break are =CRLF
* To encode one byte, we need to see the next two.
* Worst case is when we see a space, and wonder if a CRLF is comming
\*-------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*\
* Split quoted-printable characters into classes
* Precompute reverse map for encoding
\*-------------------------------------------------------------------------*/
static void qpsetup(UC *cl, UC *unbase)
{
    int i;
    for (i = 0; i < 256; i++) cl[i] = QP_QUOTED;
    for (i = 33; i <= 60; i++) cl[i] = QP_PLAIN;
    for (i = 62; i <= 126; i++) cl[i] = QP_PLAIN;
    cl['\t'] = QP_IF_LAST;
    cl[' '] = QP_IF_LAST;
    cl['\r'] = QP_CR;
    for (i = 0; i < 256; i++) unbase[i] = 255;
    unbase['0'] = 0; unbase['1'] = 1; unbase['2'] = 2;
    unbase['3'] = 3; unbase['4'] = 4; unbase['5'] = 5;
    unbase['6'] = 6; unbase['7'] = 7; unbase['8'] = 8;
    unbase['9'] = 9; unbase['A'] = 10; unbase['a'] = 10;
    unbase['B'] = 11; unbase['b'] = 11; unbase['C'] = 12;
    unbase['c'] = 12; unbase['D'] = 13; unbase['d'] = 13;
    unbase['E'] = 14; unbase['e'] = 14; unbase['F'] = 15;
    unbase['f'] = 15;
}

/*-------------------------------------------------------------------------*\
* Output one character in form =XX
\*-------------------------------------------------------------------------*/
static void qpquote(UC c, luaL_Buffer *buffer)
{
    luaL_addchar(buffer, '=');
    luaL_addchar(buffer, qpbase[c >> 4]);
    luaL_addchar(buffer, qpbase[c & 0x0F]);
}

/*-------------------------------------------------------------------------*\
* Accumulate characters until we are sure about how to deal with them.
* Once we are sure, output to the buffer, in the correct form.
\*-------------------------------------------------------------------------*/
static size_t qpencode(UC c, UC *input, size_t size,
        const char *marker, luaL_Buffer *buffer)
{
    input[size++] = c;
    /* deal with all characters we can have */
    while (size > 0) {
        switch (qpclass[input[0]]) {
            /* might be the CR of a CRLF sequence */
            case QP_CR:
                if (size < 2) return size;
                if (input[1] == '\n') {
                    luaL_addstring(buffer, marker);
                    return 0;
                } else qpquote(input[0], buffer);
                break;
            /* might be a space and that has to be quoted if last in line */
            case QP_IF_LAST:
                if (size < 3) return size;
                /* if it is the last, quote it and we are done */
                if (input[1] == '\r' && input[2] == '\n') {
                    qpquote(input[0], buffer);
                    luaL_addstring(buffer, marker);
                    return 0;
                } else luaL_addchar(buffer, input[0]);
                break;
                /* might have to be quoted always */
            case QP_QUOTED:
                qpquote(input[0], buffer);
                break;
                /* might never have to be quoted */
            default:
                luaL_addchar(buffer, input[0]);
                break;
        }
        input[0] = input[1]; input[1] = input[2];
        size--;
    }
    return 0;
}

/*-------------------------------------------------------------------------*\
* Deal with the final characters
\*-------------------------------------------------------------------------*/
static size_t qppad(UC *input, size_t size, luaL_Buffer *buffer)
{
    size_t i;
    for (i = 0; i < size; i++) {
        if (qpclass[input[i]] == QP_PLAIN) luaL_addchar(buffer, input[i]);
        else qpquote(input[i], buffer);
    }
    if (size > 0) luaL_addstring(buffer, EQCRLF);
    return 0;
}

/*-------------------------------------------------------------------------*\
* Incrementally converts a string to quoted-printable
* A, B = qp(C, D, marker)
* Marker is the text to be used to replace CRLF sequences found in A.
* A is the encoded version of the largest prefix of C .. D that
* can be encoded without doubts.
* B has the remaining bytes of C .. D, *without* encoding.
\*-------------------------------------------------------------------------*/
static int mime_global_qp(lua_State *L)
{

    size_t asize = 0, isize = 0;
    UC atom[3];
    const UC *input = (const UC *) luaL_optlstring(L, 1, NULL, &isize);
    const UC *last = input + isize;
    const char *marker = luaL_optstring(L, 3, CRLF);
    luaL_Buffer buffer;
    /* end-of-input blackhole */
    if (!input) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* make sure we don't confuse buffer stuff with arguments */
    lua_settop(L, 3);
    /* process first part of input */
    luaL_buffinit(L, &buffer);
    while (input < last)
        asize = qpencode(*input++, atom, asize, marker, &buffer);
    input = (const UC *) luaL_optlstring(L, 2, NULL, &isize);
    /* if second part is nil, we are done */
    if (!input) {
        asize = qppad(atom, asize, &buffer);
        luaL_pushresult(&buffer);
        if (!(*lua_tostring(L, -1))) lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* otherwise process rest of input */
    last = input + isize;
    while (input < last)
        asize = qpencode(*input++, atom, asize, marker, &buffer);
    luaL_pushresult(&buffer);
    lua_pushlstring(L, (char *) atom, asize);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Accumulate characters until we are sure about how to deal with them.
* Once we are sure, output the to the buffer, in the correct form.
\*-------------------------------------------------------------------------*/
static size_t qpdecode(UC c, UC *input, size_t size, luaL_Buffer *buffer) {
    int d;
    input[size++] = c;
    /* deal with all characters we can deal */
    switch (input[0]) {
        /* if we have an escape character */
        case '=':
            if (size < 3) return size;
            /* eliminate soft line break */
            if (input[1] == '\r' && input[2] == '\n') return 0;
            /* decode quoted representation */
            c = qpunbase[input[1]]; d = qpunbase[input[2]];
            /* if it is an invalid, do not decode */
            if (c > 15 || d > 15) luaL_addlstring(buffer, (char *)input, 3);
            else luaL_addchar(buffer, (char) ((c << 4) + d));
            return 0;
        case '\r':
            if (size < 2) return size;
            if (input[1] == '\n') luaL_addlstring(buffer, (char *)input, 2);
            return 0;
        default:
            if (input[0] == '\t' || (input[0] > 31 && input[0] < 127))
                luaL_addchar(buffer, input[0]);
            return 0;
    }
}

/*-------------------------------------------------------------------------*\
* Incrementally decodes a string in quoted-printable
* A, B = qp(C, D)
* A is the decoded version of the largest prefix of C .. D that
* can be decoded without doubts.
* B has the remaining bytes of C .. D, *without* decoding.
\*-------------------------------------------------------------------------*/
static int mime_global_unqp(lua_State *L)
{
    size_t asize = 0, isize = 0;
    UC atom[3];
    const UC *input = (const UC *) luaL_optlstring(L, 1, NULL, &isize);
    const UC *last = input + isize;
    luaL_Buffer buffer;
    /* end-of-input blackhole */
    if (!input) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* make sure we don't confuse buffer stuff with arguments */
    lua_settop(L, 2);
    /* process first part of input */
    luaL_buffinit(L, &buffer);
    while (input < last)
        asize = qpdecode(*input++, atom, asize, &buffer);
    input = (const UC *) luaL_optlstring(L, 2, NULL, &isize);
    /* if second part is nil, we are done */
    if (!input) {
        luaL_pushresult(&buffer);
        if (!(*lua_tostring(L, -1))) lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
    /* otherwise process rest of input */
    last = input + isize;
    while (input < last)
        asize = qpdecode(*input++, atom, asize, &buffer);
    luaL_pushresult(&buffer);
    lua_pushlstring(L, (char *) atom, asize);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Incrementally breaks a quoted-printed string into lines
* A, n = qpwrp(l, B, length)
* A is a copy of B, broken into lines of at most 'length' bytes.
* 'l' is how many bytes are left for the first line of B.
* 'n' is the number of bytes left in the last line of A.
* There are two complications: lines can't be broken in the middle
* of an encoded =XX, and there might be line breaks already
\*-------------------------------------------------------------------------*/
static int mime_global_qpwrp(lua_State *L)
{
    size_t size = 0;
    int left = (int) luaL_checknumber(L, 1);
    const UC *input = (const UC *) luaL_optlstring(L, 2, NULL, &size);
    const UC *last = input + size;
    int length = (int) luaL_optnumber(L, 3, 76);
    luaL_Buffer buffer;
    /* end-of-input blackhole */
    if (!input) {
        if (left < length) lua_pushstring(L, EQCRLF);
        else lua_pushnil(L);
        lua_pushnumber(L, length);
        return 2;
    }
    /* process all input */
    luaL_buffinit(L, &buffer);
    while (input < last) {
        switch (*input) {
            case '\r':
                break;
            case '\n':
                left = length;
                luaL_addstring(&buffer, CRLF);
                break;
            case '=':
                if (left <= 3) {
                    left = length;
                    luaL_addstring(&buffer, EQCRLF);
                }
                luaL_addchar(&buffer, *input);
                left--;
                break;
            default:
                if (left <= 1) {
                    left = length;
                    luaL_addstring(&buffer, EQCRLF);
                }
                luaL_addchar(&buffer, *input);
                left--;
                break;
        }
        input++;
    }
    luaL_pushresult(&buffer);
    lua_pushnumber(L, left);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Here is what we do: \n, and \r are considered candidates for line
* break. We issue *one* new line marker if any of them is seen alone, or
* followed by a different one. That is, \n\n and \r\r will issue two
* end of line markers each, but \r\n, \n\r etc will only issue *one*
* marker.  This covers Mac OS, Mac OS X, VMS, Unix and DOS, as well as
* probably other more obscure conventions.
*
* c is the current character being processed
* last is the previous character
\*-------------------------------------------------------------------------*/
#define eolcandidate(c) (c == '\r' || c == '\n')
static int eolprocess(int c, int last, const char *marker,
        luaL_Buffer *buffer)
{
    if (eolcandidate(c)) {
        if (eolcandidate(last)) {
            if (c == last) luaL_addstring(buffer, marker);
            return 0;
        } else {
            luaL_addstring(buffer, marker);
            return c;
        }
    } else {
        luaL_addchar(buffer, (char) c);
        return 0;
    }
}

/*-------------------------------------------------------------------------*\
* Converts a string to uniform EOL convention.
* A, n = eol(o, B, marker)
* A is the converted version of the largest prefix of B that can be
* converted unambiguously. 'o' is the context returned by the previous
* call. 'n' is the new context.
\*-------------------------------------------------------------------------*/
static int mime_global_eol(lua_State *L)
{
    int ctx = luaL_checkinteger(L, 1);
    size_t isize = 0;
    const char *input = luaL_optlstring(L, 2, NULL, &isize);
    const char *last = input + isize;
    const char *marker = luaL_optstring(L, 3, CRLF);
    luaL_Buffer buffer;
    luaL_buffinit(L, &buffer);
    /* end of input blackhole */
    if (!input) {
       lua_pushnil(L);
       lua_pushnumber(L, 0);
       return 2;
    }
    /* process all input */
    while (input < last)
        ctx = eolprocess(*input++, ctx, marker, &buffer);
    luaL_pushresult(&buffer);
    lua_pushnumber(L, ctx);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Takes one byte and stuff it if needed.
\*-------------------------------------------------------------------------*/
static size_t dot(int c, size_t state, luaL_Buffer *buffer)
{
    luaL_addchar(buffer, (char) c);
    switch (c) {
        case '\r':
            return 1;
        case '\n':
            return (state == 1)? 2: 0;
        case '.':
            if (state == 2)
                luaL_addchar(buffer, '.');
        default:
            return 0;
    }
}

/*-------------------------------------------------------------------------*\
* Incrementally applies smtp stuffing to a string
* A, n = dot(l, D)
\*-------------------------------------------------------------------------*/
static int mime_global_dot(lua_State *L)
{
    size_t isize = 0, state = (size_t) luaL_checknumber(L, 1);
    const char *input = luaL_optlstring(L, 2, NULL, &isize);
    const char *last = input + isize;
    luaL_Buffer buffer;
    /* end-of-input blackhole */
    if (!input) {
        lua_pushnil(L);
        lua_pushnumber(L, 2);
        return 2;
    }
    /* process all input */
    luaL_buffinit(L, &buffer);
    while (input < last)
        state = dot(*input++, state, &buffer);
    luaL_pushresult(&buffer);
    lua_pushnumber(L, (lua_Number) state);
    return 2;
}


/*=========================================================================*\
* Common option interface
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>

//#include "lauxlib.h"



/*=========================================================================*\
* Internal functions prototypes
\*=========================================================================*/
static int opt_setmembership(lua_State *L, p_socket ps, int level, int name);
static int opt_ip6_setmembership(lua_State *L, p_socket ps, int level, int name);
static int opt_setboolean(lua_State *L, p_socket ps, int level, int name);
static int opt_getboolean(lua_State *L, p_socket ps, int level, int name);
static int opt_setint(lua_State *L, p_socket ps, int level, int name);
static int opt_getint(lua_State *L, p_socket ps, int level, int name);
static int opt_set(lua_State *L, p_socket ps, int level, int name,
        void *val, int len);
static int opt_get(lua_State *L, p_socket ps, int level, int name,
        void *val, int* len);

/*=========================================================================*\
* Exported functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Calls appropriate option handler
\*-------------------------------------------------------------------------*/
int opt_meth_setoption(lua_State *L, p_opt opt, p_socket ps)
{
    const char *name = luaL_checkstring(L, 2);      /* obj, name, ... */
    while (opt->name && strcmp(name, opt->name))
        opt++;
    if (!opt->func) {
        char msg[45];
        sprintf(msg, "unsupported option `%.35s'", name);
        luaL_argerror(L, 2, msg);
    }
    return opt->func(L, ps);
}

int opt_meth_getoption(lua_State *L, p_opt opt, p_socket ps)
{
    const char *name = luaL_checkstring(L, 2);      /* obj, name, ... */
    while (opt->name && strcmp(name, opt->name))
        opt++;
    if (!opt->func) {
        char msg[45];
        sprintf(msg, "unsupported option `%.35s'", name);
        luaL_argerror(L, 2, msg);
    }
    return opt->func(L, ps);
}

/* enables reuse of local address */
int opt_set_reuseaddr(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, SOL_SOCKET, SO_REUSEADDR);
}

int opt_get_reuseaddr(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, SOL_SOCKET, SO_REUSEADDR);
}

/* enables reuse of local port */
int opt_set_reuseport(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, SOL_SOCKET, SO_REUSEPORT);
}

int opt_get_reuseport(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, SOL_SOCKET, SO_REUSEPORT);
}

/* disables the Naggle algorithm */
int opt_set_tcp_nodelay(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, IPPROTO_TCP, TCP_NODELAY);
}

int opt_get_tcp_nodelay(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, IPPROTO_TCP, TCP_NODELAY);
}

int opt_set_keepalive(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, SOL_SOCKET, SO_KEEPALIVE);
}

int opt_get_keepalive(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, SOL_SOCKET, SO_KEEPALIVE);
}

int opt_set_dontroute(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, SOL_SOCKET, SO_DONTROUTE);
}

int opt_get_dontroute(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, SOL_SOCKET, SO_DONTROUTE);
}

int opt_set_broadcast(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, SOL_SOCKET, SO_BROADCAST);
}

int opt_get_broadcast(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, SOL_SOCKET, SO_BROADCAST);
}

int opt_set_ip6_unicast_hops(lua_State *L, p_socket ps)
{
  return opt_setint(L, ps, IPPROTO_IPV6, IPV6_UNICAST_HOPS);
}

int opt_get_ip6_unicast_hops(lua_State *L, p_socket ps)
{
  return opt_getint(L, ps, IPPROTO_IPV6, IPV6_UNICAST_HOPS);
}

int opt_set_ip6_multicast_hops(lua_State *L, p_socket ps)
{
  return opt_setint(L, ps, IPPROTO_IPV6, IPV6_MULTICAST_HOPS);
}

int opt_get_ip6_multicast_hops(lua_State *L, p_socket ps)
{
  return opt_getint(L, ps, IPPROTO_IPV6, IPV6_MULTICAST_HOPS);
}

int opt_set_ip_multicast_loop(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, IPPROTO_IP, IP_MULTICAST_LOOP);
}

int opt_get_ip_multicast_loop(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, IPPROTO_IP, IP_MULTICAST_LOOP);
}

int opt_set_ip6_multicast_loop(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, IPPROTO_IPV6, IPV6_MULTICAST_LOOP);
}

int opt_get_ip6_multicast_loop(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, IPPROTO_IPV6, IPV6_MULTICAST_LOOP);
}

int opt_set_linger(lua_State *L, p_socket ps)
{
    struct linger li;                      /* obj, name, table */
    if (!lua_istable(L, 3)) auxiliar_typeerror(L,3,lua_typename(L, LUA_TTABLE));
    lua_pushstring(L, "on");
    lua_gettable(L, 3);
    if (!lua_isboolean(L, -1))
        luaL_argerror(L, 3, "boolean 'on' field expected");
    li.l_onoff = (u_short) lua_toboolean(L, -1);
    lua_pushstring(L, "timeout");
    lua_gettable(L, 3);
    if (!lua_isnumber(L, -1))
        luaL_argerror(L, 3, "number 'timeout' field expected");
    li.l_linger = (u_short) lua_tonumber(L, -1);
    return opt_set(L, ps, SOL_SOCKET, SO_LINGER, (char *) &li, sizeof(li));
}

int opt_get_linger(lua_State *L, p_socket ps)
{
    struct linger li;                      /* obj, name */
    int len = sizeof(li);
    int err = opt_get(L, ps, SOL_SOCKET, SO_LINGER, (char *) &li, &len);
    if (err)
        return err;
    lua_newtable(L);
    lua_pushboolean(L, li.l_onoff);
    lua_setfield(L, -2, "on");
    lua_pushinteger(L, li.l_linger);
    lua_setfield(L, -2, "timeout");
    return 1;
}

int opt_set_ip_multicast_ttl(lua_State *L, p_socket ps)
{
    return opt_setint(L, ps, IPPROTO_IP, IP_MULTICAST_TTL);
}

int opt_set_ip_multicast_if(lua_State *L, p_socket ps)
{
    const char *address = luaL_checkstring(L, 3);    /* obj, name, ip */
    struct in_addr val;
    val.s_addr = htonl(INADDR_ANY);
    if (strcmp(address, "*") && !inet_aton(address, &val))
        luaL_argerror(L, 3, "ip expected");
    return opt_set(L, ps, IPPROTO_IP, IP_MULTICAST_IF,
        (char *) &val, sizeof(val));
}

int opt_get_ip_multicast_if(lua_State *L, p_socket ps)
{
    struct in_addr val;
    socklen_t len = sizeof(val);
    if (getsockopt(*ps, IPPROTO_IP, IP_MULTICAST_IF, (char *) &val, &len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "getsockopt failed");
        return 2;
    }
    lua_pushstring(L, inet_ntoa(val));
    return 1;
}

int opt_set_ip_add_membership(lua_State *L, p_socket ps)
{
    return opt_setmembership(L, ps, IPPROTO_IP, IP_ADD_MEMBERSHIP);
}

int opt_set_ip_drop_membersip(lua_State *L, p_socket ps)
{
    return opt_setmembership(L, ps, IPPROTO_IP, IP_DROP_MEMBERSHIP);
}

int opt_set_ip6_add_membership(lua_State *L, p_socket ps)
{
    return opt_ip6_setmembership(L, ps, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP);
}

int opt_set_ip6_drop_membersip(lua_State *L, p_socket ps)
{
    return opt_ip6_setmembership(L, ps, IPPROTO_IPV6, IPV6_DROP_MEMBERSHIP);
}

int opt_get_ip6_v6only(lua_State *L, p_socket ps)
{
    return opt_getboolean(L, ps, IPPROTO_IPV6, IPV6_V6ONLY);
}

int opt_set_ip6_v6only(lua_State *L, p_socket ps)
{
    return opt_setboolean(L, ps, IPPROTO_IPV6, IPV6_V6ONLY);
}

/*=========================================================================*\
* Auxiliar functions
\*=========================================================================*/
static int opt_setmembership(lua_State *L, p_socket ps, int level, int name)
{
    struct ip_mreq val;                   /* obj, name, table */
    if (!lua_istable(L, 3)) auxiliar_typeerror(L,3,lua_typename(L, LUA_TTABLE));
    lua_pushstring(L, "multiaddr");
    lua_gettable(L, 3);
    if (!lua_isstring(L, -1))
        luaL_argerror(L, 3, "string 'multiaddr' field expected");
    if (!inet_aton(lua_tostring(L, -1), &val.imr_multiaddr))
        luaL_argerror(L, 3, "invalid 'multiaddr' ip address");
    lua_pushstring(L, "interface");
    lua_gettable(L, 3);
    if (!lua_isstring(L, -1))
        luaL_argerror(L, 3, "string 'interface' field expected");
    val.imr_interface.s_addr = htonl(INADDR_ANY);
    if (strcmp(lua_tostring(L, -1), "*") &&
            !inet_aton(lua_tostring(L, -1), &val.imr_interface))
        luaL_argerror(L, 3, "invalid 'interface' ip address");
    return opt_set(L, ps, level, name, (char *) &val, sizeof(val));
}

static int opt_ip6_setmembership(lua_State *L, p_socket ps, int level, int name)
{
    struct ipv6_mreq val;                   /* obj, opt-name, table */
    memset(&val, 0, sizeof(val));
    if (!lua_istable(L, 3)) auxiliar_typeerror(L,3,lua_typename(L, LUA_TTABLE));
    lua_pushstring(L, "multiaddr");
    lua_gettable(L, 3);
    if (!lua_isstring(L, -1))
        luaL_argerror(L, 3, "string 'multiaddr' field expected");
    if (!inet_pton(AF_INET6, lua_tostring(L, -1), &val.ipv6mr_multiaddr))
        luaL_argerror(L, 3, "invalid 'multiaddr' ip address");
    lua_pushstring(L, "interface");
    lua_gettable(L, 3);
    /* By default we listen to interface on default route
     * (sigh). However, interface= can override it. We should
     * support either number, or name for it. Waiting for
     * windows port of if_nametoindex */
    if (!lua_isnil(L, -1)) {
        if (lua_isnumber(L, -1)) {
            val.ipv6mr_interface = (unsigned int) lua_tonumber(L, -1);
        } else
          luaL_argerror(L, -1, "number 'interface' field expected");
    }
    return opt_set(L, ps, level, name, (char *) &val, sizeof(val));
}

static
int opt_get(lua_State *L, p_socket ps, int level, int name, void *val, int* len)
{
    socklen_t socklen = *len;
    if (getsockopt(*ps, level, name, (char *) val, &socklen) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "getsockopt failed");
        return 2;
    }
    *len = socklen;
    return 0;
}

static
int opt_set(lua_State *L, p_socket ps, int level, int name, void *val, int len)
{
    if (setsockopt(*ps, level, name, (char *) val, len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "setsockopt failed");
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

static int opt_getboolean(lua_State *L, p_socket ps, int level, int name)
{
    int val = 0;
    int len = sizeof(val);
    int err = opt_get(L, ps, level, name, (char *) &val, &len);
    if (err)
        return err;
    lua_pushboolean(L, val);
    return 1;
}

int opt_get_error(lua_State *L, p_socket ps)
{
    int val = 0;
    socklen_t len = sizeof(val);
    if (getsockopt(*ps, SOL_SOCKET, SO_ERROR, (char *) &val, &len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "getsockopt failed");
        return 2;
    }
    lua_pushstring(L, socket_strerror(val));
    return 1;
}

static int opt_setboolean(lua_State *L, p_socket ps, int level, int name)
{
    int val = auxiliar_checkboolean(L, 3);             /* obj, name, bool */
    return opt_set(L, ps, level, name, (char *) &val, sizeof(val));
}

static int opt_getint(lua_State *L, p_socket ps, int level, int name)
{
    int val = 0;
    int len = sizeof(val);
    int err = opt_get(L, ps, level, name, (char *) &val, &len);
    if (err)
        return err;
    lua_pushnumber(L, val);
    return 1;
}

static int opt_setint(lua_State *L, p_socket ps, int level, int name)
{
    int val = (int) lua_tonumber(L, 3);             /* obj, name, int */
    return opt_set(L, ps, level, name, (char *) &val, sizeof(val));
}

/*=========================================================================*\
* Select implementation
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>

#include "lua.h"
//#include "lauxlib.h"


/*=========================================================================*\
* Internal function prototypes.
\*=========================================================================*/
static t_socket getfd(lua_State *L);
static int dirty(lua_State *L);
static void collect_fd(lua_State *L, int tab, int itab,
        fd_set *set, t_socket *max_fd);
static int check_dirty(lua_State *L, int tab, int dtab, fd_set *set);
static void return_fd(lua_State *L, fd_set *set, t_socket max_fd,
        int itab, int tab, int start);
static void make_assoc(lua_State *L, int tab);
static int global_select(lua_State *L);

/* functions in library namespace */
static luaL_Reg selectfunc[] = {
    {"select", global_select},
    {NULL,     NULL}
};

/*=========================================================================*\
* Exported functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int select_open(lua_State *L) {
    lua_pushstring(L, "_SETSIZE");
    lua_pushinteger(L, FD_SETSIZE);
    lua_rawset(L, -3);
    lua_pushstring(L, "_SOCKETINVALID");
    lua_pushinteger(L, SOCKET_INVALID);
    lua_rawset(L, -3);
    luaL_setfuncs(L, selectfunc, 0);
    return 0;
}

/*=========================================================================*\
* Global Lua functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Waits for a set of sockets until a condition is met or timeout.
\*-------------------------------------------------------------------------*/
static int global_select(lua_State *L) {
    int rtab, wtab, itab, ret, ndirty;
    t_socket max_fd = SOCKET_INVALID;
    fd_set rset, wset;
    t_timeout tm;
    double t = luaL_optnumber(L, 3, -1);
    FD_ZERO(&rset); FD_ZERO(&wset);
    lua_settop(L, 3);
    lua_newtable(L); itab = lua_gettop(L);
    lua_newtable(L); rtab = lua_gettop(L);
    lua_newtable(L); wtab = lua_gettop(L);
    collect_fd(L, 1, itab, &rset, &max_fd);
    collect_fd(L, 2, itab, &wset, &max_fd);
    ndirty = check_dirty(L, 1, rtab, &rset);
    t = ndirty > 0? 0.0: t;
    timeout_init(&tm, t, -1);
    timeout_markstart(&tm);
    ret = socket_select(max_fd+1, &rset, &wset, NULL, &tm);
    if (ret > 0 || ndirty > 0) {
        return_fd(L, &rset, max_fd+1, itab, rtab, ndirty);
        return_fd(L, &wset, max_fd+1, itab, wtab, 0);
        make_assoc(L, rtab);
        make_assoc(L, wtab);
        return 2;
    } else if (ret == 0) {
        lua_pushstring(L, "timeout");
        return 3;
    } else {
        luaL_error(L, "select failed");
        return 3;
    }
}

/*=========================================================================*\
* Internal functions
\*=========================================================================*/
static t_socket getfd(lua_State *L) {
    t_socket fd = SOCKET_INVALID;
    lua_pushstring(L, "getfd");
    lua_gettable(L, -2);
    if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, -2);
        lua_call(L, 1, 1);
        if (lua_isnumber(L, -1)) {
            double numfd = lua_tonumber(L, -1);
            fd = (numfd >= 0.0)? (t_socket) numfd: SOCKET_INVALID;
        }
    }
    lua_pop(L, 1);
    return fd;
}

static int dirty(lua_State *L) {
    int is = 0;
    lua_pushstring(L, "dirty");
    lua_gettable(L, -2);
    if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, -2);
        lua_call(L, 1, 1);
        is = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    return is;
}

static void collect_fd(lua_State *L, int tab, int itab,
        fd_set *set, t_socket *max_fd) {
    int i = 1, n = 0;
    /* nil is the same as an empty table */
    if (lua_isnil(L, tab)) return;
    /* otherwise we need it to be a table */
    luaL_checktype(L, tab, LUA_TTABLE);
    for ( ;; ) {
        t_socket fd;
        lua_pushnumber(L, i);
        lua_gettable(L, tab);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        /* getfd figures out if this is a socket */
        fd = getfd(L);
        if (fd != SOCKET_INVALID) {
            /* make sure we don't overflow the fd_set */
#ifdef _MSC_VER
            if (n >= FD_SETSIZE)
                luaL_argerror(L, tab, "too many sockets");
#else
            if (fd >= FD_SETSIZE)
                luaL_argerror(L, tab, "descriptor too large for set size");
#endif
            FD_SET(fd, set);
            n++;
            /* keep track of the largest descriptor so far */
            if (*max_fd == SOCKET_INVALID || *max_fd < fd)
                *max_fd = fd;
            /* make sure we can map back from descriptor to the object */
            lua_pushnumber(L, (lua_Number) fd);
            lua_pushvalue(L, -2);
            lua_settable(L, itab);
        }
        lua_pop(L, 1);
        i = i + 1;
    }
}

static int check_dirty(lua_State *L, int tab, int dtab, fd_set *set) {
    int ndirty = 0, i = 1;
    if (lua_isnil(L, tab))
        return 0;
    for ( ;; ) {
        t_socket fd;
        lua_pushnumber(L, i);
        lua_gettable(L, tab);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        fd = getfd(L);
        if (fd != SOCKET_INVALID && dirty(L)) {
            lua_pushnumber(L, ++ndirty);
            lua_pushvalue(L, -2);
            lua_settable(L, dtab);
            FD_CLR(fd, set);
        }
        lua_pop(L, 1);
        i = i + 1;
    }
    return ndirty;
}

static void return_fd(lua_State *L, fd_set *set, t_socket max_fd,
        int itab, int tab, int start) {
    t_socket fd;
    for (fd = 0; fd < max_fd; fd++) {
        if (FD_ISSET(fd, set)) {
            lua_pushnumber(L, ++start);
            lua_pushnumber(L, (lua_Number) fd);
            lua_gettable(L, itab);
            lua_settable(L, tab);
        }
    }
}

static void make_assoc(lua_State *L, int tab) {
    int i = 1, atab;
    lua_newtable(L); atab = lua_gettop(L);
    for ( ;; ) {
        lua_pushnumber(L, i);
        lua_gettable(L, tab);
        if (!lua_isnil(L, -1)) {
            lua_pushnumber(L, i);
            lua_pushvalue(L, -2);
            lua_settable(L, atab);
            lua_pushnumber(L, i);
            lua_settable(L, atab);
        } else {
            lua_pop(L, 1);
            break;
        }
        i = i+1;
    }
}


#ifndef _MSC_VER
/*=========================================================================*\
* Serial stream
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>

#include "lua.h"
//#include "lauxlib.h"

#include <sys/un.h>

/*
Reuses userdata definition from unix.h, since it is useful for all
stream-like objects.

If we stored the serial path for use in error messages or userdata
printing, we might need our own userdata definition.

Group usage is semi-inherited from unix.c, but unnecessary since we
have only one object type.
*/

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int tcp_global_create(lua_State *L);
static int tcp_meth_send(lua_State *L);
static int tcp_meth_receive(lua_State *L);
static int tcp_meth_close(lua_State *L);
static int tcp_meth_settimeout(lua_State *L);
static int tcp_meth_getfd(lua_State *L);
static int tcp_meth_setfd(lua_State *L);
static int tcp_meth_dirty(lua_State *L);
static int tcp_meth_getstats(lua_State *L);
static int tcp_meth_setstats(lua_State *L);

/* serial object methods */
static luaL_Reg serial_methods[] = {
    {"__gc",        tcp_meth_close},
    {"__tostring",  auxiliar_tostring},
    {"close",       tcp_meth_close},
    {"dirty",       tcp_meth_dirty},
    {"getfd",       tcp_meth_getfd},
    {"getstats",    tcp_meth_getstats},
    {"setstats",    tcp_meth_setstats},
    {"receive",     tcp_meth_receive},
    {"send",        tcp_meth_send},
    {"setfd",       tcp_meth_setfd},
    {"settimeout",  tcp_meth_settimeout},
    {NULL,          NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
LUASOCKET_API int luaopen_socket_serial(lua_State *L) {
    /* create classes */
    auxiliar_newclass(L, "serial{client}", serial_methods);
    /* create class groups */
    auxiliar_add2group(L, "serial{client}", "serial{any}");
    lua_pushcfunction(L, tcp_global_create);
    return 1;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Just call buffered IO methods
\*-------------------------------------------------------------------------*/
static int tcp_meth_send(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "serial{client}", 1);
    return buffer_meth_send(L, &un->buf);
}

static int tcp_meth_receive(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "serial{client}", 1);
    return buffer_meth_receive(L, &un->buf);
}

static int tcp_meth_getstats(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "serial{client}", 1);
    return buffer_meth_getstats(L, &un->buf);
}

static int tcp_meth_setstats(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "serial{client}", 1);
    return buffer_meth_setstats(L, &un->buf);
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int tcp_meth_getfd(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "serial{any}", 1);
    lua_pushnumber(L, (int) un->sock);
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int tcp_meth_setfd(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "serial{any}", 1);
    un->sock = (t_socket) luaL_checknumber(L, 2);
    return 0;
}

static int tcp_meth_dirty(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "serial{any}", 1);
    lua_pushboolean(L, !buffer_isempty(&un->buf));
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int tcp_meth_close(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "serial{any}", 1);
    socket_destroy(&un->sock);
    lua_pushnumber(L, 1);
    return 1;
}


/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int tcp_meth_settimeout(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "serial{any}", 1);
    return timeout_meth_settimeout(L, &un->tm);
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/


/*-------------------------------------------------------------------------*\
* Creates a serial object
\*-------------------------------------------------------------------------*/
static int tcp_global_create(lua_State *L) {
    const char* path = luaL_checkstring(L, 1);

    /* allocate unix object */
    p_unix un = (p_unix) lua_newuserdata(L, sizeof(t_unix));

    /* open serial device */
    t_socket sock = open(path, O_NOCTTY|O_RDWR);

    /*printf("open %s on %d\n", path, sock);*/

    if (sock < 0)  {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
        lua_pushnumber(L, errno);
        return 3;
    }
    /* set its type as client object */
    auxiliar_setclass(L, "serial{client}", -1);
    /* initialize remaining structure fields */
    socket_setnonblocking(&sock);
    un->sock = sock;
    io_init(&un->io, (p_send) socket_write, (p_recv) socket_read,
            (p_error) socket_ioerror, &un->sock);
    timeout_init(&un->tm, -1, -1);
    buffer_init(&un->buf, &un->io, &un->tm);
    return 1;
}
#endif
/*=========================================================================*\
* TCP object
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>

#include "lua.h"
//#include "lauxlib.h"


/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int tcp_global_create(lua_State *L);
static int tcp_global_create4(lua_State *L);
static int tcp_global_create6(lua_State *L);
static int tcp_global_connect(lua_State *L);
static int tcp_meth_connect(lua_State *L);
static int tcp_meth_listen(lua_State *L);
static int tcp_meth_getfamily(lua_State *L);
static int tcp_meth_bind(lua_State *L);
static int tcp_meth_send(lua_State *L);
static int tcp_meth_getstats(lua_State *L);
static int tcp_meth_setstats(lua_State *L);
static int tcp_meth_getsockname(lua_State *L);
static int tcp_meth_getpeername(lua_State *L);
static int tcp_meth_shutdown(lua_State *L);
static int tcp_meth_receive(lua_State *L);
static int tcp_meth_accept(lua_State *L);
static int tcp_meth_close(lua_State *L);
static int tcp_meth_getoption(lua_State *L);
static int tcp_meth_setoption(lua_State *L);
static int tcp_meth_gettimeout(lua_State *L);
static int tcp_meth_settimeout(lua_State *L);
static int tcp_meth_getfd(lua_State *L);
static int tcp_meth_setfd(lua_State *L);
static int tcp_meth_dirty(lua_State *L);

/* tcp object methods */
static luaL_Reg tcp_methods[] = {
    {"__gc",        tcp_meth_close},
    {"__tostring",  auxiliar_tostring},
    {"accept",      tcp_meth_accept},
    {"bind",        tcp_meth_bind},
    {"close",       tcp_meth_close},
    {"connect",     tcp_meth_connect},
    {"dirty",       tcp_meth_dirty},
    {"getfamily",   tcp_meth_getfamily},
    {"getfd",       tcp_meth_getfd},
    {"getoption",   tcp_meth_getoption},
    {"getpeername", tcp_meth_getpeername},
    {"getsockname", tcp_meth_getsockname},
    {"getstats",    tcp_meth_getstats},
    {"setstats",    tcp_meth_setstats},
    {"listen",      tcp_meth_listen},
    {"receive",     tcp_meth_receive},
    {"send",        tcp_meth_send},
    {"setfd",       tcp_meth_setfd},
    {"setoption",   tcp_meth_setoption},
    {"setpeername", tcp_meth_connect},
    {"setsockname", tcp_meth_bind},
    {"settimeout",  tcp_meth_settimeout},
    {"gettimeout",  tcp_meth_gettimeout},
    {"shutdown",    tcp_meth_shutdown},
    {NULL,          NULL}
};

/* socket option handlers */
static t_opt tcpoptget[] = {
    {"keepalive",   opt_get_keepalive},
    {"reuseaddr",   opt_get_reuseaddr},
    {"reuseport",   opt_get_reuseport},
    {"tcp-nodelay", opt_get_tcp_nodelay},
    {"linger",      opt_get_linger},
    {"error",       opt_get_error},
    {NULL,          NULL}
};

static t_opt tcpoptset[] = {
    {"keepalive",   opt_set_keepalive},
    {"reuseaddr",   opt_set_reuseaddr},
    {"reuseport",   opt_set_reuseport},
    {"tcp-nodelay", opt_set_tcp_nodelay},
    {"ipv6-v6only", opt_set_ip6_v6only},
    {"linger",      opt_set_linger},
    {NULL,          NULL}
};

/* functions in library namespace */
static luaL_Reg tcpfunc[] = {
    {"tcp", tcp_global_create},
    {"tcp4", tcp_global_create4},
    {"tcp6", tcp_global_create6},
    {"connect", tcp_global_connect},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int tcp_open(lua_State *L)
{
    /* create classes */
    auxiliar_newclass(L, "tcp{master}", tcp_methods);
    auxiliar_newclass(L, "tcp{client}", tcp_methods);
    auxiliar_newclass(L, "tcp{server}", tcp_methods);
    /* create class groups */
    auxiliar_add2group(L, "tcp{master}", "tcp{any}");
    auxiliar_add2group(L, "tcp{client}", "tcp{any}");
    auxiliar_add2group(L, "tcp{server}", "tcp{any}");
    /* define library functions */
    luaL_setfuncs(L, tcpfunc, 0);
    return 0;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Just call buffered IO methods
\*-------------------------------------------------------------------------*/
static int tcp_meth_send(lua_State *L) {
    p_tcp tcp = (p_tcp) auxiliar_checkclass(L, "tcp{client}", 1);
    return buffer_meth_send(L, &tcp->buf);
}

static int tcp_meth_receive(lua_State *L) {
    p_tcp tcp = (p_tcp) auxiliar_checkclass(L, "tcp{client}", 1);
    return buffer_meth_receive(L, &tcp->buf);
}

static int tcp_meth_getstats(lua_State *L) {
    p_tcp tcp = (p_tcp) auxiliar_checkclass(L, "tcp{client}", 1);
    return buffer_meth_getstats(L, &tcp->buf);
}

static int tcp_meth_setstats(lua_State *L) {
    p_tcp tcp = (p_tcp) auxiliar_checkclass(L, "tcp{client}", 1);
    return buffer_meth_setstats(L, &tcp->buf);
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int tcp_meth_getoption(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    return opt_meth_getoption(L, tcpoptget, &tcp->sock);
}

static int tcp_meth_setoption(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    return opt_meth_setoption(L, tcpoptset, &tcp->sock);
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int tcp_meth_getfd(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    lua_pushnumber(L, (int) tcp->sock);
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int tcp_meth_setfd(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    tcp->sock = (t_socket) luaL_checknumber(L, 2);
    return 0;
}

static int tcp_meth_dirty(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    lua_pushboolean(L, !buffer_isempty(&tcp->buf));
    return 1;
}

/*-------------------------------------------------------------------------*\
* Waits for and returns a client object attempting connection to the
* server object
\*-------------------------------------------------------------------------*/
static int tcp_meth_accept(lua_State *L)
{
    p_tcp server = (p_tcp) auxiliar_checkclass(L, "tcp{server}", 1);
    p_timeout tm = timeout_markstart(&server->tm);
    t_socket sock;
    const char *err = inet_tryaccept(&server->sock, server->family, &sock, tm);
    /* if successful, push client socket */
    if (err == NULL) {
        p_tcp clnt = (p_tcp) lua_newuserdata(L, sizeof(t_tcp));
        auxiliar_setclass(L, "tcp{client}", -1);
        /* initialize structure fields */
        memset(clnt, 0, sizeof(t_tcp));
        socket_setnonblocking(&sock);
        clnt->sock = sock;
        io_init(&clnt->io, (p_send) socket_send, (p_recv) socket_recv,
                (p_error) socket_ioerror, &clnt->sock);
        timeout_init(&clnt->tm, -1, -1);
        buffer_init(&clnt->buf, &clnt->io, &clnt->tm);
        clnt->family = server->family;
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
}

/*-------------------------------------------------------------------------*\
* Binds an object to an address
\*-------------------------------------------------------------------------*/
static int tcp_meth_bind(lua_State *L) {
    p_tcp tcp = (p_tcp) auxiliar_checkclass(L, "tcp{master}", 1);
    const char *address =  luaL_checkstring(L, 2);
    const char *port = luaL_checkstring(L, 3);
    const char *err;
    struct addrinfo bindhints;
    memset(&bindhints, 0, sizeof(bindhints));
    bindhints.ai_socktype = SOCK_STREAM;
    bindhints.ai_family = tcp->family;
    bindhints.ai_flags = AI_PASSIVE;
    err = inet_trybind(&tcp->sock, &tcp->family, address, port, &bindhints);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Turns a master tcp object into a client object.
\*-------------------------------------------------------------------------*/
static int tcp_meth_connect(lua_State *L) {
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    const char *address =  luaL_checkstring(L, 2);
    const char *port = luaL_checkstring(L, 3);
    struct addrinfo connecthints;
    const char *err;
    memset(&connecthints, 0, sizeof(connecthints));
    connecthints.ai_socktype = SOCK_STREAM;
    /* make sure we try to connect only to the same family */
    connecthints.ai_family = tcp->family;
    timeout_markstart(&tcp->tm);
    err = inet_tryconnect(&tcp->sock, &tcp->family, address, port,
        &tcp->tm, &connecthints);
    /* have to set the class even if it failed due to non-blocking connects */
    auxiliar_setclass(L, "tcp{client}", 1);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int tcp_meth_close(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    socket_destroy(&tcp->sock);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Returns family as string
\*-------------------------------------------------------------------------*/
static int tcp_meth_getfamily(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    if (tcp->family == AF_INET6) {
        lua_pushliteral(L, "inet6");
        return 1;
    } else if (tcp->family == AF_INET) {
        lua_pushliteral(L, "inet4");
        return 1;
    } else {
        lua_pushliteral(L, "inet4");
        return 1;
    }
}

/*-------------------------------------------------------------------------*\
* Puts the sockt in listen mode
\*-------------------------------------------------------------------------*/
static int tcp_meth_listen(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkclass(L, "tcp{master}", 1);
    int backlog = (int) luaL_optnumber(L, 2, 32);
    int err = socket_listen(&tcp->sock, backlog);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }
    /* turn master object into a server object */
    auxiliar_setclass(L, "tcp{server}", 1);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Shuts the connection down partially
\*-------------------------------------------------------------------------*/
static int tcp_meth_shutdown(lua_State *L)
{
    /* SHUT_RD,  SHUT_WR,  SHUT_RDWR  have  the value 0, 1, 2, so we can use method index directly */
    static const char* methods[] = { "receive", "send", "both", NULL };
    p_tcp tcp = (p_tcp) auxiliar_checkclass(L, "tcp{client}", 1);
    int how = luaL_checkoption(L, 2, "both", methods);
    socket_shutdown(&tcp->sock, how);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call inet methods
\*-------------------------------------------------------------------------*/
static int tcp_meth_getpeername(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    return inet_meth_getpeername(L, &tcp->sock, tcp->family);
}

static int tcp_meth_getsockname(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    return inet_meth_getsockname(L, &tcp->sock, tcp->family);
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int tcp_meth_settimeout(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    return timeout_meth_settimeout(L, &tcp->tm);
}

static int tcp_meth_gettimeout(lua_State *L)
{
    p_tcp tcp = (p_tcp) auxiliar_checkgroup(L, "tcp{any}", 1);
    return timeout_meth_gettimeout(L, &tcp->tm);
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master tcp object
\*-------------------------------------------------------------------------*/
static int tcp_create(lua_State *L, int family) {
    p_tcp tcp = (p_tcp) lua_newuserdata(L, sizeof(t_tcp));
    memset(tcp, 0, sizeof(t_tcp));
    /* set its type as master object */
    auxiliar_setclass(L, "tcp{master}", -1);
    /* if family is AF_UNSPEC, we leave the socket invalid and
     * store AF_UNSPEC into family. This will allow it to later be
     * replaced with an AF_INET6 or AF_INET socket upon first use. */
    tcp->sock = SOCKET_INVALID;
    tcp->family = family;
    io_init(&tcp->io, (p_send) socket_send, (p_recv) socket_recv,
            (p_error) socket_ioerror, &tcp->sock);
    timeout_init(&tcp->tm, -1, -1);
    buffer_init(&tcp->buf, &tcp->io, &tcp->tm);
    if (family != AF_UNSPEC) {
        const char *err = inet_trycreate(&tcp->sock, family, SOCK_STREAM, 0);
        if (err != NULL) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        socket_setnonblocking(&tcp->sock);
    }
    return 1;
}

static int tcp_global_create(lua_State *L) {
    return tcp_create(L, AF_UNSPEC);
}

static int tcp_global_create4(lua_State *L) {
    return tcp_create(L, AF_INET);
}

static int tcp_global_create6(lua_State *L) {
    return tcp_create(L, AF_INET6);
}

static int tcp_global_connect(lua_State *L) {
    const char *remoteaddr = luaL_checkstring(L, 1);
    const char *remoteserv = luaL_checkstring(L, 2);
    const char *localaddr  = luaL_optstring(L, 3, NULL);
    const char *localserv  = luaL_optstring(L, 4, "0");
    int family = inet_optfamily(L, 5, "unspec");
    p_tcp tcp = (p_tcp) lua_newuserdata(L, sizeof(t_tcp));
    struct addrinfo bindhints, connecthints;
    const char *err = NULL;
    /* initialize tcp structure */
    memset(tcp, 0, sizeof(t_tcp));
    io_init(&tcp->io, (p_send) socket_send, (p_recv) socket_recv,
            (p_error) socket_ioerror, &tcp->sock);
    timeout_init(&tcp->tm, -1, -1);
    buffer_init(&tcp->buf, &tcp->io, &tcp->tm);
    tcp->sock = SOCKET_INVALID;
    tcp->family = AF_UNSPEC;
    /* allow user to pick local address and port */
    memset(&bindhints, 0, sizeof(bindhints));
    bindhints.ai_socktype = SOCK_STREAM;
    bindhints.ai_family = family;
    bindhints.ai_flags = AI_PASSIVE;
    if (localaddr) {
        err = inet_trybind(&tcp->sock, &tcp->family, localaddr,
            localserv, &bindhints);
        if (err) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
    }
    /* try to connect to remote address and port */
    memset(&connecthints, 0, sizeof(connecthints));
    connecthints.ai_socktype = SOCK_STREAM;
    /* make sure we try to connect only to the same family */
    connecthints.ai_family = tcp->family;
    err = inet_tryconnect(&tcp->sock, &tcp->family, remoteaddr, remoteserv,
         &tcp->tm, &connecthints);
    if (err) {
        socket_destroy(&tcp->sock);
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    auxiliar_setclass(L, "tcp{client}", -1);
    return 1;
}

/*=========================================================================*\
* Timeout management functions
* LuaSocket toolkit
\*=========================================================================*/
#include <stdio.h>
#include <limits.h>
#include <float.h>

#include "lua.h"
//#include "lauxlib.h"


#ifdef _MSC_VER
#include <windows.h>
#else
#include <time.h>
#include <sys/time.h>
#endif

/* min and max macros */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? x : y)
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? x : y)
#endif

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int timeout_lua_gettime(lua_State *L);
static int timeout_lua_sleep(lua_State *L);

static luaL_Reg func[] = {
    { "gettime", timeout_lua_gettime },
    { "sleep", timeout_lua_sleep },
    { NULL, NULL }
};

/*=========================================================================*\
* Exported functions.
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Initialize structure
\*-------------------------------------------------------------------------*/
void timeout_init(p_timeout tm, double block, double total) {
    tm->block = block;
    tm->total = total;
}

/*-------------------------------------------------------------------------*\
* Determines how much time we have left for the next system call,
* if the previous call was successful
* Input
*   tm: timeout control structure
* Returns
*   the number of ms left or -1 if there is no time limit
\*-------------------------------------------------------------------------*/
double timeout_get(p_timeout tm) {
    if (tm->block < 0.0 && tm->total < 0.0) {
        return -1;
    } else if (tm->block < 0.0) {
        double t = tm->total - timeout_gettime() + tm->start;
        return MAX(t, 0.0);
    } else if (tm->total < 0.0) {
        return tm->block;
    } else {
        double t = tm->total - timeout_gettime() + tm->start;
        return MIN(tm->block, MAX(t, 0.0));
    }
}

/*-------------------------------------------------------------------------*\
* Returns time since start of operation
* Input
*   tm: timeout control structure
* Returns
*   start field of structure
\*-------------------------------------------------------------------------*/
double timeout_getstart(p_timeout tm) {
    return tm->start;
}

/*-------------------------------------------------------------------------*\
* Determines how much time we have left for the next system call,
* if the previous call was a failure
* Input
*   tm: timeout control structure
* Returns
*   the number of ms left or -1 if there is no time limit
\*-------------------------------------------------------------------------*/
double timeout_getretry(p_timeout tm) {
    if (tm->block < 0.0 && tm->total < 0.0) {
        return -1;
    } else if (tm->block < 0.0) {
        double t = tm->total - timeout_gettime() + tm->start;
        return MAX(t, 0.0);
    } else if (tm->total < 0.0) {
        double t = tm->block - timeout_gettime() + tm->start;
        return MAX(t, 0.0);
    } else {
        double t = tm->total - timeout_gettime() + tm->start;
        return MIN(tm->block, MAX(t, 0.0));
    }
}

/*-------------------------------------------------------------------------*\
* Marks the operation start time in structure
* Input
*   tm: timeout control structure
\*-------------------------------------------------------------------------*/
p_timeout timeout_markstart(p_timeout tm) {
    tm->start = timeout_gettime();
    return tm;
}

/*-------------------------------------------------------------------------*\
* Gets time in s, relative to January 1, 1970 (UTC)
* Returns
*   time in s.
\*-------------------------------------------------------------------------*/
#ifdef _MSC_VER
double timeout_gettime(void) {
    FILETIME ft;
    double t;
    GetSystemTimeAsFileTime(&ft);
    /* Windows file time (time since January 1, 1601 (UTC)) */
    t  = ft.dwLowDateTime/1.0e7 + ft.dwHighDateTime*(4294967296.0/1.0e7);
    /* convert to Unix Epoch time (time since January 1, 1970 (UTC)) */
    return (t - 11644473600.0);
}
#else
double timeout_gettime(void) {
    struct timeval v;
    gettimeofday(&v, (struct timezone *) NULL);
    /* Unix Epoch time (time since January 1, 1970 (UTC)) */
    return v.tv_sec + v.tv_usec/1.0e6;
}
#endif

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int timeout_open(lua_State *L) {
    luaL_setfuncs(L, func, 0);
    return 0;
}

/*-------------------------------------------------------------------------*\
* Sets timeout values for IO operations
* Lua Input: base, time [, mode]
*   time: time out value in seconds
*   mode: "b" for block timeout, "t" for total timeout. (default: b)
\*-------------------------------------------------------------------------*/
int timeout_meth_settimeout(lua_State *L, p_timeout tm) {
    double t = luaL_optnumber(L, 2, -1);
    const char *mode = luaL_optstring(L, 3, "b");
    switch (*mode) {
        case 'b':
            tm->block = t;
            break;
        case 'r': case 't':
            tm->total = t;
            break;
        default:
            luaL_argcheck(L, 0, 3, "invalid timeout mode");
            break;
    }
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Gets timeout values for IO operations
* Lua Output: block, total
\*-------------------------------------------------------------------------*/
int timeout_meth_gettimeout(lua_State *L, p_timeout tm) {
    lua_pushnumber(L, tm->block);
    lua_pushnumber(L, tm->total);
    return 2;
}

/*=========================================================================*\
* Test support functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Returns the time the system has been up, in secconds.
\*-------------------------------------------------------------------------*/
static int timeout_lua_gettime(lua_State *L)
{
    lua_pushnumber(L, timeout_gettime());
    return 1;
}

/*-------------------------------------------------------------------------*\
* Sleep for n seconds.
\*-------------------------------------------------------------------------*/
#ifdef _MSC_VER
int timeout_lua_sleep(lua_State *L)
{
    double n = luaL_checknumber(L, 1);
    if (n < 0.0) n = 0.0;
    if (n < DBL_MAX/1000.0) n *= 1000.0;
    if (n > INT_MAX) n = INT_MAX;
    Sleep((int)n);
    return 0;
}
#else
int timeout_lua_sleep(lua_State *L)
{
    double n = luaL_checknumber(L, 1);
    struct timespec t, r;
    if (n < 0.0) n = 0.0;
    if (n > INT_MAX) n = INT_MAX;
    t.tv_sec = (int) n;
    n -= t.tv_sec;
    t.tv_nsec = (int) (n * 1000000000);
    if (t.tv_nsec >= 1000000000) t.tv_nsec = 999999999;
    while (nanosleep(&t, &r) != 0) {
        t.tv_sec = r.tv_sec;
        t.tv_nsec = r.tv_nsec;
    }
    return 0;
}
#endif

/*=========================================================================*\
* UDP object
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>
#include <stdlib.h>

#include "lua.h"
//#include "lauxlib.h"


/* min and max macros */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? x : y)
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? x : y)
#endif

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int udp_global_create(lua_State *L);
static int udp_global_create4(lua_State *L);
static int udp_global_create6(lua_State *L);
static int udp_meth_send(lua_State *L);
static int udp_meth_sendto(lua_State *L);
static int udp_meth_receive(lua_State *L);
static int udp_meth_receivefrom(lua_State *L);
static int udp_meth_getfamily(lua_State *L);
static int udp_meth_getsockname(lua_State *L);
static int udp_meth_getpeername(lua_State *L);
static int udp_meth_gettimeout(lua_State *L);
static int udp_meth_setsockname(lua_State *L);
static int udp_meth_setpeername(lua_State *L);
static int udp_meth_close(lua_State *L);
static int udp_meth_setoption(lua_State *L);
static int udp_meth_getoption(lua_State *L);
static int udp_meth_settimeout(lua_State *L);
static int udp_meth_getfd(lua_State *L);
static int udp_meth_setfd(lua_State *L);
static int udp_meth_dirty(lua_State *L);

/* udp object methods */
static luaL_Reg udp_methods[] = {
    {"__gc",        udp_meth_close},
    {"__tostring",  auxiliar_tostring},
    {"close",       udp_meth_close},
    {"dirty",       udp_meth_dirty},
    {"getfamily",   udp_meth_getfamily},
    {"getfd",       udp_meth_getfd},
    {"getpeername", udp_meth_getpeername},
    {"getsockname", udp_meth_getsockname},
    {"receive",     udp_meth_receive},
    {"receivefrom", udp_meth_receivefrom},
    {"send",        udp_meth_send},
    {"sendto",      udp_meth_sendto},
    {"setfd",       udp_meth_setfd},
    {"setoption",   udp_meth_setoption},
    {"getoption",   udp_meth_getoption},
    {"setpeername", udp_meth_setpeername},
    {"setsockname", udp_meth_setsockname},
    {"settimeout",  udp_meth_settimeout},
    {"gettimeout",  udp_meth_gettimeout},
    {NULL,          NULL}
};

/* socket options for setoption */
static t_opt optset[] = {
    {"dontroute",            opt_set_dontroute},
    {"broadcast",            opt_set_broadcast},
    {"reuseaddr",            opt_set_reuseaddr},
    {"reuseport",            opt_set_reuseport},
    {"ip-multicast-if",      opt_set_ip_multicast_if},
    {"ip-multicast-ttl",     opt_set_ip_multicast_ttl},
    {"ip-multicast-loop",    opt_set_ip_multicast_loop},
    {"ip-add-membership",    opt_set_ip_add_membership},
    {"ip-drop-membership",   opt_set_ip_drop_membersip},
    {"ipv6-unicast-hops",    opt_set_ip6_unicast_hops},
    {"ipv6-multicast-hops",  opt_set_ip6_unicast_hops},
    {"ipv6-multicast-loop",  opt_set_ip6_multicast_loop},
    {"ipv6-add-membership",  opt_set_ip6_add_membership},
    {"ipv6-drop-membership", opt_set_ip6_drop_membersip},
    {"ipv6-v6only",          opt_set_ip6_v6only},
    {NULL,                   NULL}
};

/* socket options for getoption */
static t_opt udpoptget[] = {
    {"dontroute",            opt_get_dontroute},
    {"broadcast",            opt_get_broadcast},
    {"reuseaddr",            opt_get_reuseaddr},
    {"reuseport",            opt_get_reuseport},
    {"ip-multicast-if",      opt_get_ip_multicast_if},
    {"ip-multicast-loop",    opt_get_ip_multicast_loop},
    {"error",                opt_get_error},
    {"ipv6-unicast-hops",    opt_get_ip6_unicast_hops},
    {"ipv6-multicast-hops",  opt_get_ip6_unicast_hops},
    {"ipv6-multicast-loop",  opt_get_ip6_multicast_loop},
    {"ipv6-v6only",          opt_get_ip6_v6only},
    {NULL,                   NULL}
};

/* functions in library namespace */
static luaL_Reg udpfunc[] = {
    {"udp", udp_global_create},
    {"udp4", udp_global_create4},
    {"udp6", udp_global_create6},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int udp_open(lua_State *L) {
    /* create classes */
    auxiliar_newclass(L, "udp{connected}", udp_methods);
    auxiliar_newclass(L, "udp{unconnected}", udp_methods);
    /* create class groups */
    auxiliar_add2group(L, "udp{connected}",   "udp{any}");
    auxiliar_add2group(L, "udp{unconnected}", "udp{any}");
    auxiliar_add2group(L, "udp{connected}",   "select{able}");
    auxiliar_add2group(L, "udp{unconnected}", "select{able}");
    /* define library functions */
    luaL_setfuncs(L, udpfunc, 0);
    /* export default UDP size */
    lua_pushliteral(L, "_DATAGRAMSIZE");
    lua_pushinteger(L, UDP_DATAGRAMSIZE);
    lua_rawset(L, -3);
    return 0;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
static const char *udp_strerror(int err) {
    /* a 'closed' error on an unconnected means the target address was not
     * accepted by the transport layer */
    if (err == IO_CLOSED) return "refused";
    else return socket_strerror(err);
}

/*-------------------------------------------------------------------------*\
* Send data through connected udp socket
\*-------------------------------------------------------------------------*/
static int udp_meth_send(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkclass(L, "udp{connected}", 1);
    p_timeout tm = &udp->tm;
    size_t count, sent = 0;
    int err;
    const char *data = luaL_checklstring(L, 2, &count);
    timeout_markstart(tm);
    err = socket_send(&udp->sock, data, count, &sent, tm);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, udp_strerror(err));
        return 2;
    }
    lua_pushnumber(L, (lua_Number) sent);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Send data through unconnected udp socket
\*-------------------------------------------------------------------------*/
static int udp_meth_sendto(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkclass(L, "udp{unconnected}", 1);
    size_t count, sent = 0;
    const char *data = luaL_checklstring(L, 2, &count);
    const char *ip = luaL_checkstring(L, 3);
    const char *port = luaL_checkstring(L, 4);
    p_timeout tm = &udp->tm;
    int err;
    struct addrinfo aihint;
    struct addrinfo *ai;
    memset(&aihint, 0, sizeof(aihint));
    aihint.ai_family = udp->family;
    aihint.ai_socktype = SOCK_DGRAM;
    aihint.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    err = getaddrinfo(ip, port, &aihint, &ai);
	if (err) {
        lua_pushnil(L);
        lua_pushstring(L, gai_strerror(err));
        return 2;
    }
    timeout_markstart(tm);
    err = socket_sendto(&udp->sock, data, count, &sent, ai->ai_addr,
        (socklen_t) ai->ai_addrlen, tm);
    freeaddrinfo(ai);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, udp_strerror(err));
        return 2;
    }
    lua_pushnumber(L, (lua_Number) sent);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Receives data from a UDP socket
\*-------------------------------------------------------------------------*/
static int udp_meth_receive(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    char buf[UDP_DATAGRAMSIZE];
    size_t got, wanted = (size_t) luaL_optnumber(L, 2, sizeof(buf));
    char *dgram = wanted > sizeof(buf)? (char *) malloc(wanted): buf;
    int err;
    p_timeout tm = &udp->tm;
    timeout_markstart(tm);
    if (!dgram) {
        lua_pushnil(L);
        lua_pushliteral(L, "out of memory");
        return 2;
    }
    err = socket_recv(&udp->sock, dgram, wanted, &got, tm);
    /* Unlike TCP, recv() of zero is not closed, but a zero-length packet. */
    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, udp_strerror(err));
        if (wanted > sizeof(buf)) free(dgram);
        return 2;
    }
    lua_pushlstring(L, dgram, got);
    if (wanted > sizeof(buf)) free(dgram);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Receives data and sender from a UDP socket
\*-------------------------------------------------------------------------*/
static int udp_meth_receivefrom(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkclass(L, "udp{unconnected}", 1);
    char buf[UDP_DATAGRAMSIZE];
    size_t got, wanted = (size_t) luaL_optnumber(L, 2, sizeof(buf));
    char *dgram = wanted > sizeof(buf)? (char *) malloc(wanted): buf;
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    char addrstr[INET6_ADDRSTRLEN];
    char portstr[6];
    int err;
    p_timeout tm = &udp->tm;
    timeout_markstart(tm);
    if (!dgram) {
        lua_pushnil(L);
        lua_pushliteral(L, "out of memory");
        return 2;
    }
    err = socket_recvfrom(&udp->sock, dgram, wanted, &got, (SA *) &addr,
            &addr_len, tm);
    /* Unlike TCP, recv() of zero is not closed, but a zero-length packet. */
    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, udp_strerror(err));
        if (wanted > sizeof(buf)) free(dgram);
        return 2;
    }
    err = getnameinfo((struct sockaddr *)&addr, addr_len, addrstr,
        INET6_ADDRSTRLEN, portstr, 6, NI_NUMERICHOST | NI_NUMERICSERV);
	if (err) {
        lua_pushnil(L);
        lua_pushstring(L, gai_strerror(err));
        if (wanted > sizeof(buf)) free(dgram);
        return 2;
    }
    lua_pushlstring(L, dgram, got);
    lua_pushstring(L, addrstr);
    lua_pushinteger(L, (int) strtol(portstr, (char **) NULL, 10));
    if (wanted > sizeof(buf)) free(dgram);
    return 3;
}

/*-------------------------------------------------------------------------*\
* Returns family as string
\*-------------------------------------------------------------------------*/
static int udp_meth_getfamily(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    if (udp->family == AF_INET6) {
        lua_pushliteral(L, "inet6");
        return 1;
    } else {
        lua_pushliteral(L, "inet4");
        return 1;
    }
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int udp_meth_getfd(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    lua_pushnumber(L, (int) udp->sock);
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int udp_meth_setfd(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    udp->sock = (t_socket) luaL_checknumber(L, 2);
    return 0;
}

static int udp_meth_dirty(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    (void) udp;
    lua_pushboolean(L, 0);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call inet methods
\*-------------------------------------------------------------------------*/
static int udp_meth_getpeername(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkclass(L, "udp{connected}", 1);
    return inet_meth_getpeername(L, &udp->sock, udp->family);
}

static int udp_meth_getsockname(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    return inet_meth_getsockname(L, &udp->sock, udp->family);
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int udp_meth_setoption(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    return opt_meth_setoption(L, optset, &udp->sock);
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int udp_meth_getoption(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    return opt_meth_getoption(L, udpoptget, &udp->sock);
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int udp_meth_settimeout(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    return timeout_meth_settimeout(L, &udp->tm);
}

static int udp_meth_gettimeout(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    return timeout_meth_gettimeout(L, &udp->tm);
}

/*-------------------------------------------------------------------------*\
* Turns a master udp object into a client object.
\*-------------------------------------------------------------------------*/
static int udp_meth_setpeername(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    p_timeout tm = &udp->tm;
    const char *address = luaL_checkstring(L, 2);
    int connecting = strcmp(address, "*");
    const char *port = connecting? luaL_checkstring(L, 3): "0";
    struct addrinfo connecthints;
    const char *err;
    memset(&connecthints, 0, sizeof(connecthints));
    connecthints.ai_socktype = SOCK_DGRAM;
    /* make sure we try to connect only to the same family */
    connecthints.ai_family = udp->family;
    if (connecting) {
        err = inet_tryconnect(&udp->sock, &udp->family, address,
            port, tm, &connecthints);
        if (err) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        auxiliar_setclass(L, "udp{connected}", 1);
    } else {
        /* we ignore possible errors because Mac OS X always
         * returns EAFNOSUPPORT */
        inet_trydisconnect(&udp->sock, udp->family, tm);
        auxiliar_setclass(L, "udp{unconnected}", 1);
    }
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int udp_meth_close(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkgroup(L, "udp{any}", 1);
    socket_destroy(&udp->sock);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Turns a master object into a server object
\*-------------------------------------------------------------------------*/
static int udp_meth_setsockname(lua_State *L) {
    p_udp udp = (p_udp) auxiliar_checkclass(L, "udp{unconnected}", 1);
    const char *address =  luaL_checkstring(L, 2);
    const char *port = luaL_checkstring(L, 3);
    const char *err;
    struct addrinfo bindhints;
    memset(&bindhints, 0, sizeof(bindhints));
    bindhints.ai_socktype = SOCK_DGRAM;
    bindhints.ai_family = udp->family;
    bindhints.ai_flags = AI_PASSIVE;
    err = inet_trybind(&udp->sock, &udp->family, address, port, &bindhints);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master udp object
\*-------------------------------------------------------------------------*/
static int udp_create(lua_State *L, int family) {
    /* allocate udp object */
    p_udp udp = (p_udp) lua_newuserdata(L, sizeof(t_udp));
    auxiliar_setclass(L, "udp{unconnected}", -1);
    /* if family is AF_UNSPEC, we leave the socket invalid and
     * store AF_UNSPEC into family. This will allow it to later be
     * replaced with an AF_INET6 or AF_INET socket upon first use. */
    udp->sock = SOCKET_INVALID;
    timeout_init(&udp->tm, -1, -1);
    udp->family = family;
    if (family != AF_UNSPEC) {
        const char *err = inet_trycreate(&udp->sock, family, SOCK_DGRAM, 0);
        if (err != NULL) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        socket_setnonblocking(&udp->sock);
    }
    return 1;
}

static int udp_global_create(lua_State *L) {
    return udp_create(L, AF_UNSPEC);
}

static int udp_global_create4(lua_State *L) {
    return udp_create(L, AF_INET);
}

static int udp_global_create6(lua_State *L) {
    return udp_create(L, AF_INET6);
}

#ifndef _MSC_VER
/*=========================================================================*\
* Unix domain socket
* LuaSocket toolkit
\*=========================================================================*/
#include "lua.h"
//#include "lauxlib.h"


/*-------------------------------------------------------------------------*\
* Modules and functions
\*-------------------------------------------------------------------------*/
static const luaL_Reg unixmod[] = {
    {"tcp", unixtcp_open},
    {"udp", unixudp_open},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int luaopen_socket_unix(lua_State *L)
{
	int i;
	lua_newtable(L);
    for (i = 0; unixmod[i].name; i++) unixmod[i].func(L);
	return 1;
}

#endif
#ifndef _MSC_VER
/*=========================================================================*\
* Unix domain socket tcp sub module
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>

#include "lua.h"
//#include "lauxlib.h"

#include <sys/un.h>

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int unixtcp_tcp_global_create(lua_State *L);
static int unixtcp_tcp_meth_connect(lua_State *L);
static int unixtcp_tcp_meth_listen(lua_State *L);
static int unixtcp_tcp_meth_bind(lua_State *L);
static int unixtcp_tcp_meth_send(lua_State *L);
static int unixtcp_tcp_meth_shutdown(lua_State *L);
static int unixtcp_tcp_meth_receive(lua_State *L);
static int unixtcp_tcp_meth_accept(lua_State *L);
static int unixtcp_tcp_meth_close(lua_State *L);
static int unixtcp_tcp_meth_setoption(lua_State *L);
static int unixtcp_tcp_meth_settimeout(lua_State *L);
static int unixtcp_tcp_meth_getfd(lua_State *L);
static int unixtcp_tcp_meth_setfd(lua_State *L);
static int unixtcp_tcp_meth_dirty(lua_State *L);
static int unixtcp_tcp_meth_getstats(lua_State *L);
static int unixtcp_tcp_meth_setstats(lua_State *L);
static int unixtcp_tcp_meth_getsockname(lua_State *L);

static const char *unixtcp_tryconnect(p_unix un, const char *path);
static const char *unixtcp_trybind(p_unix un, const char *path);

/* unixtcp object methods */
static luaL_Reg unixtcp_methods[] = {
    {"__gc",        unixtcp_tcp_meth_close},
    {"__tostring",  auxiliar_tostring},
    {"accept",      unixtcp_tcp_meth_accept},
    {"bind",        unixtcp_tcp_meth_bind},
    {"close",       unixtcp_tcp_meth_close},
    {"connect",     unixtcp_tcp_meth_connect},
    {"dirty",       unixtcp_tcp_meth_dirty},
    {"getfd",       unixtcp_tcp_meth_getfd},
    {"getstats",    unixtcp_tcp_meth_getstats},
    {"setstats",    unixtcp_tcp_meth_setstats},
    {"listen",      unixtcp_tcp_meth_listen},
    {"receive",     unixtcp_tcp_meth_receive},
    {"send",        unixtcp_tcp_meth_send},
    {"setfd",       unixtcp_tcp_meth_setfd},
    {"setoption",   unixtcp_tcp_meth_setoption},
    {"setpeername", unixtcp_tcp_meth_connect},
    {"setsockname", unixtcp_tcp_meth_bind},
    {"getsockname", unixtcp_tcp_meth_getsockname},
    {"settimeout",  unixtcp_tcp_meth_settimeout},
    {"shutdown",    unixtcp_tcp_meth_shutdown},
    {NULL,          NULL}
};

/* socket option handlers */
static t_opt optset[] = {
    {"keepalive",   opt_set_keepalive},
    {"reuseaddr",   opt_set_reuseaddr},
    {"linger",      opt_set_linger},
    {NULL,          NULL}
};

/* functions in library namespace */
static luaL_Reg unixtpcfunc[] = {
    {"tcp", unixtcp_tcp_global_create},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int unixtcp_open(lua_State *L)
{
    /* create classes */
    auxiliar_newclass(L, "unixtcp{master}", unixtcp_methods);
    auxiliar_newclass(L, "unixtcp{client}", unixtcp_methods);
    auxiliar_newclass(L, "unixtcp{server}", unixtcp_methods);

    /* create class groups */
    auxiliar_add2group(L, "unixtcp{master}", "unixtcp{any}");
    auxiliar_add2group(L, "unixtcp{client}", "unixtcp{any}");
    auxiliar_add2group(L, "unixtcp{server}", "unixtcp{any}");

    luaL_setfuncs(L, unixtpcfunc, 0);
    return 0;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Just call buffered IO methods
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_send(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixtcp{client}", 1);
    return buffer_meth_send(L, &un->buf);
}

static int unixtcp_tcp_meth_receive(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixtcp{client}", 1);
    return buffer_meth_receive(L, &un->buf);
}

static int unixtcp_tcp_meth_getstats(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixtcp{client}", 1);
    return buffer_meth_getstats(L, &un->buf);
}

static int unixtcp_tcp_meth_setstats(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixtcp{client}", 1);
    return buffer_meth_setstats(L, &un->buf);
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_setoption(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixtcp{any}", 1);
    return opt_meth_setoption(L, optset, &un->sock);
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_getfd(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixtcp{any}", 1);
    lua_pushnumber(L, (int) un->sock);
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int unixtcp_tcp_meth_setfd(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixtcp{any}", 1);
    un->sock = (t_socket) luaL_checknumber(L, 2);
    return 0;
}

static int unixtcp_tcp_meth_dirty(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixtcp{any}", 1);
    lua_pushboolean(L, !buffer_isempty(&un->buf));
    return 1;
}

/*-------------------------------------------------------------------------*\
* Waits for and returns a client object attempting connection to the
* server object
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_accept(lua_State *L) {
    p_unix server = (p_unix) auxiliar_checkclass(L, "unixtcp{server}", 1);
    p_timeout tm = timeout_markstart(&server->tm);
    t_socket sock;
    int err = socket_accept(&server->sock, &sock, NULL, NULL, tm);
    /* if successful, push client socket */
    if (err == IO_DONE) {
        p_unix clnt = (p_unix) lua_newuserdata(L, sizeof(t_unix));
        auxiliar_setclass(L, "unixtcp{client}", -1);
        /* initialize structure fields */
        socket_setnonblocking(&sock);
        clnt->sock = sock;
        io_init(&clnt->io, (p_send)socket_send, (p_recv)socket_recv,
                (p_error) socket_ioerror, &clnt->sock);
        timeout_init(&clnt->tm, -1, -1);
        buffer_init(&clnt->buf, &clnt->io, &clnt->tm);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }
}

/*-------------------------------------------------------------------------*\
* Binds an object to an address
\*-------------------------------------------------------------------------*/
static const char *unixtcp_trybind(p_unix un, const char *path) {
    struct sockaddr_un local;
    size_t len = strlen(path);
    int err;
    if (len >= sizeof(local.sun_path)) return "path too long";
    memset(&local, 0, sizeof(local));
    strcpy(local.sun_path, path);
    local.sun_family = AF_UNIX;
#ifdef UNIX_HAS_SUN_LEN
    local.sun_len = sizeof(local.sun_family) + sizeof(local.sun_len)
        + len + 1;
    err = socket_bind(&un->sock, (SA *) &local, local.sun_len);

#else
    err = socket_bind(&un->sock, (SA *) &local,
            sizeof(local.sun_family) + len);
#endif
    if (err != IO_DONE) socket_destroy(&un->sock);
    return socket_strerror(err);
}

static int unixtcp_tcp_meth_bind(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixtcp{master}", 1);
    const char *path =  luaL_checkstring(L, 2);
    const char *err = unixtcp_trybind(un, path);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

static int unixtcp_tcp_meth_getsockname(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixtcp{any}", 1);
    struct sockaddr_un peer = {0};
    socklen_t peer_len = sizeof(peer);

    if (getsockname(un->sock, (SA *) &peer, &peer_len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
        return 2;
    }

    lua_pushstring(L, peer.sun_path);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Turns a master unixtcp object into a client object.
\*-------------------------------------------------------------------------*/
static const char *unixtcp_tryconnect(p_unix un, const char *path)
{
    struct sockaddr_un remote;
    int err;
    size_t len = strlen(path);
    if (len >= sizeof(remote.sun_path)) return "path too long";
    memset(&remote, 0, sizeof(remote));
    strcpy(remote.sun_path, path);
    remote.sun_family = AF_UNIX;
    timeout_markstart(&un->tm);
#ifdef UNIX_HAS_SUN_LEN
    remote.sun_len = sizeof(remote.sun_family) + sizeof(remote.sun_len)
        + len + 1;
    err = socket_connect(&un->sock, (SA *) &remote, remote.sun_len, &un->tm);
#else
    err = socket_connect(&un->sock, (SA *) &remote,
            sizeof(remote.sun_family) + len, &un->tm);
#endif
    if (err != IO_DONE) socket_destroy(&un->sock);
    return socket_strerror(err);
}

static int unixtcp_tcp_meth_connect(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixtcp{master}", 1);
    const char *path =  luaL_checkstring(L, 2);
    const char *err = unixtcp_tryconnect(un, path);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    /* turn master object into a client object */
    auxiliar_setclass(L, "unixtcp{client}", 1);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_close(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixtcp{any}", 1);
    socket_destroy(&un->sock);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Puts the sockt in listen mode
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_listen(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixtcp{master}", 1);
    int backlog = (int) luaL_optnumber(L, 2, 32);
    int err = socket_listen(&un->sock, backlog);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }
    /* turn master object into a server object */
    auxiliar_setclass(L, "unixtcp{server}", 1);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Shuts the connection down partially
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_shutdown(lua_State *L)
{
    /* SHUT_RD,  SHUT_WR,  SHUT_RDWR  have  the value 0, 1, 2, so we can use method index directly */
    static const char* methods[] = { "receive", "send", "both", NULL };
    p_unix tcp = (p_unix) auxiliar_checkclass(L, "unixtcp{client}", 1);
    int how = luaL_checkoption(L, 2, "both", methods);
    socket_shutdown(&tcp->sock, how);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_meth_settimeout(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixtcp{any}", 1);
    return timeout_meth_settimeout(L, &un->tm);
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master unixtcp object
\*-------------------------------------------------------------------------*/
static int unixtcp_tcp_global_create(lua_State *L) {
    t_socket sock;
    int err = socket_create(&sock, AF_UNIX, SOCK_STREAM, 0);
    /* try to allocate a system socket */
    if (err == IO_DONE) {
        /* allocate unixtcp object */
        p_unix un = (p_unix) lua_newuserdata(L, sizeof(t_unix));
        /* set its type as master object */
        auxiliar_setclass(L, "unixtcp{master}", -1);
        /* initialize remaining structure fields */
        socket_setnonblocking(&sock);
        un->sock = sock;
        io_init(&un->io, (p_send) socket_send, (p_recv) socket_recv,
                (p_error) socket_ioerror, &un->sock);
        timeout_init(&un->tm, -1, -1);
        buffer_init(&un->buf, &un->io, &un->tm);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }
}
#endif
#ifndef _MSC_VER
/*=========================================================================*\
* Unix domain socket udp submodule
* LuaSocket toolkit
\*=========================================================================*/
#include <string.h>
#include <stdlib.h>

#include "lua.h"
//#include "lauxlib.h"

#include <sys/un.h>

#define UNIXUDP_DATAGRAMSIZE 8192

/*=========================================================================*\
* Internal function prototypes
\*=========================================================================*/
static int unixudp_global_create(lua_State *L);
static int unixudp_meth_connect(lua_State *L);
static int unixudp_meth_bind(lua_State *L);
static int unixudp_meth_send(lua_State *L);
static int unixudp_meth_receive(lua_State *L);
static int unixudp_meth_close(lua_State *L);
static int unixudp_meth_setoption(lua_State *L);
static int unixudp_meth_settimeout(lua_State *L);
static int unixudp_meth_gettimeout(lua_State *L);
static int unixudp_meth_getfd(lua_State *L);
static int unixudp_meth_setfd(lua_State *L);
static int unixudp_meth_dirty(lua_State *L);
static int unixudp_meth_receivefrom(lua_State *L);
static int unixudp_meth_sendto(lua_State *L);
static int unixudp_meth_getsockname(lua_State *L);

static const char *unixudp_tryconnect(p_unix un, const char *path);
static const char *unixudp_trybind(p_unix un, const char *path);

/* unixudp object methods */
static luaL_Reg unixudp_methods[] = {
    {"__gc",        unixudp_meth_close},
    {"__tostring",  auxiliar_tostring},
    {"bind",        unixudp_meth_bind},
    {"close",       unixudp_meth_close},
    {"connect",     unixudp_meth_connect},
    {"dirty",       unixudp_meth_dirty},
    {"getfd",       unixudp_meth_getfd},
    {"send",        unixudp_meth_send},
    {"sendto",      unixudp_meth_sendto},
    {"receive",     unixudp_meth_receive},
    {"receivefrom", unixudp_meth_receivefrom},
    {"setfd",       unixudp_meth_setfd},
    {"setoption",   unixudp_meth_setoption},
    {"setpeername", unixudp_meth_connect},
    {"setsockname", unixudp_meth_bind},
    {"getsockname", unixudp_meth_getsockname},
    {"settimeout",  unixudp_meth_settimeout},
    {"gettimeout",  unixudp_meth_gettimeout},
    {NULL,          NULL}
};

/* socket option handlers */
static t_opt optset[] = {
    {"reuseaddr",   opt_set_reuseaddr},
    {NULL,          NULL}
};

/* functions in library namespace */
static luaL_Reg unixudpfunc[] = {
    {"udp", unixudp_global_create},
    {NULL, NULL}
};

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int unixudp_open(lua_State *L)
{
    /* create classes */
    auxiliar_newclass(L, "unixudp{connected}", unixudp_methods);
    auxiliar_newclass(L, "unixudp{unconnected}", unixudp_methods);
    /* create class groups */
    auxiliar_add2group(L, "unixudp{connected}",   "unixudp{any}");
    auxiliar_add2group(L, "unixudp{unconnected}", "unixudp{any}");
    auxiliar_add2group(L, "unixudp{connected}",   "select{able}");
    auxiliar_add2group(L, "unixudp{unconnected}", "select{able}");

    luaL_setfuncs(L, unixudpfunc, 0);
    return 0;
}

/*=========================================================================*\
* Lua methods
\*=========================================================================*/
static const char *unixudp_strerror(int err)
{
    /* a 'closed' error on an unconnected means the target address was not
     * accepted by the transport layer */
    if (err == IO_CLOSED) return "refused";
    else return socket_strerror(err);
}

static int unixudp_meth_send(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixudp{connected}", 1);
    p_timeout tm = &un->tm;
    size_t count, sent = 0;
    int err;
    const char *data = luaL_checklstring(L, 2, &count);
    timeout_markstart(tm);
    err = socket_send(&un->sock, data, count, &sent, tm);
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, unixudp_strerror(err));
        return 2;
    }
    lua_pushnumber(L, (lua_Number) sent);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Send data through unconnected unixudp socket
\*-------------------------------------------------------------------------*/
static int unixudp_meth_sendto(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixudp{unconnected}", 1);
    size_t count, sent = 0;
    const char *data = luaL_checklstring(L, 2, &count);
    const char *path = luaL_checkstring(L, 3);
    p_timeout tm = &un->tm;
    int err;
    struct sockaddr_un remote;
    size_t len = strlen(path);

    if (len >= sizeof(remote.sun_path)) {
		lua_pushnil(L);
		lua_pushstring(L, "path too long");
		return 2;
	}

    memset(&remote, 0, sizeof(remote));
    strcpy(remote.sun_path, path);
    remote.sun_family = AF_UNIX;
    timeout_markstart(tm);
#ifdef UNIX_HAS_SUN_LEN
    remote.sun_len = sizeof(remote.sun_family) + sizeof(remote.sun_len)
        + len + 1;
    err = socket_sendto(&un->sock, data, count, &sent, (SA *) &remote, remote.sun_len, tm);
#else
    err = socket_sendto(&un->sock, data, count, &sent, (SA *) &remote,
		   	sizeof(remote.sun_family) + len, tm);
#endif
    if (err != IO_DONE) {
        lua_pushnil(L);
        lua_pushstring(L, unixudp_strerror(err));
        return 2;
    }
    lua_pushnumber(L, (lua_Number) sent);
    return 1;
}

static int unixudp_meth_receive(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    char buf[UNIXUDP_DATAGRAMSIZE];
    size_t got, wanted = (size_t) luaL_optnumber(L, 2, sizeof(buf));
    char *dgram = wanted > sizeof(buf)? (char *) malloc(wanted): buf;
    int err;
    p_timeout tm = &un->tm;
    timeout_markstart(tm);
    if (!dgram) {
        lua_pushnil(L);
        lua_pushliteral(L, "out of memory");
        return 2;
    }
    err = socket_recv(&un->sock, dgram, wanted, &got, tm);
    /* Unlike TCP, recv() of zero is not closed, but a zero-length packet. */
    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, unixudp_strerror(err));
        if (wanted > sizeof(buf)) free(dgram);
        return 2;
    }
    lua_pushlstring(L, dgram, got);
    if (wanted > sizeof(buf)) free(dgram);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Receives data and sender from a UDP socket
\*-------------------------------------------------------------------------*/
static int unixudp_meth_receivefrom(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixudp{unconnected}", 1);
    char buf[UNIXUDP_DATAGRAMSIZE];
    size_t got, wanted = (size_t) luaL_optnumber(L, 2, sizeof(buf));
    char *dgram = wanted > sizeof(buf)? (char *) malloc(wanted): buf;
    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);
    int err;
    p_timeout tm = &un->tm;
    timeout_markstart(tm);
    if (!dgram) {
        lua_pushnil(L);
        lua_pushliteral(L, "out of memory");
        return 2;
    }
    err = socket_recvfrom(&un->sock, dgram, wanted, &got, (SA *) &addr,
            &addr_len, tm);
    /* Unlike TCP, recv() of zero is not closed, but a zero-length packet. */
    if (err != IO_DONE && err != IO_CLOSED) {
        lua_pushnil(L);
        lua_pushstring(L, unixudp_strerror(err));
        if (wanted > sizeof(buf)) free(dgram);
        return 2;
    }

    lua_pushlstring(L, dgram, got);
	/* the path may be empty, when client send without bind */
    lua_pushstring(L, addr.sun_path);
    if (wanted > sizeof(buf)) free(dgram);
    return 2;
}

/*-------------------------------------------------------------------------*\
* Just call option handler
\*-------------------------------------------------------------------------*/
static int unixudp_meth_setoption(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    return opt_meth_setoption(L, optset, &un->sock);
}

/*-------------------------------------------------------------------------*\
* Select support methods
\*-------------------------------------------------------------------------*/
static int unixudp_meth_getfd(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    lua_pushnumber(L, (int) un->sock);
    return 1;
}

/* this is very dangerous, but can be handy for those that are brave enough */
static int unixudp_meth_setfd(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    un->sock = (t_socket) luaL_checknumber(L, 2);
    return 0;
}

static int unixudp_meth_dirty(lua_State *L) {
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    (void) un;
    lua_pushboolean(L, 0);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Binds an object to an address
\*-------------------------------------------------------------------------*/
static const char *unixudp_trybind(p_unix un, const char *path) {
    struct sockaddr_un local;
    size_t len = strlen(path);
    int err;
    if (len >= sizeof(local.sun_path)) return "path too long";
    memset(&local, 0, sizeof(local));
    strcpy(local.sun_path, path);
    local.sun_family = AF_UNIX;
#ifdef UNIX_HAS_SUN_LEN
    local.sun_len = sizeof(local.sun_family) + sizeof(local.sun_len)
        + len + 1;
    err = socket_bind(&un->sock, (SA *) &local, local.sun_len);

#else
    err = socket_bind(&un->sock, (SA *) &local,
            sizeof(local.sun_family) + len);
#endif
    if (err != IO_DONE) socket_destroy(&un->sock);
    return socket_strerror(err);
}

static int unixudp_meth_bind(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkclass(L, "unixudp{unconnected}", 1);
    const char *path =  luaL_checkstring(L, 2);
    const char *err = unixudp_trybind(un, path);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushnumber(L, 1);
    return 1;
}

static int unixudp_meth_getsockname(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    struct sockaddr_un peer = {0};
    socklen_t peer_len = sizeof(peer);

    if (getsockname(un->sock, (SA *) &peer, &peer_len) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(errno));
        return 2;
    }

    lua_pushstring(L, peer.sun_path);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Turns a master unixudp object into a client object.
\*-------------------------------------------------------------------------*/
static const char *unixudp_tryconnect(p_unix un, const char *path)
{
    struct sockaddr_un remote;
    int err;
    size_t len = strlen(path);
    if (len >= sizeof(remote.sun_path)) return "path too long";
    memset(&remote, 0, sizeof(remote));
    strcpy(remote.sun_path, path);
    remote.sun_family = AF_UNIX;
    timeout_markstart(&un->tm);
#ifdef UNIX_HAS_SUN_LEN
    remote.sun_len = sizeof(remote.sun_family) + sizeof(remote.sun_len)
        + len + 1;
    err = socket_connect(&un->sock, (SA *) &remote, remote.sun_len, &un->tm);
#else
    err = socket_connect(&un->sock, (SA *) &remote,
            sizeof(remote.sun_family) + len, &un->tm);
#endif
    if (err != IO_DONE) socket_destroy(&un->sock);
    return socket_strerror(err);
}

static int unixudp_meth_connect(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    const char *path =  luaL_checkstring(L, 2);
    const char *err = unixudp_tryconnect(un, path);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    /* turn unconnected object into a connected object */
    auxiliar_setclass(L, "unixudp{connected}", 1);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Closes socket used by object
\*-------------------------------------------------------------------------*/
static int unixudp_meth_close(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    socket_destroy(&un->sock);
    lua_pushnumber(L, 1);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Just call tm methods
\*-------------------------------------------------------------------------*/
static int unixudp_meth_settimeout(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    return timeout_meth_settimeout(L, &un->tm);
}

static int unixudp_meth_gettimeout(lua_State *L)
{
    p_unix un = (p_unix) auxiliar_checkgroup(L, "unixudp{any}", 1);
    return timeout_meth_gettimeout(L, &un->tm);
}

/*=========================================================================*\
* Library functions
\*=========================================================================*/
/*-------------------------------------------------------------------------*\
* Creates a master unixudp object
\*-------------------------------------------------------------------------*/
static int unixudp_global_create(lua_State *L)
{
    t_socket sock;
    int err = socket_create(&sock, AF_UNIX, SOCK_DGRAM, 0);
    /* try to allocate a system socket */
    if (err == IO_DONE) {
        /* allocate unixudp object */
        p_unix un = (p_unix) lua_newuserdata(L, sizeof(t_unix));
        /* set its type as master object */
        auxiliar_setclass(L, "unixudp{unconnected}", -1);
        /* initialize remaining structure fields */
        socket_setnonblocking(&sock);
        un->sock = sock;
        io_init(&un->io, (p_send) socket_send, (p_recv) socket_recv,
                (p_error) socket_ioerror, &un->sock);
        timeout_init(&un->tm, -1, -1);
        buffer_init(&un->buf, &un->io, &un->tm);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, socket_strerror(err));
        return 2;
    }
}
#endif
/*=========================================================================*\
* Socket compatibilization module for Unix
* LuaSocket toolkit
*
* The code is now interrupt-safe.
* The penalty of calling select to avoid busy-wait is only paid when
* the I/O call fail in the first place.
\*=========================================================================*/
#ifndef _MSC_VER
#include <string.h>
#include <signal.h>


/*-------------------------------------------------------------------------*\
* Wait for readable/writable/connected socket with timeout
\*-------------------------------------------------------------------------*/
#ifndef SOCKET_SELECT
#include <sys/poll.h>

#define WAITFD_R        POLLIN
#define WAITFD_W        POLLOUT
#define WAITFD_C        (POLLIN|POLLOUT)
int socket_waitfd(p_socket ps, int sw, p_timeout tm) {
    int ret;
    struct pollfd pfd;
    pfd.fd = *ps;
    pfd.events = sw;
    pfd.revents = 0;
    if (timeout_iszero(tm)) return IO_TIMEOUT;  /* optimize timeout == 0 case */
    do {
        int t = (int)(timeout_getretry(tm)*1e3);
        ret = poll(&pfd, 1, t >= 0? t: -1);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1) return errno;
    if (ret == 0) return IO_TIMEOUT;
    if (sw == WAITFD_C && (pfd.revents & (POLLIN|POLLERR))) return IO_CLOSED;
    return IO_DONE;
}
#else

#define WAITFD_R        1
#define WAITFD_W        2
#define WAITFD_C        (WAITFD_R|WAITFD_W)

int socket_waitfd(p_socket ps, int sw, p_timeout tm) {
    int ret;
    fd_set rfds, wfds, *rp, *wp;
    struct timeval tv, *tp;
    double t;
    if (*ps >= FD_SETSIZE) return EINVAL;
    if (timeout_iszero(tm)) return IO_TIMEOUT;  /* optimize timeout == 0 case */
    do {
        /* must set bits within loop, because select may have modifed them */
        rp = wp = NULL;
        if (sw & WAITFD_R) { FD_ZERO(&rfds); FD_SET(*ps, &rfds); rp = &rfds; }
        if (sw & WAITFD_W) { FD_ZERO(&wfds); FD_SET(*ps, &wfds); wp = &wfds; }
        t = timeout_getretry(tm);
        tp = NULL;
        if (t >= 0.0) {
            tv.tv_sec = (int)t;
            tv.tv_usec = (int)((t-tv.tv_sec)*1.0e6);
            tp = &tv;
        }
        ret = select(*ps+1, rp, wp, NULL, tp);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1) return errno;
    if (ret == 0) return IO_TIMEOUT;
    if (sw == WAITFD_C && FD_ISSET(*ps, &rfds)) return IO_CLOSED;
    return IO_DONE;
}
#endif


/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int socket_open(void) {
    /* instals a handler to ignore sigpipe or it will crash us */
    signal(SIGPIPE, SIG_IGN);
    return 1;
}

/*-------------------------------------------------------------------------*\
* Close module
\*-------------------------------------------------------------------------*/
int socket_close(void) {
    return 1;
}

/*-------------------------------------------------------------------------*\
* Close and inutilize socket
\*-------------------------------------------------------------------------*/
void socket_destroy(p_socket ps) {
    if (*ps != SOCKET_INVALID) {
        close(*ps);
        *ps = SOCKET_INVALID;
    }
}

/*-------------------------------------------------------------------------*\
* Select with timeout control
\*-------------------------------------------------------------------------*/
int socket_select(t_socket n, fd_set *rfds, fd_set *wfds, fd_set *efds,
        p_timeout tm) {
    int ret;
    do {
        struct timeval tv;
        double t = timeout_getretry(tm);
        tv.tv_sec = (int) t;
        tv.tv_usec = (int) ((t - tv.tv_sec) * 1.0e6);
        /* timeout = 0 means no wait */
        ret = select(n, rfds, wfds, efds, t >= 0.0 ? &tv: NULL);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

/*-------------------------------------------------------------------------*\
* Creates and sets up a socket
\*-------------------------------------------------------------------------*/
int socket_create(p_socket ps, int domain, int type, int protocol) {
    *ps = socket(domain, type, protocol);
    if (*ps != SOCKET_INVALID) return IO_DONE;
    else return errno;
}

/*-------------------------------------------------------------------------*\
* Binds or returns error message
\*-------------------------------------------------------------------------*/
int socket_bind(p_socket ps, SA *addr, socklen_t len) {
    int err = IO_DONE;
    socket_setblocking(ps);
    if (bind(*ps, addr, len) < 0) err = errno;
    socket_setnonblocking(ps);
    return err;
}

/*-------------------------------------------------------------------------*\
*
\*-------------------------------------------------------------------------*/
int socket_listen(p_socket ps, int backlog) {
    int err = IO_DONE;
    if (listen(*ps, backlog)) err = errno;
    return err;
}

/*-------------------------------------------------------------------------*\
*
\*-------------------------------------------------------------------------*/
void socket_shutdown(p_socket ps, int how) {
    shutdown(*ps, how);
}

/*-------------------------------------------------------------------------*\
* Connects or returns error message
\*-------------------------------------------------------------------------*/
int socket_connect(p_socket ps, SA *addr, socklen_t len, p_timeout tm) {
    int err;
    /* avoid calling on closed sockets */
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    /* call connect until done or failed without being interrupted */
    do if (connect(*ps, addr, len) == 0) return IO_DONE;
    while ((err = errno) == EINTR);
    /* if connection failed immediately, return error code */
    if (err != EINPROGRESS && err != EAGAIN) return err;
    /* zero timeout case optimization */
    if (timeout_iszero(tm)) return IO_TIMEOUT;
    /* wait until we have the result of the connection attempt or timeout */
    err = socket_waitfd(ps, WAITFD_C, tm);
    if (err == IO_CLOSED) {
        if (recv(*ps, (char *) &err, 0, 0) == 0) return IO_DONE;
        else return errno;
    } else return err;
}

/*-------------------------------------------------------------------------*\
* Accept with timeout
\*-------------------------------------------------------------------------*/
int socket_accept(p_socket ps, p_socket pa, SA *addr, socklen_t *len, p_timeout tm) {
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        int err;
        if ((*pa = accept(*ps, addr, len)) != SOCKET_INVALID) return IO_DONE;
        err = errno;
        if (err == EINTR) continue;
        if (err != EAGAIN && err != ECONNABORTED) return err;
        if ((err = socket_waitfd(ps, WAITFD_R, tm)) != IO_DONE) return err;
    }
    /* can't reach here */
    return IO_UNKNOWN;
}

/*-------------------------------------------------------------------------*\
* Send with timeout
\*-------------------------------------------------------------------------*/
int socket_send(p_socket ps, const char *data, size_t count,
        size_t *sent, p_timeout tm)
{
    int err;
    *sent = 0;
    /* avoid making system calls on closed sockets */
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    /* loop until we send something or we give up on error */
    for ( ;; ) {
        long put = (long) send(*ps, data, count, 0);
        /* if we sent anything, we are done */
        if (put >= 0) {
            *sent = put;
            return IO_DONE;
        }
        err = errno;
        /* EPIPE means the connection was closed */
        if (err == EPIPE) return IO_CLOSED;
        /* EPROTOTYPE means the connection is being closed (on Yosemite!)*/
        if (err == EPROTOTYPE) continue;
        /* we call was interrupted, just try again */
        if (err == EINTR) continue;
        /* if failed fatal reason, report error */
        if (err != EAGAIN) return err;
        /* wait until we can send something or we timeout */
        if ((err = socket_waitfd(ps, WAITFD_W, tm)) != IO_DONE) return err;
    }
    /* can't reach here */
    return IO_UNKNOWN;
}

/*-------------------------------------------------------------------------*\
* Sendto with timeout
\*-------------------------------------------------------------------------*/
int socket_sendto(p_socket ps, const char *data, size_t count, size_t *sent,
        SA *addr, socklen_t len, p_timeout tm)
{
    int err;
    *sent = 0;
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        long put = (long) sendto(*ps, data, count, 0, addr, len); 
        if (put >= 0) {
            *sent = put;
            return IO_DONE;
        }
        err = errno;
        if (err == EPIPE) return IO_CLOSED;
        if (err == EPROTOTYPE) continue;
        if (err == EINTR) continue;
        if (err != EAGAIN) return err;
        if ((err = socket_waitfd(ps, WAITFD_W, tm)) != IO_DONE) return err;
    }
    return IO_UNKNOWN;
}

/*-------------------------------------------------------------------------*\
* Receive with timeout
\*-------------------------------------------------------------------------*/
int socket_recv(p_socket ps, char *data, size_t count, size_t *got, p_timeout tm) {
    int err;
    *got = 0;
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        long taken = (long) recv(*ps, data, count, 0);
        if (taken > 0) {
            *got = taken;
            return IO_DONE;
        }
        err = errno;
        if (taken == 0) return IO_CLOSED;
        if (err == EINTR) continue;
        if (err != EAGAIN) return err;
        if ((err = socket_waitfd(ps, WAITFD_R, tm)) != IO_DONE) return err;
    }
    return IO_UNKNOWN;
}

/*-------------------------------------------------------------------------*\
* Recvfrom with timeout
\*-------------------------------------------------------------------------*/
int socket_recvfrom(p_socket ps, char *data, size_t count, size_t *got,
        SA *addr, socklen_t *len, p_timeout tm) {
    int err;
    *got = 0;
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        long taken = (long) recvfrom(*ps, data, count, 0, addr, len);
        if (taken > 0) {
            *got = taken;
            return IO_DONE;
        }
        err = errno;
        if (taken == 0) return IO_CLOSED;
        if (err == EINTR) continue;
        if (err != EAGAIN) return err;
        if ((err = socket_waitfd(ps, WAITFD_R, tm)) != IO_DONE) return err;
    }
    return IO_UNKNOWN;
}


/*-------------------------------------------------------------------------*\
* Write with timeout
*
* socket_read and socket_write are cut-n-paste of socket_send and socket_recv,
* with send/recv replaced with write/read. We can't just use write/read
* in the socket version, because behaviour when size is zero is different.
\*-------------------------------------------------------------------------*/
int socket_write(p_socket ps, const char *data, size_t count,
        size_t *sent, p_timeout tm)
{
    int err;
    *sent = 0;
    /* avoid making system calls on closed sockets */
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    /* loop until we send something or we give up on error */
    for ( ;; ) {
        long put = (long) write(*ps, data, count);
        /* if we sent anything, we are done */
        if (put >= 0) {
            *sent = put;
            return IO_DONE;
        }
        err = errno;
        /* EPIPE means the connection was closed */
        if (err == EPIPE) return IO_CLOSED;
        /* EPROTOTYPE means the connection is being closed (on Yosemite!)*/
        if (err == EPROTOTYPE) continue;
        /* we call was interrupted, just try again */
        if (err == EINTR) continue;
        /* if failed fatal reason, report error */
        if (err != EAGAIN) return err;
        /* wait until we can send something or we timeout */
        if ((err = socket_waitfd(ps, WAITFD_W, tm)) != IO_DONE) return err;
    }
    /* can't reach here */
    return IO_UNKNOWN;
}

/*-------------------------------------------------------------------------*\
* Read with timeout
* See note for socket_write
\*-------------------------------------------------------------------------*/
int socket_read(p_socket ps, char *data, size_t count, size_t *got, p_timeout tm) {
    int err;
    *got = 0;
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        long taken = (long) read(*ps, data, count);
        if (taken > 0) {
            *got = taken;
            return IO_DONE;
        }
        err = errno;
        if (taken == 0) return IO_CLOSED;
        if (err == EINTR) continue;
        if (err != EAGAIN) return err;
        if ((err = socket_waitfd(ps, WAITFD_R, tm)) != IO_DONE) return err;
    }
    return IO_UNKNOWN;
}

/*-------------------------------------------------------------------------*\
* Put socket into blocking mode
\*-------------------------------------------------------------------------*/
void socket_setblocking(p_socket ps) {
    int flags = fcntl(*ps, F_GETFL, 0);
    flags &= (~(O_NONBLOCK));
    fcntl(*ps, F_SETFL, flags);
}

/*-------------------------------------------------------------------------*\
* Put socket into non-blocking mode
\*-------------------------------------------------------------------------*/
void socket_setnonblocking(p_socket ps) {
    int flags = fcntl(*ps, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(*ps, F_SETFL, flags);
}

/*-------------------------------------------------------------------------*\
* DNS helpers
\*-------------------------------------------------------------------------*/
int socket_gethostbyaddr(const char *addr, socklen_t len, struct hostent **hp) {
    *hp = gethostbyaddr(addr, len, AF_INET);
    if (*hp) return IO_DONE;
    else if (h_errno) return h_errno;
    else if (errno) return errno;
    else return IO_UNKNOWN;
}

int socket_gethostbyname(const char *addr, struct hostent **hp) {
    *hp = gethostbyname(addr);
    if (*hp) return IO_DONE;
    else if (h_errno) return h_errno;
    else if (errno) return errno;
    else return IO_UNKNOWN;
}

/*-------------------------------------------------------------------------*\
* Error translation functions
* Make sure important error messages are standard
\*-------------------------------------------------------------------------*/
const char *socket_hoststrerror(int err) {
    if (err <= 0) return io_strerror(err);
    switch (err) {
        case HOST_NOT_FOUND: return PIE_HOST_NOT_FOUND;
        default: return hstrerror(err);
    }
}

const char *socket_strerror(int err) {
    if (err <= 0) return io_strerror(err);
    switch (err) {
        case EADDRINUSE: return PIE_ADDRINUSE;
        case EISCONN: return PIE_ISCONN;
        case EACCES: return PIE_ACCESS;
        case ECONNREFUSED: return PIE_CONNREFUSED;
        case ECONNABORTED: return PIE_CONNABORTED;
        case ECONNRESET: return PIE_CONNRESET;
        case ETIMEDOUT: return PIE_TIMEDOUT;
        default: {
            return strerror(err);
        }
    }
}

const char *socket_ioerror(p_socket ps, int err) {
    (void) ps;
    return socket_strerror(err);
}

const char *socket_gaistrerror(int err) {
    if (err == 0) return NULL;
    switch (err) {
        case EAI_AGAIN: return PIE_AGAIN;
        case EAI_BADFLAGS: return PIE_BADFLAGS;
#ifdef EAI_BADHINTS
        case EAI_BADHINTS: return PIE_BADHINTS;
#endif
        case EAI_FAIL: return PIE_FAIL;
        case EAI_FAMILY: return PIE_FAMILY;
        case EAI_MEMORY: return PIE_MEMORY;
        case EAI_NONAME: return PIE_NONAME;
        case EAI_OVERFLOW: return PIE_OVERFLOW;
#ifdef EAI_PROTOCOL
        case EAI_PROTOCOL: return PIE_PROTOCOL;
#endif
        case EAI_SERVICE: return PIE_SERVICE;
        case EAI_SOCKTYPE: return PIE_SOCKTYPE;
        case EAI_SYSTEM: return strerror(errno);
        default: return gai_strerror(err);
    }
}

#endif
/*=========================================================================*\
* Socket compatibilization module for Win32
* LuaSocket toolkit
*
* The penalty of calling select to avoid busy-wait is only paid when
* the I/O call fail in the first place.
\*=========================================================================*/
#ifdef _MSC_VER
#include <string.h>


/* WinSock doesn't have a strerror... */
static const char *wstrerror(int err);

/*-------------------------------------------------------------------------*\
* Initializes module
\*-------------------------------------------------------------------------*/
int socket_open(void) {
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 0);
    int err = WSAStartup(wVersionRequested, &wsaData );
    if (err != 0) return 0;
    if ((LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 0) &&
        (LOBYTE(wsaData.wVersion) != 1 || HIBYTE(wsaData.wVersion) != 1)) {
        WSACleanup();
        return 0;
    }
    return 1;
}

/*-------------------------------------------------------------------------*\
* Close module
\*-------------------------------------------------------------------------*/
int socket_close(void) {
    WSACleanup();
    return 1;
}

/*-------------------------------------------------------------------------*\
* Wait for readable/writable/connected socket with timeout
\*-------------------------------------------------------------------------*/
#define WAITFD_R        1
#define WAITFD_W        2
#define WAITFD_E        4
#define WAITFD_C        (WAITFD_E|WAITFD_W)

int socket_waitfd(p_socket ps, int sw, p_timeout tm) {
    int ret;
    fd_set rfds, wfds, efds, *rp = NULL, *wp = NULL, *ep = NULL;
    struct timeval tv, *tp = NULL;
    double t;
    if (timeout_iszero(tm)) return IO_TIMEOUT;  /* optimize timeout == 0 case */
    if (sw & WAITFD_R) {
        FD_ZERO(&rfds);
        FD_SET(*ps, &rfds);
        rp = &rfds;
    }
    if (sw & WAITFD_W) { FD_ZERO(&wfds); FD_SET(*ps, &wfds); wp = &wfds; }
    if (sw & WAITFD_C) { FD_ZERO(&efds); FD_SET(*ps, &efds); ep = &efds; }
    if ((t = timeout_get(tm)) >= 0.0) {
        tv.tv_sec = (int) t;
        tv.tv_usec = (int) ((t-tv.tv_sec)*1.0e6);
        tp = &tv;
    }
    ret = select(0, rp, wp, ep, tp);
    if (ret == -1) return WSAGetLastError();
    if (ret == 0) return IO_TIMEOUT;
    if (sw == WAITFD_C && FD_ISSET(*ps, &efds)) return IO_CLOSED;
    return IO_DONE;
}

/*-------------------------------------------------------------------------*\
* Select with int timeout in ms
\*-------------------------------------------------------------------------*/
int socket_select(t_socket n, fd_set *rfds, fd_set *wfds, fd_set *efds,
        p_timeout tm) {
    struct timeval tv;
    double t = timeout_get(tm);
    tv.tv_sec = (int) t;
    tv.tv_usec = (int) ((t - tv.tv_sec) * 1.0e6);
    if (n <= 0) {
        Sleep((DWORD) (1000*t));
        return 0;
    } else return select(0, rfds, wfds, efds, t >= 0.0? &tv: NULL);
}

/*-------------------------------------------------------------------------*\
* Close and inutilize socket
\*-------------------------------------------------------------------------*/
void socket_destroy(p_socket ps) {
    if (*ps != SOCKET_INVALID) {
        socket_setblocking(ps); /* close can take a long time on WIN32 */
        closesocket(*ps);
        *ps = SOCKET_INVALID;
    }
}

/*-------------------------------------------------------------------------*\
*
\*-------------------------------------------------------------------------*/
void socket_shutdown(p_socket ps, int how) {
    socket_setblocking(ps);
    shutdown(*ps, how);
    socket_setnonblocking(ps);
}

/*-------------------------------------------------------------------------*\
* Creates and sets up a socket
\*-------------------------------------------------------------------------*/
int socket_create(p_socket ps, int domain, int type, int protocol) {
    *ps = socket(domain, type, protocol);
    if (*ps != SOCKET_INVALID) return IO_DONE;
    else return WSAGetLastError();
}

/*-------------------------------------------------------------------------*\
* Connects or returns error message
\*-------------------------------------------------------------------------*/
int socket_connect(p_socket ps, SA *addr, socklen_t len, p_timeout tm) {
    int err;
    /* don't call on closed socket */
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    /* ask system to connect */
    if (connect(*ps, addr, len) == 0) return IO_DONE;
    /* make sure the system is trying to connect */
    err = WSAGetLastError();
    if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) return err;
    /* zero timeout case optimization */
    if (timeout_iszero(tm)) return IO_TIMEOUT;
    /* we wait until something happens */
    err = socket_waitfd(ps, WAITFD_C, tm);
    if (err == IO_CLOSED) {
        int len = sizeof(err);
        /* give windows time to set the error (yes, disgusting) */
        Sleep(10);
        /* find out why we failed */
        getsockopt(*ps, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
        /* we KNOW there was an error. if 'why' is 0, we will return
        * "unknown error", but it's not really our fault */
        return err > 0? err: IO_UNKNOWN;
    } else return err;

}

/*-------------------------------------------------------------------------*\
* Binds or returns error message
\*-------------------------------------------------------------------------*/
int socket_bind(p_socket ps, SA *addr, socklen_t len) {
    int err = IO_DONE;
    socket_setblocking(ps);
    if (bind(*ps, addr, len) < 0) err = WSAGetLastError();
    socket_setnonblocking(ps);
    return err;
}

/*-------------------------------------------------------------------------*\
*
\*-------------------------------------------------------------------------*/
int socket_listen(p_socket ps, int backlog) {
    int err = IO_DONE;
    socket_setblocking(ps);
    if (listen(*ps, backlog) < 0) err = WSAGetLastError();
    socket_setnonblocking(ps);
    return err;
}

/*-------------------------------------------------------------------------*\
* Accept with timeout
\*-------------------------------------------------------------------------*/
int socket_accept(p_socket ps, p_socket pa, SA *addr, socklen_t *len,
        p_timeout tm) {
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        int err;
        /* try to get client socket */
        if ((*pa = accept(*ps, addr, len)) != SOCKET_INVALID) return IO_DONE;
        /* find out why we failed */
        err = WSAGetLastError();
        /* if we failed because there was no connectoin, keep trying */
        if (err != WSAEWOULDBLOCK && err != WSAECONNABORTED) return err;
        /* call select to avoid busy wait */
        if ((err = socket_waitfd(ps, WAITFD_R, tm)) != IO_DONE) return err;
    }
}

/*-------------------------------------------------------------------------*\
* Send with timeout
* On windows, if you try to send 10MB, the OS will buffer EVERYTHING
* this can take an awful lot of time and we will end up blocked.
* Therefore, whoever calls this function should not pass a huge buffer.
\*-------------------------------------------------------------------------*/
int socket_send(p_socket ps, const char *data, size_t count,
        size_t *sent, p_timeout tm)
{
    int err;
    *sent = 0;
    /* avoid making system calls on closed sockets */
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    /* loop until we send something or we give up on error */
    for ( ;; ) {
        /* try to send something */
        int put = send(*ps, data, (int) count, 0);
        /* if we sent something, we are done */
        if (put > 0) {
            *sent = put;
            return IO_DONE;
        }
        /* deal with failure */
        err = WSAGetLastError();
        /* we can only proceed if there was no serious error */
        if (err != WSAEWOULDBLOCK) return err;
        /* avoid busy wait */
        if ((err = socket_waitfd(ps, WAITFD_W, tm)) != IO_DONE) return err;
    }
}

/*-------------------------------------------------------------------------*\
* Sendto with timeout
\*-------------------------------------------------------------------------*/
int socket_sendto(p_socket ps, const char *data, size_t count, size_t *sent,
        SA *addr, socklen_t len, p_timeout tm)
{
    int err;
    *sent = 0;
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        int put = sendto(*ps, data, (int) count, 0, addr, len);
        if (put > 0) {
            *sent = put;
            return IO_DONE;
        }
        err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) return err;
        if ((err = socket_waitfd(ps, WAITFD_W, tm)) != IO_DONE) return err;
    }
}

/*-------------------------------------------------------------------------*\
* Receive with timeout
\*-------------------------------------------------------------------------*/
int socket_recv(p_socket ps, char *data, size_t count, size_t *got,
        p_timeout tm)
{
    int err, prev = IO_DONE;
    *got = 0;
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        int taken = recv(*ps, data, (int) count, 0);
        if (taken > 0) {
            *got = taken;
            return IO_DONE;
        }
        if (taken == 0) return IO_CLOSED;
        err = WSAGetLastError();
        /* On UDP, a connreset simply means the previous send failed.
         * So we try again.
         * On TCP, it means our socket is now useless, so the error passes.
         * (We will loop again, exiting because the same error will happen) */
        if (err != WSAEWOULDBLOCK) {
            if (err != WSAECONNRESET || prev == WSAECONNRESET) return err;
            prev = err;
        }
        if ((err = socket_waitfd(ps, WAITFD_R, tm)) != IO_DONE) return err;
    }
}

/*-------------------------------------------------------------------------*\
* Recvfrom with timeout
\*-------------------------------------------------------------------------*/
int socket_recvfrom(p_socket ps, char *data, size_t count, size_t *got,
        SA *addr, socklen_t *len, p_timeout tm)
{
    int err, prev = IO_DONE;
    *got = 0;
    if (*ps == SOCKET_INVALID) return IO_CLOSED;
    for ( ;; ) {
        int taken = recvfrom(*ps, data, (int) count, 0, addr, len);
        if (taken > 0) {
            *got = taken;
            return IO_DONE;
        }
        if (taken == 0) return IO_CLOSED;
        err = WSAGetLastError();
        /* On UDP, a connreset simply means the previous send failed.
         * So we try again.
         * On TCP, it means our socket is now useless, so the error passes.
         * (We will loop again, exiting because the same error will happen) */
        if (err != WSAEWOULDBLOCK) {
            if (err != WSAECONNRESET || prev == WSAECONNRESET) return err;
            prev = err;
        }
        if ((err = socket_waitfd(ps, WAITFD_R, tm)) != IO_DONE) return err;
    }
}

/*-------------------------------------------------------------------------*\
* Put socket into blocking mode
\*-------------------------------------------------------------------------*/
void socket_setblocking(p_socket ps) {
    u_long argp = 0;
    ioctlsocket(*ps, FIONBIO, &argp);
}

/*-------------------------------------------------------------------------*\
* Put socket into non-blocking mode
\*-------------------------------------------------------------------------*/
void socket_setnonblocking(p_socket ps) {
    u_long argp = 1;
    ioctlsocket(*ps, FIONBIO, &argp);
}

/*-------------------------------------------------------------------------*\
* DNS helpers
\*-------------------------------------------------------------------------*/
int socket_gethostbyaddr(const char *addr, socklen_t len, struct hostent **hp) {
    *hp = gethostbyaddr(addr, len, AF_INET);
    if (*hp) return IO_DONE;
    else return WSAGetLastError();
}

int socket_gethostbyname(const char *addr, struct hostent **hp) {
    *hp = gethostbyname(addr);
    if (*hp) return IO_DONE;
    else return  WSAGetLastError();
}

/*-------------------------------------------------------------------------*\
* Error translation functions
\*-------------------------------------------------------------------------*/
const char *socket_hoststrerror(int err) {
    if (err <= 0) return io_strerror(err);
    switch (err) {
        case WSAHOST_NOT_FOUND: return PIE_HOST_NOT_FOUND;
        default: return wstrerror(err);
    }
}

const char *socket_strerror(int err) {
    if (err <= 0) return io_strerror(err);
    switch (err) {
        case WSAEADDRINUSE: return PIE_ADDRINUSE;
        case WSAECONNREFUSED : return PIE_CONNREFUSED;
        case WSAEISCONN: return PIE_ISCONN;
        case WSAEACCES: return PIE_ACCESS;
        case WSAECONNABORTED: return PIE_CONNABORTED;
        case WSAECONNRESET: return PIE_CONNRESET;
        case WSAETIMEDOUT: return PIE_TIMEDOUT;
        default: return wstrerror(err);
    }
}

const char *socket_ioerror(p_socket ps, int err) {
    (void) ps;
    return socket_strerror(err);
}

static const char *wstrerror(int err) {
    switch (err) {
        case WSAEINTR: return "Interrupted function call";
        case WSAEACCES: return PIE_ACCESS; // "Permission denied";
        case WSAEFAULT: return "Bad address";
        case WSAEINVAL: return "Invalid argument";
        case WSAEMFILE: return "Too many open files";
        case WSAEWOULDBLOCK: return "Resource temporarily unavailable";
        case WSAEINPROGRESS: return "Operation now in progress";
        case WSAEALREADY: return "Operation already in progress";
        case WSAENOTSOCK: return "Socket operation on nonsocket";
        case WSAEDESTADDRREQ: return "Destination address required";
        case WSAEMSGSIZE: return "Message too long";
        case WSAEPROTOTYPE: return "Protocol wrong type for socket";
        case WSAENOPROTOOPT: return "Bad protocol option";
        case WSAEPROTONOSUPPORT: return "Protocol not supported";
        case WSAESOCKTNOSUPPORT: return PIE_SOCKTYPE; // "Socket type not supported";
        case WSAEOPNOTSUPP: return "Operation not supported";
        case WSAEPFNOSUPPORT: return "Protocol family not supported";
        case WSAEAFNOSUPPORT: return PIE_FAMILY; // "Address family not supported by protocol family";
        case WSAEADDRINUSE: return PIE_ADDRINUSE; // "Address already in use";
        case WSAEADDRNOTAVAIL: return "Cannot assign requested address";
        case WSAENETDOWN: return "Network is down";
        case WSAENETUNREACH: return "Network is unreachable";
        case WSAENETRESET: return "Network dropped connection on reset";
        case WSAECONNABORTED: return "Software caused connection abort";
        case WSAECONNRESET: return PIE_CONNRESET; // "Connection reset by peer";
        case WSAENOBUFS: return "No buffer space available";
        case WSAEISCONN: return PIE_ISCONN; // "Socket is already connected";
        case WSAENOTCONN: return "Socket is not connected";
        case WSAESHUTDOWN: return "Cannot send after socket shutdown";
        case WSAETIMEDOUT: return PIE_TIMEDOUT; // "Connection timed out";
        case WSAECONNREFUSED: return PIE_CONNREFUSED; // "Connection refused";
        case WSAEHOSTDOWN: return "Host is down";
        case WSAEHOSTUNREACH: return "No route to host";
        case WSAEPROCLIM: return "Too many processes";
        case WSASYSNOTREADY: return "Network subsystem is unavailable";
        case WSAVERNOTSUPPORTED: return "Winsock.dll version out of range";
        case WSANOTINITIALISED:
            return "Successful WSAStartup not yet performed";
        case WSAEDISCON: return "Graceful shutdown in progress";
        case WSAHOST_NOT_FOUND: return PIE_HOST_NOT_FOUND; // "Host not found";
        case WSATRY_AGAIN: return "Nonauthoritative host not found";
        case WSANO_RECOVERY: return PIE_FAIL; // "Nonrecoverable name lookup error";
        case WSANO_DATA: return "Valid name, no data record of requested type";
        default: return "Unknown error";
    }
}

const char *socket_gaistrerror(int err) {
    if (err == 0) return NULL;
    switch (err) {
        case EAI_AGAIN: return PIE_AGAIN;
        case EAI_BADFLAGS: return PIE_BADFLAGS;
#ifdef EAI_BADHINTS
        case EAI_BADHINTS: return PIE_BADHINTS;
#endif
        case EAI_FAIL: return PIE_FAIL;
        case EAI_FAMILY: return PIE_FAMILY;
        case EAI_MEMORY: return PIE_MEMORY;
        case EAI_NONAME: return PIE_NONAME;
#ifdef EAI_OVERFLOW
        case EAI_OVERFLOW: return PIE_OVERFLOW;
#endif
#ifdef EAI_PROTOCOL
        case EAI_PROTOCOL: return PIE_PROTOCOL;
#endif
        case EAI_SERVICE: return PIE_SERVICE;
        case EAI_SOCKTYPE: return PIE_SOCKTYPE;
#ifdef EAI_SYSTEM
        case EAI_SYSTEM: return strerror(errno);
#endif
        default: return gai_strerror(err);
    }
}

#endif
