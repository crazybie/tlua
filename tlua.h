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
#include <cassert>

#ifndef NO_MINI_LUA
#include "lua.h"
#endif

#include "luasocket.h"


#define TLuaType(Type, funcs) \
    static auto __reg_##Type = tlua::LuaMgr::getRegisters().insert({#Type, []{ \
        typedef Type Class; \
        auto& table = tlua::LuaMgr::get()->newType<Type>(#Type); \
        funcs \
    }});

#define _TLuaTypeBase(base)                    table["base"] = #base;
#define TLuaField(name, val)                   table[#name] = val;
#define TLuaTypeInherit(name, base, funcs)     TLuaType(name, funcs _TLuaTypeBase(base) )
#define TLuaConstructor(...)                   table["New"] = &tlua::Construct<Class,__VA_ARGS__>; table["Delete"] = &tlua::Destruct<Class>;
#define TLuaFunc(name)                         table[#name] = &Class::name;
#define TLuaFuncOverload(name, newName, type)  table[#newName] = type &Class::name;
#define TLuaEnumItem(name)                     table[#name] = Class::name;


namespace tlua
{
    using namespace std;

    namespace helpers
    {
        // c++14 index_sequence in c++11
        template<size_t...>					struct index_sequence {};
        template<size_t N, size_t... Is>	struct make_index_sequence : make_index_sequence<N - 1, N - 1, Is...> {};
        template<size_t... Is>				struct make_index_sequence<0, Is...> : index_sequence<Is...> {};

        //////////////////////////////////////////////////////////////////////////

        template <typename T>
        struct is_functor
        {
            template <typename U> static char deduce(decltype(&U::operator())*);
            template <typename U> static int deduce(...);

            static constexpr bool value = sizeof(deduce<T>(0)) == 1;
        };

        //////////////////////////////////////////////////////////////////////////

        template <typename T>
        struct function_traits : function_traits<decltype(&T::operator())>
        {};

        template<class C, class R, class... Args>
        struct function_traits<R(C::*)(Args...)const>
        {
            using return_type = R;
            using argument_tuple = std::tuple<Args...>;
        };

        //////////////////////////////////////////////////////////////////////////

        // void trick.
        template <typename T, typename Default>
        T operator,(T&& v, Default)
        {
            return forward<T>(v);
        }

        //////////////////////////////////////////////////////////////////////////

        template<typename T, typename... A>
        T* Construct(A... a) { return new T(a...); }

        template<typename T>
        void Destruct(T* d) { delete d; }

        template<typename... A>
        string Sprintf(const char* fmt, A&&... args) 
        {
            char buf[255];
            sprintf_s(buf, fmt, forward<A>(args)...);
            return buf;
        }
    }

    //////////////////////////////////////////////////////////////////////////

    using namespace helpers;

    struct Nil
    {};

    struct LuaObj
    {
        static lua_State* L;

        struct PopOnExit
        {
            ~PopOnExit()
            {
                lua_pop(L, 1);
            }
        };
    };

    template<typename T, bool isEnum, bool isFunctor>
    struct StackHelper;

    //////////////////////////////////////////////////////////////////////////

    template <typename T>
    struct Stack
    {
        using Imp = StackHelper<T, is_enum_v<T>, is_functor<T>::value>;

        static T get(int index)
        {
            return Imp::get(index);
        }
        static void push(T&& r)
        {
            Imp::push(forward<T>(r));
        }
    };

    //////////////////////////////////////////////////////////////////////////

