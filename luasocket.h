#ifndef AUXILIAR_H
#define AUXILIAR_H
/*=========================================================================*\
* Auxiliar routines for class hierarchy manipulation
* LuaSocket toolkit (but completely independent of other LuaSocket modules)
*
* A LuaSocket class is a name associated with Lua metatables. A LuaSocket
* group is a name associated with a class. A class can belong to any number
* of groups. This module provides the functionality to:
*
*   - create new classes
*   - add classes to groups
*   - set the class of objects
*   - check if an object belongs to a given class or group
*   - get the userdata associated to objects
*   - print objects in a pretty way
*
* LuaSocket class names follow the convention <module>{<class>}. Modules
* can define any number of classes and groups. The module tcp.c, for
* example, defines the classes tcp{master}, tcp{client} and tcp{server} and
* the groups tcp{client,server} and tcp{any}. Module functions can then
* perform type-checking on their arguments by either class or group.
*
* LuaSocket metatables define the __index metamethod as being a table. This
* table has one field for each method supported by the class, and a field
* "class" with the class name.
*
* The mapping from class name to the corresponding metatable and the
* reverse mapping are done using lauxlib.
\*=========================================================================*/

#include "lua.h"
//#include "lauxlib.h"
/* ======== Begin of [compat.h] ====== */
#ifndef COMPAT_H
#define COMPAT_H

#include "lua.h"
//#include "lauxlib.h"

#if LUA_VERSION_NUM==501
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup);
#endif

#endif
/* ======== End of [compat.h] ====== */

int auxiliar_open(lua_State *L);
void auxiliar_newclass(lua_State *L, const char *classname, luaL_Reg *func);
void auxiliar_add2group(lua_State *L, const char *classname, const char *group);
void auxiliar_setclass(lua_State *L, const char *classname, int objidx);
void *auxiliar_checkclass(lua_State *L, const char *classname, int objidx);
void *auxiliar_checkgroup(lua_State *L, const char *groupname, int objidx);
void *auxiliar_getclassudata(lua_State *L, const char *groupname, int objidx);
void *auxiliar_getgroupudata(lua_State *L, const char *groupname, int objidx);
int auxiliar_checkboolean(lua_State *L, int objidx);
int auxiliar_tostring(lua_State *L);
int auxiliar_typeerror(lua_State *L, int narg, const char *tname);

#endif /* AUXILIAR_H */

#ifndef BUF_H
#define BUF_H 
/*=========================================================================*\
* Input/Output interface for Lua programs
* LuaSocket toolkit
*
* Line patterns require buffering. Reading one character at a time involves
* too many system calls and is very slow. This module implements the
* LuaSocket interface for input/output on connected objects, as seen by 
* Lua programs. 
*
* Input is buffered. Output is *not* buffered because there was no simple
* way of making sure the buffered output data would ever be sent.
*
* The module is built on top of the I/O abstraction defined in io.h and the
* timeout management is done with the timeout.h interface.
\*=========================================================================*/
#include "lua.h"

/* ======== Begin of [io.h] ====== */
#ifndef IO_H
#define IO_H
/*=========================================================================*\
* Input/Output abstraction
* LuaSocket toolkit
*
* This module defines the interface that LuaSocket expects from the
* transport layer for streamed input/output. The idea is that if any
* transport implements this interface, then the buffer.c functions
* automatically work on it.
*
* The module socket.h implements this interface, and thus the module tcp.h
* is very simple.
\*=========================================================================*/
#include <stdio.h>
#include "lua.h"

/* ======== Begin of [timeout.h] ====== */
#ifndef TIMEOUT_H
#define TIMEOUT_H
/*=========================================================================*\
* Timeout management functions
* LuaSocket toolkit
\*=========================================================================*/
#include "lua.h"

/* timeout control structure */
typedef struct t_timeout_ {
    double block;          /* maximum time for blocking calls */
    double total;          /* total number of miliseconds for operation */
    double start;          /* time of start of operation */
} t_timeout;
typedef t_timeout *p_timeout;

int timeout_open(lua_State *L);
void timeout_init(p_timeout tm, double block, double total);
double timeout_get(p_timeout tm);
double timeout_getretry(p_timeout tm);
p_timeout timeout_markstart(p_timeout tm);
double timeout_getstart(p_timeout tm);
double timeout_gettime(void);
int timeout_meth_settimeout(lua_State *L, p_timeout tm);
int timeout_meth_gettimeout(lua_State *L, p_timeout tm);

#define timeout_iszero(tm)   ((tm)->block == 0.0)

#endif /* TIMEOUT_H */
/* ======== End of [timeout.h] ====== */

