# TLUA - Tiny LUA binding utility 

tlua(binding utility) + packed lua(optional) + luasocket(optional)


# Hightlights

- tiny lua binding utility in one header file(inspired by [LuaBridge](https://github.com/vinniefalco/LuaBridge))
- official lua5.3 packed in 1 h&cpp files.
- luasocket for remote debuging packed in 1 h&cpp files.
- no other dependencies


# Binding features:

- support most **stl containers**.
- support binding **enum** and **lambda**.
- support using **std::function** to call lua and vice versa.
- access lua tables with **std::map** like syntax(**can chain**).
- support **overloaded** C++ functions
- very simple binding syntax.

example:

```c++
// bind QIQevice to lua
TLuaTypeInherit(QIODevice, QObject,
    TLuaFuncOverload(open, 2, (Class* c, QFile::OpenMode m), c->open(m))
    TLuaFieldAddr(close)
    TLuaFuncOverload(read, 2, (Class* c, qint64 sz), c->read(sz))
    TLuaFieldAddr(readAll)
    TLuaFuncOverload(write, 2, (Class* c, const QByteArray &data), c->write(data))
);

class ZipFile
{
public:
    // can use std::function to call lua function
    static bool Zip(QString f, QString saveFile, function<void(QString)> prog)
    {
        // .........
    }
    static bool Unzip(QString zip, QString saveFolder, function<void(QString, int cur, int total)> prog)
    {
        // .........
    }
};

TLuaType(ZipFile,
    TLuaFieldAddr(Zip)
    TLuaFieldAddr(Unzip)
)

int main() {
    tlua::LuaMgr lua;
    lua.setSourceRoot("data/lua");
    lua.setGlobal("print", [](const char* msg) { qDebug(msg); });
    lua.logError = [](const char* err) { QMessageBox::warning(nullptr, "LUA", err); };
    lua.setGlobal("qApp", qApp);
    return (int)lua.doFile("client.main");
}
    
```


### License

- TLUA (the binding utility): [The MIT License](https://github.com/crazybie/tlua/blob/master/LICENSE)
- [Lua](https://www.lua.org/license.html)
- [LuaSocket](https://github.com/diegonehab/luasocket/blob/master/LICENSE)