    class FuncHelper : public LuaObj
    {
    public:
        template<typename R, typename... A, typename F>
        static int callCpp(tuple<A...>*, int argsOffset, F&& f)
        {
            return callCpp<R, A...>(argsOffset, forward<F>(f));
        }
        template<typename R, typename... A, typename F>
        static int callCpp(int argsOffset, F&& f)
        {
            try {
                Stack<R>::push((callCpp<R, A...>(argsOffset, forward<F>(f), make_index_sequence<sizeof...(A)>()), Nil()));
                return std::is_same<R, void>::value ? 0 : 1;
            }
            catch (std::exception &e) {
                luaL_error(L, "C++ exception: %s", e.what());
            }
            catch (...) {
                luaL_error(L, "C++ exception: unknown");
            }
            return 0;
        }
        template<typename R, typename... A>
        static R callLua(A&&... a)
        {
            PopOnExit t;
            lua_getglobal(L, "__traceback");
            lua_insert(L, -2);
            std::initializer_list<char> ordered = { (Stack<A>::push(forward<A>(a)),0)... };
            int nargs = sizeof...(A);
            lua_pcall(L, nargs, 1, -nargs - 2);
            return Stack<R>::get(-1);
        }
    private:
        template<typename R, typename... A, typename F, size_t... index>
        static R callCpp(int argsOffset, F&& f, index_sequence<index...>)
        {
            auto expectedNumArgs = sizeof...(A) + argsOffset - 1;
            auto numArgs = lua_gettop(L);
            if (numArgs < expectedNumArgs)
                throw std::runtime_error(Sprintf("Invalid arguments count: expect: %d, got %d", expectedNumArgs, numArgs));
            return f(Stack<A>::get(argsOffset + index)...);
        }
    };

    //////////////////////////////////////////////////////////////////////////

    class LuaRef;
    class Iterator;

    class LuaRefBase : public LuaObj
    {
    public:
        void iniFromStack();
        virtual ~LuaRefBase();
        virtual void push() const;
        int type() const;
        int createRef() const;
        void pop();
        bool isNil() const;
        explicit operator bool()const;
        int length() const;

        template <typename T>
        explicit operator T() const
        {
            PopOnExit p;
            push();
            return Stack<T>::get(lua_gettop(L));
        }

        template <typename R = void, typename... A>
        R call(A&&... a) const
        {
            push();
            return FuncHelper::callLua<R>(forward<A>(a)...);
        }

        template <class T>
        void append(T&& v) const
        {
            push();
            Stack <T>::push(forward<T>(v));
            luaL_ref(L, -2);
            lua_pop(L, 1);
        }
    protected:
        int m_ref = LUA_REFNIL;
    };

    //////////////////////////////////////////////////////////////////////////

    class TableProxy : public LuaRefBase
    {
        int m_tableRef;
    public:
        TableProxy(int tableRef);
        TableProxy(TableProxy const& other) = delete;
        void operator=(TableProxy const& other) = delete;
        TableProxy(TableProxy&& other);
        void push() const override;
        template <typename T>
        TableProxy& operator= (T&& v)
        {
            PopOnExit p;
            lua_rawgeti(L, LUA_REGISTRYINDEX, m_tableRef);
            lua_rawgeti(L, LUA_REGISTRYINDEX, m_ref);
            Stack <T>::push(forward<T>(v));
            lua_rawset(L, -3);
            return *this;
        }
        template <typename T>
        TableProxy operator[] (T&& key) const
        {
            return LuaRef(*this)[forward<T>(key)];
        }
    };

    //////////////////////////////////////////////////////////////////////////

    class LuaRef : public LuaRefBase
    {
    public:
        LuaRef()
        {}
        LuaRef(TableProxy const& other);
        LuaRef(LuaRef&& other);
        LuaRef& operator=(LuaRef&& other);
        LuaRef(LuaRef const& other);
        static LuaRef fromIndex(int index);
        static LuaRef fromStack();
        template <typename T>
        TableProxy operator[] (T&& key) const
        {
            Stack<T>::push(forward<T>(key));
            return TableProxy(m_ref);
        }
        template <typename T>
        LuaRef& operator= (T&& rhs)
        {
            luaL_unref(L, LUA_REGISTRYINDEX, m_ref);
            Stack <T>::push(forward<T>(rhs));
            m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            return *this;
        }
        Iterator begin() const;
        Iterator end() const;
    };

    //////////////////////////////////////////////////////////////////////////