/* IO error codes */
enum {
    IO_DONE = 0,        /* operation completed successfully */
    IO_TIMEOUT = -1,    /* operation timed out */
    IO_CLOSED = -2,     /* the connection has been closed */
	IO_UNKNOWN = -3
};

/* interface to error message function */
typedef const char *(*p_error) (
    void *ctx,          /* context needed by send */
    int err             /* error code */
);

/* interface to send function */
typedef int (*p_send) (
    void *ctx,          /* context needed by send */
    const char *data,   /* pointer to buffer with data to send */
    size_t count,       /* number of bytes to send from buffer */
    size_t *sent,       /* number of bytes sent uppon return */
    p_timeout tm        /* timeout control */
);

/* interface to recv function */
typedef int (*p_recv) (
    void *ctx,          /* context needed by recv */
    char *data,         /* pointer to buffer where data will be writen */
    size_t count,       /* number of bytes to receive into buffer */
    size_t *got,        /* number of bytes received uppon return */
    p_timeout tm        /* timeout control */
);

/* IO driver definition */
typedef struct t_io_ {
    void *ctx;          /* context needed by send/recv */
    p_send send;        /* send function pointer */
    p_recv recv;        /* receive function pointer */
    p_error error;      /* strerror function */
} t_io;
typedef t_io *p_io;

void io_init(p_io io, p_send send, p_recv recv, p_error error, void *ctx);
const char *io_strerror(int err);

#endif /* IO_H */

/* ======== End of [io.h] ====== */

/* buffer size in bytes */
#define BUF_SIZE 8192

/* buffer control structure */
typedef struct t_buffer_ {
    double birthday;        /* throttle support info: creation time, */
    size_t sent, received;  /* bytes sent, and bytes received */
    p_io io;                /* IO driver used for this buffer */
    p_timeout tm;           /* timeout management for this buffer */
    size_t first, last;     /* index of first and last bytes of stored data */
    char data[BUF_SIZE];    /* storage space for buffer data */
} t_buffer;
typedef t_buffer *p_buffer;

int buffer_open(lua_State *L);
void buffer_init(p_buffer buf, p_io io, p_timeout tm);
int buffer_meth_send(lua_State *L, p_buffer buf);
int buffer_meth_receive(lua_State *L, p_buffer buf);
int buffer_meth_getstats(lua_State *L, p_buffer buf);
int buffer_meth_setstats(lua_State *L, p_buffer buf);
int buffer_isempty(p_buffer buf);

#endif /* BUF_H */

#ifndef COMPAT_H
#define COMPAT_H

#include "lua.h"
//#include "lauxlib.h"

#if LUA_VERSION_NUM==501
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup);
#endif

#endif

#ifndef EXCEPT_H
#define EXCEPT_H
/*=========================================================================*\
* Exception control
* LuaSocket toolkit (but completely independent from other modules)
*
* This provides support for simple exceptions in Lua. During the
* development of the HTTP/FTP/SMTP support, it became aparent that
* error checking was taking a substantial amount of the coding. These
* function greatly simplify the task of checking errors.
*
* The main idea is that functions should return nil as their first return
* values when they find an error, and return an error message (or value)
* following nil. In case of success, as long as the first value is not nil,
* the other values don't matter.
*
* The idea is to nest function calls with the "try" function. This function
* checks the first value, and, if it's falsy, wraps the second value in a
* table with metatable and calls "error" on it. Otherwise, it returns all
* values it received. Basically, it works like the Lua "assert" function,
* but it creates errors targeted specifically at "protect".
*
* The "newtry" function is a factory for "try" functions that call a
* finalizer in protected mode before calling "error".
*
* The "protect" function returns a new function that behaves exactly like
* the function it receives, but the new function catches exceptions thrown
* by "try" functions and returns nil followed by the error message instead.
*
* With these three functions, it's easy to write functions that throw
* exceptions on error, but that don't interrupt the user script.
\*=========================================================================*/

#include "lua.h"

int except_open(lua_State *L);

#endif

#ifndef INET_H
#define INET_H
/*=========================================================================*\
* Internet domain functions
* LuaSocket toolkit
*
* This module implements the creation and connection of internet domain
* sockets, on top of the socket.h interface, and the interface of with the
* resolver.
*
* The function inet_aton is provided for the platforms where it is not
* available. The module also implements the interface of the internet
* getpeername and getsockname functions as seen by Lua programs.
*
* The Lua functions toip and tohostname are also implemented here.
\*=========================================================================*/
#include "lua.h"
/* ======== Begin of [socket.h] ====== */
#ifndef SOCKET_H
#define SOCKET_H
/*=========================================================================*\
* Socket compatibilization module
* LuaSocket toolkit
*
* BSD Sockets and WinSock are similar, but there are a few irritating
* differences. Also, not all *nix platforms behave the same. This module
* (and the associated usocket.h and wsocket.h) factor these differences and
* creates a interface compatible with the io.h module.
\*=========================================================================*/

