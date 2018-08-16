# TLUA - Tiny LUA binding utility 

tlua(binding utility) + packed lua(optional) + luasocket(optional)


# Hightlights

- tiny lua binding utility in one header file(expired by LuaBridge)
- official lua5.3 packed in 1 h&cpp files.
- luasocket for remote debuging packed in 1 h&cpp files.
- no other dependencies


# Binding features:

- support most stl containers.
- support binding enum and lambda.
- support using stl function to call lua and vice versa.
- access lua tables with std::map like syntax(can chain).
- support overloaded C++ functions
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

The MIT License

```
Copyright (C) 2018 soniced@sina.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