    class Iterator : public LuaObj
    {
    public:
        Iterator()
        {}
        explicit Iterator(const LuaRef& table);
        Iterator& operator++ ();
        bool operator==(const Iterator& r)const;
        bool operator!=(const Iterator& r)const;
        std::pair<LuaRef, LuaRef> operator*();
        void next();
    private:
        LuaRef m_table, m_key, m_value;
        bool valid = false;
    };

    //////////////////////////////////////////////////////////////////////////

    class LuaMgr : public LuaObj
    {
    public:
        typedef void(*Register)();

        LuaMgr();
        void setSourceRoot(string luaRoot = "");
        void wrapClasses();
        virtual ~LuaMgr();
        static std::map<string, Register>& getRegisters();
        LuaRef doFile(const char *name);
        LuaRef doString(const char* name);
        LuaRef newTable();
        LuaRef getGlobal(const char* name);
        static LuaMgr* get() { return instance; }

        template<typename T>
        void setGlobal(const char* name, T&& t)
        {
            Stack<T>::push(forward<T>(t));
            lua_setglobal(L, name);
        }

        template<typename T>
        static string& typeNames() 
        {
            static string s;
            return s;
        }
        template<typename T>
        LuaRef newType(const char* name) 
        {
            auto& r = newTable();
            r["name"] = name;
            typeNames<T>() = name;
            setGlobal(name, r);
            return r;
        }

        function<void(const char*)> logError;
        function<string(const char*)> fileLoader;

    private:
        static string loadFile(const char* name);
        static void traceback(const char* msg);
        static int luaLoader(lua_State* L);

    private:
        string srcDir;
        static LuaMgr* instance;
    };

    //////////////////////////////////////////////////////////////////////////
    /// basic types

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
            lua_pushnil(L);
        }
    };

    template <>
    struct Stack <lua_CFunction> : LuaObj
    {
        static void push(lua_CFunction f)
        {
            lua_pushcfunction(L, f);
        }
    };

    template <>
    struct Stack<lua_Number> : LuaObj
    {
        static lua_Number get(int index)
        {
            return lua_tonumber(L, index);
        }
        static void push(lua_Number r)
        {
            lua_pushnumber(L, r);
        }
    };

    template <>
    struct Stack<lua_Integer> : LuaObj
    {
        static lua_Integer get(int index)
        {
            return lua_tointeger(L, index);
        }
        static void push(lua_Integer r)
        {
            lua_pushinteger(L, r);
        }
    };

#ifndef TLUA_INT_AS_INTEGER

    template <>
    struct Stack<int> : Stack<lua_Integer> {};

    template <>
    struct Stack<unsigned int> : Stack<lua_Integer> {};

