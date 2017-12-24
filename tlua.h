// Tiny Lua binding.
// by soniced@sina.com
//
#pragma once
#include <type_traits>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

#ifndef NO_MINI_LUA
#include "lua.h"
#endif
#ifndef TLUA_NO_SOCKET
#include "luasocket.h"
#endif

namespace tlua
{
    namespace imp
    {
        using namespace std;

        // c++14 index_sequence in c++11
        template<size_t...>					struct index_sequence {};
        template<size_t N, size_t... Is>	struct make_index_sequence : make_index_sequence<N - 1, N - 1, Is...> {};
        template<size_t... Is>				struct make_index_sequence<0, Is...> : index_sequence<Is...> {};


        struct LuaObj
        {
            static lua_State*& L()
            {
                static lua_State* s;
                return s;
            }

            struct PopOnExit
            {
                ~PopOnExit()
                {
                    lua_pop(L(), 1);
                }
            };
        };

        template <typename T>
        struct Stack : LuaObj
        {
            static_assert(is_enum_v<T>);

            static T get(int index)
            {
                return (T)lua_tointeger(L(), index);
            }
            static void push(T r)
            {
                lua_pushinteger(L(), (int)r);
            }
        };


        struct Nil
        {};


        // void trick.
        template <typename T, typename Default>
        T operator,(T&& v, Default)
        {
            return forward<T>(v);
        }


        class LuaCaller : public LuaObj
        {
        public:
            template<typename R, typename... A, typename F>
            static int callCpp(int argIndex, F f)
            {
                Stack<R>::push((callCpp<R, A...>(argIndex, f, make_index_sequence<sizeof...(A)>()), Nil()));
                return std::is_same<R, void>::value ? 0 : 1;
            }
            template<typename R, typename... A>
            static R callLuaFromStack(A&&... a)
            {
                PopOnExit t;
                lua_getglobal(L(), "__traceback");
                lua_insert(L(), -2);
                std::initializer_list<char> ordered = { (Stack<A>::push(forward<A>(a)),0)... };
                int nargs = sizeof...(A);
                lua_pcall(L(), nargs, 1, -nargs - 2);
                return Stack<R>::get(-1);
            }
        private:
            template<typename R, typename... A, typename F, size_t... index>
            static R callCpp(int offset, F f, index_sequence<index...>)
            {
                return f(Stack<A>::get(offset + index)...);
            }
        };

        class LuaRef;
        class Iterator;

        class LuaRefBase : public LuaObj
        {
        public:
            void iniFromStack()
            {
                m_ref = luaL_ref(L(), LUA_REGISTRYINDEX);
            }
            virtual ~LuaRefBase()
            {
                if (m_ref != LUA_REFNIL && L())
                    luaL_unref(L(), LUA_REGISTRYINDEX, m_ref);
            }
            virtual void push() const
            {
                lua_rawgeti(L(), LUA_REGISTRYINDEX, m_ref);
            }
            int type() const
            {
                if (m_ref == LUA_REFNIL) return LUA_TNIL;
                PopOnExit p;
                push();
                return lua_type(L(), -1);
            }
            int createRef() const
            {
                if (m_ref == LUA_REFNIL) return LUA_REFNIL;
                push();
                return luaL_ref(L(), LUA_REGISTRYINDEX);
            }
            void pop()
            {
                luaL_unref(L(), LUA_REGISTRYINDEX, m_ref);
                m_ref = luaL_ref(L(), LUA_REGISTRYINDEX);
            }
            bool isNil() const
            {
                return type() == LUA_TNIL;
            }
            explicit operator bool()const
            {
                return !isNil();
            }
            int length() const
            {
                PopOnExit p{};
                push();
                return (int)lua_objlen(L(), -1);
            }
            template <typename T>
            explicit operator T() const
            {
                PopOnExit p;
                push();
                return Stack<T>::get(lua_gettop(L()));
            }

            template <typename R = void, typename... A>
            R call(A&&... a) const
            {
                push();
                return LuaCaller::callLuaFromStack<R>(forward<A>(a)...);
            }
            template <class T>
            void append(T&& v) const
            {
                push();
                Stack <T>::push(forward<T>(v));
                luaL_ref(L(), -2);
                lua_pop(L(), 1);
            }
        protected:
            int m_ref = LUA_REFNIL;
        };