/*=========================================================================*\
* Platform specific compatibilization
\*=========================================================================*/
#ifdef _WIN32
/* ======== Begin of [wsocket.h] ====== */
#ifdef _WIN32
#ifndef WSOCKET_H
#define WSOCKET_H
/*=========================================================================*\
* Socket compatibilization module for Win32
* LuaSocket toolkit
\*=========================================================================*/

/*=========================================================================*\
* WinSock include files
\*=========================================================================*/
#include <winsock2.h>
#include <ws2tcpip.h>

typedef int socklen_t;
typedef SOCKADDR_STORAGE t_sockaddr_storage;
typedef SOCKET t_socket;
typedef t_socket *p_socket;

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif

#define SOCKET_INVALID (INVALID_SOCKET)

#ifndef SO_REUSEPORT
#define SO_REUSEPORT SO_REUSEADDR
#endif

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV (0)
#endif

#endif /* WSOCKET_H */
#endif/* ======== End of [wsocket.h] ====== */
#else
/* ======== Begin of [usocket.h] ====== */
#ifndef _WIN32
#ifndef USOCKET_H
#define USOCKET_H
/*=========================================================================*\
* Socket compatibilization module for Unix
* LuaSocket toolkit
\*=========================================================================*/

/*=========================================================================*\
* BSD include files
\*=========================================================================*/
/* error codes */
#include <errno.h>
/* close function */
#include <unistd.h>
/* fnctnl function and associated constants */
#include <fcntl.h>
/* struct sockaddr */
#include <sys/types.h>
/* socket function */
#include <sys/socket.h>
/* struct timeval */
#include <sys/time.h>
/* gethostbyname and gethostbyaddr functions */
#include <netdb.h>
/* sigpipe handling */
#include <signal.h>
/* IP stuff*/
#include <netinet/in.h>
#include <arpa/inet.h>
/* TCP options (nagle algorithm disable) */
#include <netinet/tcp.h>
#include <net/if.h>

#ifndef SO_REUSEPORT
#define SO_REUSEPORT SO_REUSEADDR
#endif

/* Some platforms use IPV6_JOIN_GROUP instead if
 * IPV6_ADD_MEMBERSHIP. The semantics are same, though. */
#ifndef IPV6_ADD_MEMBERSHIP
#ifdef IPV6_JOIN_GROUP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif /* IPV6_JOIN_GROUP */
#endif /* !IPV6_ADD_MEMBERSHIP */

/* Same with IPV6_DROP_MEMBERSHIP / IPV6_LEAVE_GROUP. */
#ifndef IPV6_DROP_MEMBERSHIP
#ifdef IPV6_LEAVE_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif /* IPV6_LEAVE_GROUP */
#endif /* !IPV6_DROP_MEMBERSHIP */

typedef int t_socket;
typedef t_socket *p_socket;
typedef struct sockaddr_storage t_sockaddr_storage;

#define SOCKET_INVALID (-1)

#endif /* USOCKET_H */
#endif/* ======== End of [usocket.h] ====== */
#endif

/*=========================================================================*\
* The connect and accept functions accept a timeout and their
* implementations are somewhat complicated. We chose to move
* the timeout control into this module for these functions in
* order to simplify the modules that use them. 
\*=========================================================================*/

/* we are lazy... */
typedef struct sockaddr SA;

/*=========================================================================*\
* Functions bellow implement a comfortable platform independent 
* interface to sockets
\*=========================================================================*/
int socket_open(void);
int socket_close(void);
void socket_destroy(p_socket ps);
void socket_shutdown(p_socket ps, int how); 
int socket_sendto(p_socket ps, const char *data, size_t count, 
        size_t *sent, SA *addr, socklen_t addr_len, p_timeout tm);
int socket_recvfrom(p_socket ps, char *data, size_t count, 
        size_t *got, SA *addr, socklen_t *addr_len, p_timeout tm);

void socket_setnonblocking(p_socket ps);
void socket_setblocking(p_socket ps);

int socket_waitfd(p_socket ps, int sw, p_timeout tm);
int socket_select(t_socket n, fd_set *rfds, fd_set *wfds, fd_set *efds, 
        p_timeout tm);

int socket_connect(p_socket ps, SA *addr, socklen_t addr_len, p_timeout tm); 
int socket_create(p_socket ps, int domain, int type, int protocol);
int socket_bind(p_socket ps, SA *addr, socklen_t addr_len); 
int socket_listen(p_socket ps, int backlog);
int socket_accept(p_socket ps, p_socket pa, SA *addr, 
        socklen_t *addr_len, p_timeout tm);