#endif

    // enum
    template<typename T>
    struct StackHelper<T, true, false> : LuaObj
    {
        static T get(int index)
        {
            return (T)lua_tointeger(L, index);
        }
        static void push(T r)
        {
            lua_pushinteger(L, (int)r);
        }
    };

    template <>
    struct Stack<const char*> : LuaObj
    {
        static const char* get(int index)
        {
            return lua_tostring(L, index);
        }
        static void push(const char *r)
        {
            lua_pushstring(L, r);
        }
    };

    template <int N>
    struct Stack < const char(&)[N] > : LuaObj
    {
        static void push(const char *r)
        {
            lua_pushlstring(L, r, N - 1);
        }
    };

    template <>
    struct Stack<bool> : LuaObj
    {
        static bool	get(int index)
        {
            return lua_toboolean(L, index) != 0;
        }
        static void	push(bool r)
        {
            lua_pushboolean(L, r ? 1 : 0);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    /// std types

    template <>
    struct Stack<string> : LuaObj
    {
        static void push(string const& v)
        {
            lua_pushlstring(L, v.c_str(), v.length());
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
            for (auto& i : tb) v.emplace_back((T)i.second);
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
            for (auto& i : tb) v[(K)i.first] = std::move((V)i.second);
            return v;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // user types

    struct UserData 
    {
        void* ptr;
    };

    template<typename T>
    struct UserDataValue : UserData
    {
        char buffer[sizeof(T)];
    };

    // general value type
    template<typename T>
    struct StackHelper<T, false, false> : LuaObj
    {
        static T get(int index)
        {
            return *Stack<T*>::get(index);
        }
        static void push(const T& r)
        {
            auto* p = (UserDataValue<T>*)lua_newuserdata(L, sizeof(UserDataValue<T>));
            p->ptr = p->buffer;
            new (p->ptr) T(r);
            Stack<T*>::setMetatable();
        }
    };

    template <typename T>
    struct Stack<T*> : LuaObj
    {
        static T* get(int index)
        {
            auto p = static_cast<UserData*>(lua_touserdata(L, index));
            return p && p->ptr ? (T*)p->ptr : nullptr;
        }
        static void push(T* r)
        {
            auto d = (UserData*)lua_newuserdata(L, sizeof(UserData));
            d->ptr = r;
            setMetatable();
        }
        static void setMetatable()
        {
            lua_getglobal(L, LuaMgr::typeNames<T>().c_str());
            assert(lua_istable(L, -1) && "type not registered");
            lua_setmetatable(L, -2);
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
            *(F*)lua_newuserdata(L, sizeof(F)) = f;
            lua_pushcclosure(L, [](lua_State* L) {
                auto f = *(F*)lua_touserdata(L, lua_upvalueindex(1));
                return FuncHelper::callCpp<R, A...>(1, f);
            }, 1);
        }
    };

    template<typename R, typename... A>
    struct Stack<function<R(A...)>> : LuaObj
    {
        static void push(const function<R(A...)>& f)
        {
            using F = function<R(A...)>;
            new (lua_newuserdata(L, sizeof(F))) F(f);
            lua_pushcclosure(L, [](lua_State* L) {
                auto& f = *(F*)lua_touserdata(L, lua_upvalueindex(1));
                return FuncHelper::callCpp<R, A...>(1, f);
            }, 1);
        }
        static function<R(A...)> get(int idx)
        {
            auto& f = LuaRef::fromIndex(idx);
            return [=](A&&... a) {
                return (R)f.call(forward<A>(a)...);
            };
        }
    };

    // lambda
    template<typename T>
    struct StackHelper<T, false, true> : LuaObj
    {
        static void push(T&& f)
        {
            new (lua_newuserdata(L, sizeof(T))) T(move(f));
            lua_pushcclosure(L, [](lua_State* L) {
                auto& f = *(T*)lua_touserdata(L, lua_upvalueindex(1));
                using FT = function_traits<T>;
                return FuncHelper::callCpp<FT::return_type>((FT::argument_tuple*)nullptr, 1, f);
            }, 1);
        }
    };

    template< typename R, typename C, typename... A>
    struct Stack<R(C::*)(A...)> : LuaObj
    {
        static void push(R(C::*f)(A...))
        {
            using MF = decltype(f);
            *(MF*)lua_newuserdata(L, sizeof(MF)) = f;
            lua_pushcclosure(L, [](lua_State* L) {
                return FuncHelper::callCpp<R, A...>(2, [L](A&&... a) {
                    auto f = *(MF*)lua_touserdata(L, lua_upvalueindex(1));
                    auto obj = Stack<C*>::get(1);
                    if (!obj) throw std::runtime_error("self is nil");
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
            *(MF*)lua_newuserdata(L, sizeof(MF)) = f;
            lua_pushcclosure(L, [](lua_State* L) {
                return FuncHelper::callCpp<R, A...>(2, [L](A&&... a) {
                    auto f = *(MF*)lua_touserdata(L, lua_upvalueindex(1));
                    auto obj = Stack<C*>::get(1);
                    if (!obj) throw std::runtime_error("self is nil");
                    return (obj->*f)(forward<A>(a)...);
                });
            }, 1);
        }
    };

}