        class TableProxy : public LuaRefBase
        {
            int m_tableRef;
        public:
            TableProxy(int tableRef) : m_tableRef(tableRef)
            {
                iniFromStack();
            }
            TableProxy(TableProxy const& other) = delete;
            void operator=(TableProxy const& other) = delete;
            TableProxy(TableProxy&& other)
            {
                m_tableRef = other.m_tableRef;
                m_ref = other.m_ref;
                other.m_ref = LUA_REFNIL;
            }
            void push() const override
            {
                lua_rawgeti(L(), LUA_REGISTRYINDEX, m_tableRef);
                lua_rawgeti(L(), LUA_REGISTRYINDEX, m_ref);
                lua_gettable(L(), -2);
                lua_remove(L(), -2); // remove the table
            }
            template <class T>
            TableProxy& operator= (T&& v)
            {
                PopOnExit p;
                lua_rawgeti(L(), LUA_REGISTRYINDEX, m_tableRef);
                lua_rawgeti(L(), LUA_REGISTRYINDEX, m_ref);
                Stack <T>::push(forward<T>(v));
                lua_rawset(L(), -3);
                return *this;
            }
            template <class T>
            TableProxy operator[] (T&& key) const
            {
                return LuaRef(*this)[forward<T>(key)];
            }
        };

        class LuaRef : public LuaRefBase
        {
        public:
            LuaRef()
            {}
            LuaRef(TableProxy const& other)
            {
                m_ref = other.createRef();
            }
            LuaRef(LuaRef&& other)
            {
                m_ref = other.m_ref;
                other.m_ref = LUA_REFNIL;
            }
            LuaRef& operator=(LuaRef&& other)
            {
                luaL_unref(L(), LUA_REGISTRYINDEX, m_ref);
                m_ref = other.m_ref;
                other.m_ref = LUA_REFNIL;
                return *this;
            }
            LuaRef(LuaRef const& other)
            {
                m_ref = other.createRef();
            }
            static LuaRef fromIndex(int index)
            {
                lua_pushvalue(L(), index);
                return fromStack();
            }
            static LuaRef fromStack()
            {
                LuaRef r;
                r.iniFromStack();
                return r;
            }
            template <typename T>
            TableProxy operator[] (T&& key) const
            {
                Stack<T>::push(forward<T>(key));
                return TableProxy(m_ref);
            }
            template <typename T>
            LuaRef& operator= (T&& rhs)
            {
                luaL_unref(L(), LUA_REGISTRYINDEX, m_ref);
                Stack <T>::push(forward<T>(rhs));
                m_ref = luaL_ref(L(), LUA_REGISTRYINDEX);
                return *this;
            }
            Iterator begin() const;
            Iterator end() const;
        };

        class Iterator : public LuaObj
        {
        public:
            Iterator()
            {}
            explicit Iterator(const LuaRef& table) : m_table(table)
            {
                next();
            }
            Iterator& operator++ ()
            {
                if (valid) next();
                return *this;
            }
            bool operator==(const Iterator& r)const
            {
                if (!valid && !r.valid) return true;
                return false;
            }
            bool operator!=(const Iterator& r)const
            {
                return !(*this == r);
            }
            std::pair<LuaRef, LuaRef> operator*()
            {
                return std::pair<LuaRef, LuaRef>(m_key, m_value);
            }
            void next()
            {
                m_table.push();
                m_key.push();
                valid = false;
                if (lua_next(L(), -2)) {
                    valid = true;
                    m_value.pop();
                    m_key.pop();
                }
                lua_pop(L(), 1);
            }
        private:
            LuaRef m_table, m_key, m_value;
            bool valid = false;
        };


        inline Iterator LuaRef::begin()const
        {
            return Iterator(*this);
        }
        inline Iterator LuaRef::end()const
        {
            return Iterator();
        }

        class LuaMgr : public LuaObj
        {
        public:
            typedef void(*Register)();

            LuaMgr()
            {
                get() = this;
                L() = luaL_newstate();
                luaL_openlibs(L());
#ifndef TLUA_NO_SOCKET
                luaopen_socket_core(L());
#endif
                LuaRef loaders = getGlobal("package")["loaders"];
                if (loaders.type() != LUA_TTABLE) {
                    loaders = (LuaRef)getGlobal("package")["searchers"];
                }
                loaders.append(&luaLoader);

                setGlobal("__traceback", &traceback);
                lua_gc(L(), LUA_GCSETSTEPMUL, 1);
                for (auto i : getRegisters()) i();
            }
            void setSourceRoot(string luaRoot = "") { srcDir = luaRoot; }
            virtual ~LuaMgr()
            {
                lua_close(L());
                L() = 0;
            }
            static std::vector<Register>& getRegisters()
            {
                static std::vector<Register> registers;
                return registers;
            }
            template<typename T>
            void setGlobal(const char* name, T&& t)
            {
                Stack<T>::push(forward<T>(t));
                lua_setglobal(L(), name);
            }
            LuaRef doFile(const char *name)
            {
                auto cmd = string("return require('") + name + "')";
                luaL_loadstring(L(), cmd.c_str());
                return LuaCaller::callLuaFromStack<LuaRef>();
            }
            LuaRef doString(const char* name)
            {
                luaL_loadstring(L(), name);
                return LuaCaller::callLuaFromStack<LuaRef>();
            }
            LuaRef newTable()
            {
                lua_newtable(L());
                return LuaRef::fromStack();
            }
            LuaRef getGlobal(const char* name)
            {
                lua_getglobal(L(), name);
                return LuaRef::fromStack();
            }
            static LuaMgr*& get()
            {
                static LuaMgr* s;
                return s;
            }
        protected:
            virtual void onScriptError(const char* msg, const char* stack)
            {
                get()->logError(stack);
            }
            virtual string loadFile(const char* name)
            {
                string ret;
                if (FILE* f = fopen(name, "rb")) {
                    fseek(f, 0, SEEK_END);
                    ret.resize(ftell(f));
                    fseek(f, 0, SEEK_SET);
                    fread((void*)ret.data(), ret.size(), 1, f);
                    fclose(f);
                }
                return ret;
            }
            virtual void logError(const string& err)
            {
                fprintf(stderr, "%s\n", err.c_str());
            }

