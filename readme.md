# TLUA - Tiny LUA binding utility 

便携版lua + luasocket + qlua（cpp binding）

# Hightlights

- official lua5.3 packed in 1 h&cpp files.
- luasocket for remote debuging packed in 1 h&cpp files.
- tiny lua binding utility (expired by LuaBridge)


# Binding features:

- support most stl containers.
- support stl function from lua and back.
- access lua tables with table like syntax.
- support overloaded C++ functions
- clean binding syntax.

# example

```c++

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
    static bool Zip(QString f, QString saveFile, function<void(QString)> prog)
    {
        mz_zip_archive zip_archive = { 0 };
        auto status = mz_zip_writer_init_file(&zip_archive, saveFile.toUtf8(), 0);
        if ( !status ) {
            qCritical("mz_zip_writer_init_file() failed!\n");
            return false;
        }

        auto root = QDir(f);
        function<void(QString)> handleDir = [&](QString d) {
            QDir dir(d);
            for ( auto info : dir.entryInfoList(QDir::Filter::NoDotAndDotDot | QDir::Filter::AllEntries, QDir::SortFlag::DirsFirst) ) {

                qApp->processEvents();
                auto absPath = info.filePath();
                if ( prog ) prog(absPath);

                if ( info.isFile() ) {
                    auto relPath = root.relativeFilePath(absPath);
                    mz_zip_writer_add_file(&zip_archive, relPath.toUtf8(), absPath.toUtf8(), 0, 0, MZ_DEFAULT_COMPRESSION);
                } else if ( info.isDir() ) {
                    handleDir(absPath);
                }
            }
        };
        handleDir(f);
        status = mz_zip_writer_finalize_archive(&zip_archive);
        if ( !status )
            return false;
        mz_zip_writer_end(&zip_archive);
        return true;
    }

    static bool Unzip(QString zip, QString saveFolder, function<void(QString, int cur, int total)> prog)
    {
        if ( !saveFolder.isEmpty() )
            QDir(saveFolder).removeRecursively();

        mz_zip_archive zip_archive = { 0 };
        auto status = mz_zip_reader_init_file(&zip_archive, zip.toUtf8(), 0);
        if ( !status ) {
            qCritical("mz_zip_reader_init_file() failed!\n");
            return false;
        }

        auto total = (int)mz_zip_reader_get_num_files(&zip_archive);
        for ( auto i = 0; i < total; i++ ) {
            qApp->processEvents();

            mz_zip_archive_file_stat file_stat;
            if ( !mz_zip_reader_file_stat(&zip_archive, i, &file_stat) ) {
                qCritical("mz_zip_reader_file_stat() failed!\n");
                mz_zip_reader_end(&zip_archive);
                return false;
            }

            if ( prog ) prog(file_stat.m_filename, i + 1, total);
            if ( file_stat.m_is_directory ) {
                QDir(saveFolder).mkpath(file_stat.m_filename);
                continue;
            }

            auto dstFileName = saveFolder + "/" + file_stat.m_filename;
            mz_zip_reader_extract_file_to_file(&zip_archive, file_stat.m_filename, dstFileName.toUtf8(), 0);
        }

        mz_zip_reader_end(&zip_archive);
        return true;
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