const char *socket_hoststrerror(int err);
const char *socket_gaistrerror(int err);
const char *socket_strerror(int err);

/* these are perfect to use with the io abstraction module 
   and the buffered input module */
int socket_send(p_socket ps, const char *data, size_t count, 
        size_t *sent, p_timeout tm);
int socket_recv(p_socket ps, char *data, size_t count, size_t *got, p_timeout tm);
int socket_write(p_socket ps, const char *data, size_t count, 
        size_t *sent, p_timeout tm);
int socket_read(p_socket ps, char *data, size_t count, size_t *got, p_timeout tm);
const char *socket_ioerror(p_socket ps, int err);

int socket_gethostbyaddr(const char *addr, socklen_t len, struct hostent **hp);
int socket_gethostbyname(const char *addr, struct hostent **hp);

#endif /* SOCKET_H */
/* ======== End of [socket.h] ====== */

#ifdef _WIN32
#define LUASOCKET_INET_ATON
#endif

int inet_open(lua_State *L);

const char *inet_trycreate(p_socket ps, int family, int type, int protocol);
const char *inet_tryconnect(p_socket ps, int *family, const char *address,
        const char *serv, p_timeout tm, struct addrinfo *connecthints);
const char *inet_trybind(p_socket ps, int *family, const char *address,
        const char *serv, struct addrinfo *bindhints);
const char *inet_trydisconnect(p_socket ps, int family, p_timeout tm);
const char *inet_tryaccept(p_socket server, int family, p_socket client, p_timeout tm);

int inet_meth_getpeername(lua_State *L, p_socket ps, int family);
int inet_meth_getsockname(lua_State *L, p_socket ps, int family);

int inet_optfamily(lua_State* L, int narg, const char* def);
int inet_optsocktype(lua_State* L, int narg, const char* def);

#ifdef LUASOCKET_INET_ATON
int inet_aton(const char *cp, struct in_addr *inp);
#endif

#ifdef LUASOCKET_INET_PTON
const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt);
int inet_pton(int af, const char *src, void *dst);
#endif

#endif /* INET_H */

#ifndef IO_H
#define IO_H
/*=========================================================================*\
* Input/Output abstraction
* LuaSocket toolkit
*
* This module defines the interface that LuaSocket expects from the
* transport layer for streamed input/output. The idea is that if any
* transport implements this interface, then the buffer.c functions
* automatically work on it.
*
* The module socket.h implements this interface, and thus the module tcp.h
* is very simple.
\*=========================================================================*/
#include <stdio.h>
#include "lua.h"


/* IO error codes */
enum {
    IO_DONE = 0,        /* operation completed successfully */
    IO_TIMEOUT = -1,    /* operation timed out */
    IO_CLOSED = -2,     /* the connection has been closed */
	IO_UNKNOWN = -3
};

/* interface to error message function */
typedef const char *(*p_error) (
    void *ctx,          /* context needed by send */
    int err             /* error code */
);

/* interface to send function */
typedef int (*p_send) (
    void *ctx,          /* context needed by send */
    const char *data,   /* pointer to buffer with data to send */
    size_t count,       /* number of bytes to send from buffer */
    size_t *sent,       /* number of bytes sent uppon return */
    p_timeout tm        /* timeout control */
);

/* interface to recv function */
typedef int (*p_recv) (
    void *ctx,          /* context needed by recv */
    char *data,         /* pointer to buffer where data will be writen */
    size_t count,       /* number of bytes to receive into buffer */
    size_t *got,        /* number of bytes received uppon return */
    p_timeout tm        /* timeout control */
);

/* IO driver definition */
typedef struct t_io_ {
    void *ctx;          /* context needed by send/recv */
    p_send send;        /* send function pointer */
    p_recv recv;        /* receive function pointer */
    p_error error;      /* strerror function */
} t_io;
typedef t_io *p_io;

void io_init(p_io io, p_send send, p_recv recv, p_error error, void *ctx);
const char *io_strerror(int err);

#endif /* IO_H */


#ifndef LUASOCKET_H
#define LUASOCKET_H
/*=========================================================================*\
* LuaSocket toolkit
* Networking support for the Lua language
* Diego Nehab
* 9/11/1999
\*=========================================================================*/
#include "lua.h"

/*-------------------------------------------------------------------------*\
* Current socket library version
\*-------------------------------------------------------------------------*/
#define LUASOCKET_VERSION    "LuaSocket 3.0-rc1"
#define LUASOCKET_COPYRIGHT  "Copyright (C) 1999-2013 Diego Nehab"