        private:
            static void traceback(const char* msg)
            {
                auto stack = get()->getGlobal("debug")["traceback"].call<const char*>(msg, 0);
                get()->onScriptError(msg, stack);
            }
            static int luaLoader(lua_State* L)
            {
                string requireFile = lua_tostring(L, -1);
                while (auto c = strchr(&requireFile[0], '.')) *c = '/';
                requireFile += ".lua";
                auto filePath = get()->srcDir + "/" + requireFile;
                auto chunk = get()->loadFile(filePath.c_str());
                if (chunk.size() == 0) {
                    get()->logError(string("can not get file data of ") + filePath);
                    return 0;
                }
                auto err = luaL_loadbuffer(L, chunk.data(), chunk.size(), requireFile.c_str());
                if (err == LUA_ERRSYNTAX) {
                    get()->logError(string("syntax error in ") + filePath);
                    return 0;
                }
                return 1;
            }

        private:
            string srcDir;
        };

    }

    using imp::LuaMgr;
    using imp::LuaRef;



#define ExportLuaType(name, funcs) \
    static bool __reg_##name = (tlua::LuaMgr::getRegisters().push_back([]{ \
        typedef name Class; \
        auto* mgr = tlua::LuaMgr::get(); \
        auto& table = mgr->newTable(); \
        funcs \
        mgr->setGlobal(#name, table); \
    }), true);

#define _LuaTypeBase(base) table["base"] = #base; 

