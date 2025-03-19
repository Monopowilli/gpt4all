#include "json-helpers.h" // IWYU pragma: keep

#include <boost/json.hpp> // IWYU pragma: keep
#include <gpt4all-backend/json-helpers.h> // IWYU pragma: keep

#include <QDebug>
#include <QSaveFile>
#include <QtAssert>
#include <QtLogging>

#include <algorithm>
#include <tuple>


namespace gpt4all::ui {


template <typename T>
DataStore<T>::DataStore(std::filesystem::path path)
    : DataStoreBase(std::move(path))
{
    if (auto res = reload(); !res)
        res.error().raise(); // should be impossible
}

template <typename T>
auto DataStore<T>::createImpl(T data, const QString &name) -> DataStoreResult<>
{
    // acquire id
    typename decltype(m_entries)::iterator entry;
    bool unique;
    std::tie(entry, unique) = m_entries.emplace(data.id, std::move(data));
    if (!unique)
        return std::unexpected(QStringLiteral("id not unique: %1").arg(data.id.toString()));

    auto *pdata = &entry->second;
    auto normName = normalizeName(name);
    auto [nameIt, unique2] = m_normNames.emplace(pdata->id, normName);
    Q_UNUSED(unique2)
    Q_ASSERT(unique2);

    // acquire name
    typename decltype(m_normNameToId)::iterator n2idIt;
    std::tie(n2idIt, unique) = m_normNameToId.emplace(std::move(normName), pdata->id);
    if (!unique) {
        m_entries.erase(entry);
        m_normNames.erase(nameIt);
        return std::unexpected(QStringLiteral("name not unique: %1").arg(QLatin1StringView(normName)));
    }

    // acquire path
    auto file = openNew(normName);
    if (!file) {
        m_entries.erase(entry);
        m_normNames.erase(nameIt);
        m_normNameToId.erase(n2idIt);
        return std::unexpected(file.error());
    }

    // serialize
    if (auto res = write(boost::json::value_from(*pdata), **file); !res) {
        m_entries.erase(entry);
        m_normNames.erase(nameIt);
        m_normNameToId.erase(n2idIt);
        if (!(*file)->remove())
            (qWarning().nospace() << "failed to remove " << (*file)->fileName()).noquote() << ": " << (*file)->errorString();
        return std::unexpected(res.error());
    }
    return {};
}

template <typename T>
auto DataStore<T>::setData(T data, const QString &name, bool create) -> DataStoreResult<>
{
    // acquire name
    auto normName = normalizeName(name);
    auto n2idIt = m_normNameToId.find(normName);
    if (n2idIt != m_normNameToId.end() && n2idIt->second != data.id)
        return std::unexpected(QStringLiteral("name is not unique: %1").arg(QLatin1StringView(normName)));

    // determine filename to open
    bool isNew = false;
    QByteArray *openName;
    auto nameIt = m_normNames.find(data.id);
    if (nameIt != m_normNames.end()) {
        openName = &nameIt->second;
    } else if (create) {
        isNew = true;
        openName = &normName;
    } else
        return std::unexpected(QStringLiteral("id not found: %1").arg(data.id.toString()));

    bool isRename = !isNew && n2idIt == m_normNameToId.end();

    // acquire path
    auto file = openExisting(*openName, create);
    if (!file)
        return std::unexpected(file.error());

    // serialize
    if (auto res = write(boost::json::value_from(data), **file); !res)
        return std::unexpected(res.error());
    if (!(*file)->commit())
        return std::unexpected(file->get());

    // update cache
    auto id = data.id;
    if (isNew) {
        [[maybe_unused]] bool unique;
        std::tie(std::ignore, unique) = m_entries  .emplace(data.id, std::move(data));
        Q_ASSERT(unique);
        std::tie(std::ignore, unique) = m_normNames.emplace(data.id, normName       );
        Q_ASSERT(unique);
    } else {
        m_entries.at(data.id) = std::move(data);
        nameIt->second = normName;
    }

    // update name
    if (isRename) {
        [[maybe_unused]] auto nRemoved = m_normNameToId.erase(*openName); // remove old name
        Q_ASSERT(nRemoved);

        std::error_code ec;
        std::filesystem::rename(getFilePath(*openName), getFilePath(normName), ec);
        if (ec)
            return std::unexpected(ec); // FIXME(jared): attempt rollback? the state is now inconsistent.
    }
    if (isNew || isRename) {
        auto [_, unique] = m_normNameToId.emplace(*openName, id);
        Q_ASSERT(unique);
    }
    return {};
}

template <typename T>
auto DataStore<T>::remove(const QUuid &id) -> DataStoreResult<>
{
    // acquire UUID
    auto nameIt = m_normNames.find(id);
    if (nameIt == m_normNames.end())
        return std::unexpected(QStringLiteral("id not found: %1").arg(id.toString()));

    // remove the path
    auto path = getFilePath(nameIt->second);
    QFile file(path);
    if (!file.remove())
        throw std::unexpected(&file);

    // update cache
    auto normName = std::move(nameIt->second);
    m_normNames.erase(nameIt);
    [[maybe_unused]] size_t nRemoved;
    nRemoved = m_entries.erase(id);
    Q_ASSERT(nRemoved);
    nRemoved = m_normNameToId.erase(normName);
    Q_ASSERT(nRemoved);
    return {};
}

template <typename T>
auto DataStore<T>::acquire(QUuid id) -> DataStoreResult<std::optional<const T *>>
{
    auto [it, unique] = m_acquired.insert(std::move(id));
    if (!unique)
        return std::unexpected(QStringLiteral("id already acquired: %1").arg(id.toString()));
    return find(*it);
}

template <typename T>
auto DataStore<T>::release(const QUuid &id) -> DataStoreResult<>
{
    if (!m_acquired.erase(id))
        return std::unexpected(QStringLiteral("id not acquired: %1").arg(id.toString()));
    return {};
}

template <typename T>
auto DataStore<T>::clear() -> DataStoreResult<>
{
    if (!m_acquired.empty())
        return std::unexpected(QStringLiteral("cannot clear data store with living references"));
    m_entries.clear();
    m_normNameToId.clear();
    return {};
}

template <typename T>
auto DataStore<T>::cacheInsert(const std::filesystem::path &stem, const boost::json::value &jv) -> CacheInsertResult
{
    auto data = boost::json::value_to<T>(jv);
    auto id = data.id;
    auto [entryIt, unique] = m_entries.emplace(id, std::move(data));
    if (!unique)
        return { .unique = false, .id = std::move(id) };
    auto normName = toQString(stem).toUtf8(); // FIXME(jared): better not to trust the filename
    std::tie(std::ignore, unique) = m_normNameToId.emplace(normName, id);
    if (!unique) {
        m_entries.erase(entryIt);
        return { .unique = false, .id = std::move(id) };
    }
    std::tie(std::ignore, unique) = m_normNames.emplace(id, std::move(normName));
    Q_ASSERT(unique);
    return { .unique = true, .id = std::move(id) };
}


} // namespace gpt4all::ui