/*-------------------------------------------------------------------------*\
* This macro prefixes all exported API functions
\*-------------------------------------------------------------------------*/
#ifndef LUASOCKET_API
#define LUASOCKET_API extern
#endif

/*-------------------------------------------------------------------------*\
* Initializes the library.
\*-------------------------------------------------------------------------*/
LUASOCKET_API int luaopen_socket_core(lua_State *L);

#endif /* LUASOCKET_H */

#ifndef MIME_H 
#define MIME_H 
/*=========================================================================*\
* Core MIME support
* LuaSocket toolkit
*
* This module provides functions to implement transfer content encodings
* and formatting conforming to RFC 2045. It is used by mime.lua, which
* provide a higher level interface to this functionality. 
\*=========================================================================*/
#include "lua.h"

/*-------------------------------------------------------------------------*\
* Current MIME library version
\*-------------------------------------------------------------------------*/
#define MIME_VERSION    "MIME 1.0.3"
#define MIME_COPYRIGHT  "Copyright (C) 2004-2013 Diego Nehab"
#define MIME_AUTHORS    "Diego Nehab"

/*-------------------------------------------------------------------------*\
* This macro prefixes all exported API functions
\*-------------------------------------------------------------------------*/
#ifndef MIME_API
#define MIME_API extern
#endif

MIME_API int luaopen_mime_core(lua_State *L);

#endif /* MIME_H */

#ifndef OPTIONS_H
#define OPTIONS_H
/*=========================================================================*\
* Common option interface 
* LuaSocket toolkit
*
* This module provides a common interface to socket options, used mainly by
* modules UDP and TCP. 
\*=========================================================================*/

#include "lua.h"

/* option registry */
typedef struct t_opt {
  const char *name;
  int (*func)(lua_State *L, p_socket ps);
} t_opt;
typedef t_opt *p_opt;

/* supported options for setoption */
int opt_set_dontroute(lua_State *L, p_socket ps);
int opt_set_broadcast(lua_State *L, p_socket ps);
int opt_set_tcp_nodelay(lua_State *L, p_socket ps);
int opt_set_keepalive(lua_State *L, p_socket ps);
int opt_set_linger(lua_State *L, p_socket ps);
int opt_set_reuseaddr(lua_State *L, p_socket ps);
int opt_set_reuseport(lua_State *L, p_socket ps);
int opt_set_ip_multicast_if(lua_State *L, p_socket ps);
int opt_set_ip_multicast_ttl(lua_State *L, p_socket ps);
int opt_set_ip_multicast_loop(lua_State *L, p_socket ps);
int opt_set_ip_add_membership(lua_State *L, p_socket ps);
int opt_set_ip_drop_membersip(lua_State *L, p_socket ps);
int opt_set_ip6_unicast_hops(lua_State *L, p_socket ps);
int opt_set_ip6_multicast_hops(lua_State *L, p_socket ps);
int opt_set_ip6_multicast_loop(lua_State *L, p_socket ps);
int opt_set_ip6_add_membership(lua_State *L, p_socket ps);
int opt_set_ip6_drop_membersip(lua_State *L, p_socket ps);
int opt_set_ip6_v6only(lua_State *L, p_socket ps);

/* supported options for getoption */
int opt_get_dontroute(lua_State *L, p_socket ps);
int opt_get_broadcast(lua_State *L, p_socket ps);
int opt_get_reuseaddr(lua_State *L, p_socket ps);
int opt_get_reuseport(lua_State *L, p_socket ps);
int opt_get_tcp_nodelay(lua_State *L, p_socket ps);
int opt_get_keepalive(lua_State *L, p_socket ps);
int opt_get_linger(lua_State *L, p_socket ps);
int opt_get_ip_multicast_loop(lua_State *L, p_socket ps);
int opt_get_ip_multicast_if(lua_State *L, p_socket ps);
int opt_get_error(lua_State *L, p_socket ps);
int opt_get_ip6_multicast_loop(lua_State *L, p_socket ps);
int opt_get_ip6_multicast_hops(lua_State *L, p_socket ps);
int opt_get_ip6_unicast_hops(lua_State *L, p_socket ps);
int opt_get_ip6_v6only(lua_State *L, p_socket ps);
int opt_get_reuseport(lua_State *L, p_socket ps);

/* invokes the appropriate option handler */
int opt_meth_setoption(lua_State *L, p_opt opt, p_socket ps);
int opt_meth_getoption(lua_State *L, p_opt opt, p_socket ps);

#endif

#ifndef PIERROR_H
#define PIERROR_H
/*=========================================================================*\
* Error messages
* Defines platform independent error messages
\*=========================================================================*/