#define ExportLuaTypeInherit(name, base, funcs) ExportLuaType(name, funcs _LuaTypeBase(base) )
#define ExportLuaFunc(name) table[#name] = &Class::name;
#define ExportLuaFuncOverload(name, type) table[#name] = type &Class::name;
#define ExportLuaField(name) table[#name] = Class::name;

    //////////////////////////////////////////////////////////////////////////


    namespace imp
    {

        template <class T>
        struct Stack <T&> : Stack<T>
        {};

        template <class T>
        struct Stack <const T> : Stack<T>
        {};


        template <>
        struct Stack<void>
        {
            static void get(int index)
            {}
            static void push(Nil r)
            {}
        };

        template <>
        struct Stack<Nil> : LuaObj
        {
            static void push(Nil r)
            {
                lua_pushnil(L());
            }
        };

        template <>
        struct Stack <lua_CFunction> : LuaObj
        {
            static void push(lua_CFunction f) 
            {
                lua_pushcfunction(L(), f); 
            }
            static lua_CFunction get(int index) 
            {
                return lua_tocfunction(L(), index); 
            }
        };

        template <>
        struct Stack<lua_Number> : LuaObj
        {
            static lua_Number get(int index)
            {
                return lua_tonumber(L(), index);
            }
            static void push(lua_Number r)
            {
                lua_pushnumber(L(), r);
            }
        };

        template <>
        struct Stack<lua_Integer> : LuaObj
        {
            static lua_Integer get(int index)
            {
                return lua_tointeger(L(), index);
            }
            static void push(lua_Integer r)
            {
                lua_pushinteger(L(), r);
            }
        };

#ifndef TLUA_INT_AS_INTEGER

        template <>
        struct Stack<int> : Stack<lua_Integer> {};

        template <>
        struct Stack<unsigned int> : Stack<lua_Integer> {};
#endif

        template <>
        struct Stack<const char*> : LuaObj
        {
            static const char* get(int index)
            {
                return lua_tostring(L(), index);
            }
            static void push(const char *r)
            {
                lua_pushstring(L(), r);
            }
        };

        template <int N>
        struct Stack < const char(&)[N] > : LuaObj
        {
            static void push(const char *r)
            {
                lua_pushlstring(L(), r, N - 1);
            }
        };

        template <>
        struct Stack<bool> : LuaObj
        {
            static bool	get(int index)
            {
                return lua_toboolean(L(), index) != 0;
            }
            static void	push(bool r)
            {
                lua_pushboolean(L(), r ? 1 : 0);
            }
        };

        template <typename T>
        struct Stack<T*> : LuaObj
        {
            static T* get(int index)
            {
                return static_cast<T*>(lua_touserdata(L(), index));
            }
            static void push(T* r)
            {
                lua_pushlightuserdata(L(), r);
            }
        };       
        

        template <>
        struct Stack<LuaRef> : LuaObj
        {
            static void push(LuaRef const& v)
            {
                v.push();
            }
            static LuaRef get(int index)
            {
                return LuaRef::fromIndex(index);
            }
        };

        template<typename R, typename... A>
        struct Stack<R(*)(A...)> : LuaObj
        {
            static void push(R(*f)(A...))
            {
                using F = decltype(f);
                *(F*)lua_newuserdata(L(), sizeof(F)) = f;
                lua_pushcclosure(L(), [](lua_State* L) {
                    auto f = *(F*)lua_touserdata(L, lua_upvalueindex(1));
                    return LuaCaller::callCpp<R, A...>(1, f);
                }, 1);
            }
        };

        template<typename R, typename... A>
        struct Stack<function<R(A...)>> : LuaObj
        {
            static void push(const function<R(A...)>& f)
            {
                using F = function<R(A...)>;
                new (lua_newuserdata(L(), sizeof(F))) F(f);
                lua_pushcclosure(L(), [](lua_State* L) {
                    auto& f = *(F*)lua_touserdata(L, lua_upvalueindex(1));
                    return LuaCaller::callCpp<R, A...>(1, f);
                }, 1);
            }
            static function<R(A...)> get(int idx) 
            {
                auto f = LuaRef::fromIndex(idx);
                return [=](A... a) -> R {                    
                    return (R) f.call(forward<A>(a)...);
                };
            }
        };

        template< typename R, typename C, typename... A>
        struct Stack<R(C::*)(A...)> : LuaObj
        {
            static void push(R(C::*f)(A...))
            {
                using MF = decltype(f);
                *(MF*)lua_newuserdata(L(), sizeof(MF)) = f;
                lua_pushcclosure(L(), [](lua_State* L) {
                    return LuaCaller::callCpp<R, A...>(2, [L](A&&... a) {
                        auto f = *(MF*)lua_touserdata(L, lua_upvalueindex(1));
                        auto obj = (C*)lua_touserdata(L, 1);
                        return (obj->*f)(forward<A>(a)...);
                    });
                }, 1);
            }
        };

        template< typename R, typename C, typename... A>
        struct Stack<R(C::*)(A...)const> : LuaObj
        {
            static void push(R(C::*f)(A...)const)
            {
                using MF = decltype(f);
                *(MF*)lua_newuserdata(L(), sizeof(MF)) = f;
                lua_pushcclosure(L(), [](lua_State* L) {
                    return LuaCaller::callCpp<R, A...>(2, [L](A&&... a) {
                        auto f = *(MF*)lua_touserdata(L, lua_upvalueindex(1));
                        auto obj = (C*)lua_touserdata(L, 1);
                        return (obj->*f)(forward<A>(a)...);
                    });
                }, 1);
            }
        };

        template <>
        struct Stack<string> : LuaObj
        {
            static void push(string const& v)
            {
                lua_pushlstring(L(), v.c_str(), v.length());
            }
            static string get(int index)
            {
                return Stack<const char*>::get(index);
            }
        };

        template <typename T>
        struct Stack<std::vector<T>> : LuaObj
        {
            static void push(const std::vector<T>& v)
            {
                auto& tb = LuaMgr::get()->newTable();
                for (auto& i : v) tb.append(i);
                tb.push();
            }
            static std::vector<T> get(int index)
            {
                const LuaRef& tb = LuaRef::fromIndex(index);
                std::vector<T> v;
                for (auto i : tb) v.emplace_back((T)i.second);
                return v;
            }
        };

        template <typename K, typename V>
        struct Stack<std::map<K, V>> : LuaObj
        {
            static void push(const std::map<K, V>& v)
            {
                auto& tb = LuaMgr::get()->newTable();
                for (auto& i : v) tb[i.first] = i.second;
                tb.push();
            }
            static std::map<K, V> get(int index)
            {
                const LuaRef& tb = LuaRef::fromIndex(index);
                std::map<K, V> v;
                for (auto i : tb) v[(K)i.first] = std::move((V)i.second);
                return v;
            }
        };
    }


}
