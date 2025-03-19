#include <QJsonObject>
#include <QMap>
#include <QVariant>
#include <QtAssert>


inline QJsonObject makeJsonObject(std::initializer_list<std::pair<QLatin1StringView, QJsonValue>> args)
{
    QJsonObject obj;
    for (auto &arg : args)
        obj.insert(arg.first, arg.second);
    return obj;
}

inline QJsonObject &extend(QJsonObject &obj, const QMap<QLatin1StringView, QVariant> &values)
{
    for (auto [key, value] : values.asKeyValueRange())
        obj.insert(key, QJsonValue::fromVariant(value));
    return obj;
}

// copied from qfile.h
inline QString toQString(const std::filesystem::path &path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QString::fromStdString(path.native());
#endif
}

// copied from qfile.h
inline auto toFSPath(const QString &str) -> std::filesystem::path
{
    return { reinterpret_cast<const char16_t *>(str.cbegin()),
             reinterpret_cast<const char16_t *>(str.cend  ())  };
}

inline FileError::FileError(const QString &str, QFileDevice::FileError code)
    : std::runtime_error(str.toUtf8().constData())
    , m_code(code)
{ Q_ASSERT(code); }