#define PIE_HOST_NOT_FOUND "host not found"
#define PIE_ADDRINUSE      "address already in use"
#define PIE_ISCONN         "already connected"
#define PIE_ACCESS         "permission denied"
#define PIE_CONNREFUSED    "connection refused"
#define PIE_CONNABORTED    "closed"
#define PIE_CONNRESET      "closed"
#define PIE_TIMEDOUT       "timeout"
#define PIE_AGAIN          "temporary failure in name resolution"
#define PIE_BADFLAGS       "invalid value for ai_flags"
#define PIE_BADHINTS       "invalid value for hints"
#define PIE_FAIL           "non-recoverable failure in name resolution"
#define PIE_FAMILY         "ai_family not supported"
#define PIE_MEMORY         "memory allocation failure"
#define PIE_NONAME         "host or service not provided, or not known"
#define PIE_OVERFLOW       "argument buffer overflow"
#define PIE_PROTOCOL       "resolved protocol is unknown"
#define PIE_SERVICE        "service not supported for socket type"
#define PIE_SOCKTYPE       "ai_socktype not supported"

#endif

#ifndef SELECT_H
#define SELECT_H
/*=========================================================================*\
* Select implementation
* LuaSocket toolkit
*
* Each object that can be passed to the select function has to export 
* method getfd() which returns the descriptor to be passed to the
* underlying select function. Another method, dirty(), should return 
* true if there is data ready for reading (required for buffered input).
\*=========================================================================*/

int select_open(lua_State *L);

#endif /* SELECT_H */

#ifndef SOCKET_H
#define SOCKET_H
/*=========================================================================*\
* Socket compatibilization module
* LuaSocket toolkit
*
* BSD Sockets and WinSock are similar, but there are a few irritating
* differences. Also, not all *nix platforms behave the same. This module
* (and the associated usocket.h and wsocket.h) factor these differences and
* creates a interface compatible with the io.h module.
\*=========================================================================*/

/*=========================================================================*\
* Platform specific compatibilization
\*=========================================================================*/
#ifdef _WIN32
#else
#endif

/*=========================================================================*\
* The connect and accept functions accept a timeout and their
* implementations are somewhat complicated. We chose to move
* the timeout control into this module for these functions in
* order to simplify the modules that use them. 
\*=========================================================================*/

/* we are lazy... */
typedef struct sockaddr SA;

/*=========================================================================*\
* Functions bellow implement a comfortable platform independent 
* interface to sockets
\*=========================================================================*/
int socket_open(void);
int socket_close(void);
void socket_destroy(p_socket ps);
void socket_shutdown(p_socket ps, int how); 
int socket_sendto(p_socket ps, const char *data, size_t count, 
        size_t *sent, SA *addr, socklen_t addr_len, p_timeout tm);
int socket_recvfrom(p_socket ps, char *data, size_t count, 
        size_t *got, SA *addr, socklen_t *addr_len, p_timeout tm);

void socket_setnonblocking(p_socket ps);
void socket_setblocking(p_socket ps);

int socket_waitfd(p_socket ps, int sw, p_timeout tm);
int socket_select(t_socket n, fd_set *rfds, fd_set *wfds, fd_set *efds, 
        p_timeout tm);

int socket_connect(p_socket ps, SA *addr, socklen_t addr_len, p_timeout tm); 
int socket_create(p_socket ps, int domain, int type, int protocol);
int socket_bind(p_socket ps, SA *addr, socklen_t addr_len); 
int socket_listen(p_socket ps, int backlog);
int socket_accept(p_socket ps, p_socket pa, SA *addr, 
        socklen_t *addr_len, p_timeout tm);

const char *socket_hoststrerror(int err);
const char *socket_gaistrerror(int err);
const char *socket_strerror(int err);

/* these are perfect to use with the io abstraction module 
   and the buffered input module */
int socket_send(p_socket ps, const char *data, size_t count, 
        size_t *sent, p_timeout tm);
int socket_recv(p_socket ps, char *data, size_t count, size_t *got, p_timeout tm);
int socket_write(p_socket ps, const char *data, size_t count, 
        size_t *sent, p_timeout tm);
int socket_read(p_socket ps, char *data, size_t count, size_t *got, p_timeout tm);
const char *socket_ioerror(p_socket ps, int err);

int socket_gethostbyaddr(const char *addr, socklen_t len, struct hostent **hp);
int socket_gethostbyname(const char *addr, struct hostent **hp);

#endif /* SOCKET_H */

#ifndef TCP_H
#define TCP_H
/*=========================================================================*\
* TCP object
* LuaSocket toolkit
*
* The tcp.h module is basicly a glue that puts together modules buffer.h,
* timeout.h socket.h and inet.h to provide the LuaSocket TCP (AF_INET,
* SOCK_STREAM) support.
*
* Three classes are defined: master, client and server. The master class is
* a newly created tcp object, that has not been bound or connected. Server
* objects are tcp objects bound to some local address. Client objects are
* tcp objects either connected to some address or returned by the accept
* method of a server object.
\*=========================================================================*/
#include "lua.h"

