#pragma once

#include <QFileDevice>
#include <QHash>
#include <QJsonValue>
#include <QBitArray> // for qHash overload // IWYU pragma: keep
#include <QLatin1StringView> // IWYU pragma: keep

#include <concepts>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <utility> // IWYU pragma: keep

// IWYU pragma: no_forward_declare QJsonValue
class QJsonObject;
class QVariant;
template <typename Key, typename T> class QMap;


// alternative to QJsonObject's initializer_list constructor that accepts Latin-1 strings
QJsonObject makeJsonObject(std::initializer_list<std::pair<QLatin1StringView, QJsonValue>> args);

QJsonObject &extend(QJsonObject &obj, const QMap<QLatin1StringView, QVariant> &values);

QString toQString(const std::filesystem::path &path);
auto    toFSPath (const QString &str) -> std::filesystem::path;

template <typename T>
concept QHashable = requires(const T &x) {
    { qHash(x) } -> std::same_as<size_t>;
};

template <QHashable T>
struct std::hash<T> {
    size_t operator()(const T &value) const noexcept
    { return qHash(value); }
};

class FileError : public std::runtime_error {
public:
    explicit FileError(const QFileDevice *file)
        : FileError(file->errorString(), file->error()) {}
    explicit FileError(const QString &str, QFileDevice::FileError code);
    QFileDevice::FileError code() const noexcept { return m_code; }

private:
    QFileDevice::FileError m_code;
};

template <typename... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };


#include "utils.inl" // IWYU pragma: export
