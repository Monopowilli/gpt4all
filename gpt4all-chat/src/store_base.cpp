#include "store_base.h"

#include <fmt/format.h>
#include <gpt4all-backend/formatters.h> // IWYU pragma: keep
#include <tl/generator.hpp>

#include <QByteArray>
#include <QDebug>
#include <QIODevice>
#include <QLatin1StringView> // IWYU pragma: keep
#include <QSaveFile>
#include <QUrl>
#include <QtAssert>
#include <QtLogging>

#include <array>
#include <stdexcept>
#include <string>
#include <system_error>

namespace fs   = std::filesystem;
namespace json = boost::json;
namespace sys  = boost::system;
using namespace Qt::StringLiterals;


namespace gpt4all::ui {


DataStoreError::DataStoreError(std::error_code e)
    : m_error(e)
    , m_errorString(QString::fromStdString(e.message()))
    {}

DataStoreError::DataStoreError(const sys::system_error &e)
    : m_error(e.code())
    , m_errorString(QString::fromUtf8(e.what()))
{
    Q_ASSERT(e.code());
}

DataStoreError::DataStoreError(const QFileDevice *file)
    : m_error(file->error())
    , m_errorString(file->errorString())
{
    Q_ASSERT(file->error());
}

DataStoreError::DataStoreError(QString e)
    : m_error()
    , m_errorString(e)
    {}

void DataStoreError::raise() const
{
    std::visit(Overloaded {
        [&](std::error_code        e) { throw std::system_error(e);                                   },
        [&](sys::error_code        e) { throw std::runtime_error(m_errorString.toUtf8().constData()); },
        [&](QFileDevice::FileError e) { throw FileError(m_errorString, e);                            },
        [&](std::monostate          ) { throw std::runtime_error(m_errorString.toUtf8().constData()); },
    }, m_error);
    Q_UNREACHABLE();
}

auto DataStoreBase::reload() -> DataStoreResult<>
{
    if (auto res = clear(); !res)
        return res;

    json::stream_parser parser;
    QFile file;

    fs::directory_iterator it;
    try {
        it = fs::directory_iterator(m_path);
    } catch (const fs::filesystem_error &e) {
        if (e.code() == std::errc::no_such_file_or_directory) {
            fs::create_directories(m_path);
            return {}; // brand new dir, nothing to load
        }
        throw;
    }

    for (auto &entry : it) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue; // skip directories and such
        file.setFileName(entry.path());
        if (!file.open(QFile::ReadOnly)) {
            qWarning().noquote() << "skipping unopenable file:" << file.fileName();
            continue;
        }
        auto jv = read(file, parser);
        if (!jv) {
            (qWarning().nospace() << "skipping " << file.fileName() << " because of read error: ").noquote()
                << jv.error().errorString();
        } else if (auto [unique, uuid] = cacheInsert(entry.path().stem(), *jv); !unique)
            qWarning() << "skipping duplicate data store entry:" << uuid;
        file.close();
    }
    return {};
}

auto DataStoreBase::setPath(fs::path path) -> DataStoreResult<>
{
    if (path != m_path) {
        m_path = std::move(path);
        return reload();
    }
    return {};
}

QByteArray DataStoreBase::normalizeName(const QString &name)
{
    auto lower = name.toLower();
    auto norm = QUrl::toPercentEncoding(lower, /*exclude*/ " !#$%&'()+,;=@[]^`{}"_ba, /*include*/ "~"_ba);

    // leading dot indicates a hidden file on Unix
    if (norm.startsWith('.'))
        norm = "%2E"_ba.append(QByteArrayView(norm).slice(1));
    return norm;
}

auto DataStoreBase::getFilePath(const QByteArray &normName) -> fs::path
{ return m_path / fmt::format("{}.json", QLatin1StringView(normName)); }

auto DataStoreBase::openNew(const QByteArray &normName) -> DataStoreResult<std::unique_ptr<QFile>>
{
    auto path = getFilePath(normName);
    auto file = std::make_unique<QFile>(path);
    if (file->exists())
        return std::unexpected(sys::system_error(std::make_error_code(std::errc::file_exists), path.string()));
    if (!file->open(QFile::WriteOnly | QFile::NewOnly))
        return std::unexpected(&*file);
    return file;
}

auto DataStoreBase::openExisting(const QByteArray &normName, bool allowCreate) -> DataStoreResult<std::unique_ptr<QSaveFile>>
{
    auto path = getFilePath(normName);
    if (!allowCreate && !QFile::exists(path))
        return std::unexpected(sys::system_error(
            std::make_error_code(std::errc::no_such_file_or_directory), path.string()
        ));
    auto file = std::make_unique<QSaveFile>(toQString(path));
    QFile::OpenMode flags = QSaveFile::WriteOnly;
    if (!allowCreate)
        flags |= QSaveFile::ExistingOnly;
    if (!file->open(flags))
        return std::unexpected(&*file);
    return file;
}

auto DataStoreBase::read(QFileDevice &file, json::stream_parser &parser) -> DataStoreResult<json::value>
{
    // chunk stream
    auto iterChunks = [&] -> tl::generator<DataStoreResult<QByteArray>> {
        for (;;) {
            auto chunk = file.read(JSON_BUFSIZ);
            if (file.error()) {
                DataStoreResult<QByteArray> res(std::unexpect, &file);
                co_yield res;
            }
            if (chunk.isEmpty()) {
                Q_ASSERT(file.atEnd());
                break;
            }
            DataStoreResult<QByteArray> res(std::move(chunk));
            co_yield res;
        }
    };

    bool partialRead = false;
    auto chunkIt = iterChunks();

    // read JSON data
    parser.reset();
    for (auto &chunk : chunkIt) {
        if (!chunk)
            return std::unexpected(chunk.error());
        size_t nRead = parser.write_some(chunk->data(), chunk->size());
        // consume trailing whitespace in chunk
        if (nRead < chunk->size()) {
            auto rest = QByteArrayView(*chunk).slice(nRead);
            if (!rest.trimmed().isEmpty())
                return std::unexpected(u"unexpected data after json: \"%1\""_s.arg(QByteArray(rest)));
            partialRead = true;
            break;
        }
    }
    // consume trailing whitespace in file
    if (partialRead) {
        for (auto &chunk : chunkIt) {
            if (!chunk)
                return std::unexpected(chunk.error());
            if (!chunk->trimmed().isEmpty())
                return std::unexpected(u"unexpected data after json: \"%1\""_s.arg(*chunk));
        }
    }
    return parser.release();
}

auto DataStoreBase::write(const json::value &value, QFileDevice &file) -> DataStoreResult<>
{
    qint64 nWritten;
    m_serializer.reset(&value);
    std::array<char, JSON_BUFSIZ> buf;
    while (!m_serializer.done()) {
        auto chunk = m_serializer.read(buf.data(), buf.size());
        nWritten = file.write(chunk.data(), chunk.size());
        if (nWritten < 0)
            return std::unexpected(&file);
        Q_ASSERT(nWritten == chunk.size());
    }

    // write trailing newline to make it a valid text file
    nWritten = file.write("\n"_ba);
    if (nWritten < 0)
        return std::unexpected(&file);
    Q_ASSERT(nWritten == 1);

    if (!file.flush())
        return std::unexpected(&file);

    return {};
}


} // namespace gpt4all::ui
