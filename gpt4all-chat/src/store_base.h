#pragma once

#include "utils.h" // IWYU pragma: keep

#include <boost/json.hpp> // IWYU pragma: keep
#include <boost/system.hpp> // IWYU pragma: keep

#include <QFile>
#include <QFileDevice>
#include <QString>
#include <QUuid>
#include <QtTypes> // IWYU pragma: keep

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include <ranges>

class QByteArray;
class QSaveFile;


namespace gpt4all::ui {


class DataStoreError {
public:
    using ErrorCode = std::variant<
        std::monostate,
        std::error_code,
        boost::system::error_code,
        QFileDevice::FileError
    >;

    DataStoreError(std::error_code e);
    DataStoreError(const boost::system::system_error &e);
    DataStoreError(const QFileDevice *file);
    DataStoreError(QString e);

    [[nodiscard]] const ErrorCode &error      () const { return m_error;       }
    [[nodiscard]] const QString   &errorString() const { return m_errorString; }

    [[noreturn]] void raise() const;

private:
    ErrorCode m_error;
    QString   m_errorString;
};

template <typename T = void>
using DataStoreResult = std::expected<T, DataStoreError>;

class DataStoreBase {
protected:
    explicit DataStoreBase(std::filesystem::path path)
        : m_path(std::move(path))
        {}

public:
    auto path() const -> const std::filesystem::path & { return m_path; }
    auto setPath(std::filesystem::path path) -> DataStoreResult<>;

protected:
    auto reload() -> DataStoreResult<>;
    virtual auto clear() -> DataStoreResult<> = 0;
    struct CacheInsertResult { bool unique; QUuid id; };
    virtual CacheInsertResult cacheInsert(const std::filesystem::path &stem, const boost::json::value &jv) = 0;

    // helpers
    static QByteArray normalizeName(const QString &name);
    auto getFilePath(const QByteArray &normName) -> std::filesystem::path;
    auto openNew(const QByteArray &normName) -> DataStoreResult<std::unique_ptr<QFile>>;
    auto openExisting(const QByteArray &normName, bool allowCreate = false) -> DataStoreResult<std::unique_ptr<QSaveFile>>;
    static auto read(QFileDevice &file, boost::json::stream_parser &parser) -> DataStoreResult<boost::json::value>;
    auto write(const boost::json::value &value, QFileDevice &file) -> DataStoreResult<>;

private:
    static constexpr uint JSON_BUFSIZ = 16384; // default QFILE_WRITEBUFFER_SIZE

protected:
    std::filesystem::path m_path;

private:
    boost::json::serializer m_serializer;
};

template <typename T>
class DataStore : public DataStoreBase {
public:
    explicit DataStore(std::filesystem::path path);

    auto list() { return m_entries | std::views::transform([](auto &e) { return e.second; }); }
    auto setData(T data, const QString &name, bool create = false) -> DataStoreResult<>;
    auto remove(const QUuid &id) -> DataStoreResult<>;

    auto acquire(QUuid        id) -> DataStoreResult<std::optional<const T *>>;
    auto release(const QUuid &id) -> DataStoreResult<>;

    [[nodiscard]] auto operator[](const QUuid &id) const -> const T &
    { return m_entries.at(id); }
    [[nodiscard]] auto find(const QUuid &id) const -> std::optional<const T *>
    { auto it = m_entries.find(id); return it == m_entries.end() ? std::nullopt : std::optional(&it->second); }

protected:
    auto createImpl(T data, const QString &name) -> DataStoreResult<>;
    auto clear() -> DataStoreResult<> final;
    CacheInsertResult cacheInsert(const std::filesystem::path &stem, const boost::json::value &jv) override;

private:
    std::unordered_map<QUuid,      T         > m_entries;
    std::unordered_map<QUuid,      QByteArray> m_normNames;
    std::unordered_map<QByteArray, QUuid     > m_normNameToId;
    std::unordered_set<QUuid>                  m_acquired;
};


} // namespace gpt4all::ui

#include "store_base.inl" // IWYU pragma: export