/* ======== Begin of [buffer.h] ====== */
#ifndef BUF_H
#define BUF_H 
/*=========================================================================*\
* Input/Output interface for Lua programs
* LuaSocket toolkit
*
* Line patterns require buffering. Reading one character at a time involves
* too many system calls and is very slow. This module implements the
* LuaSocket interface for input/output on connected objects, as seen by 
* Lua programs. 
*
* Input is buffered. Output is *not* buffered because there was no simple
* way of making sure the buffered output data would ever be sent.
*
* The module is built on top of the I/O abstraction defined in io.h and the
* timeout management is done with the timeout.h interface.
\*=========================================================================*/
#include "lua.h"


/* buffer size in bytes */
#define BUF_SIZE 8192

/* buffer control structure */
typedef struct t_buffer_ {
    double birthday;        /* throttle support info: creation time, */
    size_t sent, received;  /* bytes sent, and bytes received */
    p_io io;                /* IO driver used for this buffer */
    p_timeout tm;           /* timeout management for this buffer */
    size_t first, last;     /* index of first and last bytes of stored data */
    char data[BUF_SIZE];    /* storage space for buffer data */
} t_buffer;
typedef t_buffer *p_buffer;

int buffer_open(lua_State *L);
void buffer_init(p_buffer buf, p_io io, p_timeout tm);
int buffer_meth_send(lua_State *L, p_buffer buf);
int buffer_meth_receive(lua_State *L, p_buffer buf);
int buffer_meth_getstats(lua_State *L, p_buffer buf);
int buffer_meth_setstats(lua_State *L, p_buffer buf);
int buffer_isempty(p_buffer buf);

#endif /* BUF_H */
/* ======== End of [buffer.h] ====== */

typedef struct t_tcp_ {
    t_socket sock;
    t_io io;
    t_buffer buf;
    t_timeout tm;
    int family;
} t_tcp;

typedef t_tcp *p_tcp;

int tcp_open(lua_State *L);

#endif /* TCP_H */

#ifndef TIMEOUT_H
#define TIMEOUT_H
/*=========================================================================*\
* Timeout management functions
* LuaSocket toolkit
\*=========================================================================*/
#include "lua.h"

/* timeout control structure */
typedef struct t_timeout_ {
    double block;          /* maximum time for blocking calls */
    double total;          /* total number of miliseconds for operation */
    double start;          /* time of start of operation */
} t_timeout;
typedef t_timeout *p_timeout;

int timeout_open(lua_State *L);
void timeout_init(p_timeout tm, double block, double total);
double timeout_get(p_timeout tm);
double timeout_getretry(p_timeout tm);
p_timeout timeout_markstart(p_timeout tm);
double timeout_getstart(p_timeout tm);
double timeout_gettime(void);
int timeout_meth_settimeout(lua_State *L, p_timeout tm);
int timeout_meth_gettimeout(lua_State *L, p_timeout tm);

#define timeout_iszero(tm)   ((tm)->block == 0.0)

#endif /* TIMEOUT_H */

#ifndef UDP_H
#define UDP_H
/*=========================================================================*\
* UDP object
* LuaSocket toolkit
*
* The udp.h module provides LuaSocket with support for UDP protocol
* (AF_INET, SOCK_DGRAM).
*
* Two classes are defined: connected and unconnected. UDP objects are
* originally unconnected. They can be "connected" to a given address
* with a call to the setpeername function. The same function can be used to
* break the connection.
\*=========================================================================*/
#include "lua.h"


#define UDP_DATAGRAMSIZE 8192

typedef struct t_udp_ {
    t_socket sock;
    t_timeout tm;
    int family;
} t_udp;
typedef t_udp *p_udp;

int udp_open(lua_State *L);

#endif /* UDP_H */

#ifndef UNIX_H
#define UNIX_H
/*=========================================================================*\
* Unix domain object
* LuaSocket toolkit
*
* This module is just an example of how to extend LuaSocket with a new 
* domain.
\*=========================================================================*/
#include "lua.h"


#ifndef UNIX_API
#define UNIX_API extern
#endif

typedef struct t_unix_ {
    t_socket sock;
    t_io io;
    t_buffer buf;
    t_timeout tm;
} t_unix;
typedef t_unix *p_unix;

UNIX_API int luaopen_socket_unix(lua_State *L);

#endif /* UNIX_H */

#ifndef _WIN32
#ifndef UNIXTCP_H
#define UNIXTCP_H
/*=========================================================================*\
* UNIX TCP object
* LuaSocket toolkit
*
* The unixtcp.h module is basicly a glue that puts together modules buffer.h,
* timeout.h socket.h and inet.h to provide the LuaSocket UNIX TCP (AF_UNIX,
* SOCK_STREAM) support.
*
* Three classes are defined: master, client and server. The master class is
* a newly created unixtcp object, that has not been bound or connected. Server
* objects are unixtcp objects bound to some local address. Client objects are
* unixtcp objects either connected to some address or returned by the accept
* method of a server object.
\*=========================================================================*/
/* ======== Begin of [unix.h] ====== */
#ifndef UNIX_H
#define UNIX_H
/*=========================================================================*\
* Unix domain object
* LuaSocket toolkit
*
* This module is just an example of how to extend LuaSocket with a new 
* domain.
\*=========================================================================*/
#include "lua.h"


#ifndef UNIX_API
#define UNIX_API extern
#endif

typedef struct t_unix_ {
    t_socket sock;
    t_io io;
    t_buffer buf;
    t_timeout tm;
} t_unix;
typedef t_unix *p_unix;

UNIX_API int luaopen_socket_unix(lua_State *L);

#endif /* UNIX_H */
/* ======== End of [unix.h] ====== */

int unixtcp_open(lua_State *L);

#endif /* UNIXTCP_H */
#endif
#ifndef _WIN32
#ifndef UNIXUDP_H
#define UNIXUDP_H
/*=========================================================================*\
* UDP object
* LuaSocket toolkit
*
* The udp.h module provides LuaSocket with support for UDP protocol
* (AF_INET, SOCK_DGRAM).
*
* Two classes are defined: connected and unconnected. UDP objects are
* originally unconnected. They can be "connected" to a given address
* with a call to the setpeername function. The same function can be used to
* break the connection.
\*=========================================================================*/


int unixudp_open(lua_State *L);

#endif /* UNIXUDP_H */
#endif
#ifndef _WIN32
#ifndef USOCKET_H
#define USOCKET_H
/*=========================================================================*\
* Socket compatibilization module for Unix
* LuaSocket toolkit
\*=========================================================================*/

/*=========================================================================*\
* BSD include files
\*=========================================================================*/
/* error codes */
#include <errno.h>
/* close function */
#include <unistd.h>
/* fnctnl function and associated constants */
#include <fcntl.h>
/* struct sockaddr */
#include <sys/types.h>
/* socket function */
#include <sys/socket.h>
/* struct timeval */
#include <sys/time.h>
/* gethostbyname and gethostbyaddr functions */
#include <netdb.h>
/* sigpipe handling */
#include <signal.h>
/* IP stuff*/
#include <netinet/in.h>
#include <arpa/inet.h>
/* TCP options (nagle algorithm disable) */
#include <netinet/tcp.h>
#include <net/if.h>

#ifndef SO_REUSEPORT
#define SO_REUSEPORT SO_REUSEADDR
#endif

/* Some platforms use IPV6_JOIN_GROUP instead if
 * IPV6_ADD_MEMBERSHIP. The semantics are same, though. */
#ifndef IPV6_ADD_MEMBERSHIP
#ifdef IPV6_JOIN_GROUP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif /* IPV6_JOIN_GROUP */
#endif /* !IPV6_ADD_MEMBERSHIP */

/* Same with IPV6_DROP_MEMBERSHIP / IPV6_LEAVE_GROUP. */
#ifndef IPV6_DROP_MEMBERSHIP
#ifdef IPV6_LEAVE_GROUP
#define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif /* IPV6_LEAVE_GROUP */
#endif /* !IPV6_DROP_MEMBERSHIP */

typedef int t_socket;
typedef t_socket *p_socket;
typedef struct sockaddr_storage t_sockaddr_storage;

#define SOCKET_INVALID (-1)

#endif /* USOCKET_H */
#endif
#ifdef _WIN32
#ifndef WSOCKET_H
#define WSOCKET_H
/*=========================================================================*\
* Socket compatibilization module for Win32
* LuaSocket toolkit
\*=========================================================================*/

/*=========================================================================*\
* WinSock include files
\*=========================================================================*/
#include <winsock2.h>
#include <ws2tcpip.h>

typedef int socklen_t;
typedef SOCKADDR_STORAGE t_sockaddr_storage;
typedef SOCKET t_socket;
typedef t_socket *p_socket;

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif

#define SOCKET_INVALID (INVALID_SOCKET)

#ifndef SO_REUSEPORT
#define SO_REUSEPORT SO_REUSEADDR
#endif

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV (0)
#endif

#endif /* WSOCKET_H */
#endif
