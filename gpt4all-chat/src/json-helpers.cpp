#include "json-helpers.h"

#include <boost/json.hpp> // IWYU pragma: keep
#include <boost/system.hpp> // IWYU pragma: keep
#include <gpt4all-backend/json-helpers.h>

#include <QByteArray>
#include <QUrl>
#include <QUuid>
#include <QtAssert>

#include <system_error>

namespace json = boost::json;
namespace sys = boost::system;


void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const QUuid &uuid)
{
    auto bytes = uuid.toRfc4122().toBase64();
    value = json::value_from(json::string_view(bytes.data(), bytes.size()));
}

QUuid tag_invoke(const boost::json::value_to_tag<QUuid> &, const boost::json::value &value)
{
    auto &s = value.as_string();
    auto bytes = QByteArray::fromRawData(s.data(), s.size());
    auto result = QByteArray::fromBase64Encoding(bytes);
    if (!result)
        throw sys::system_error(std::make_error_code(std::errc::invalid_argument), __func__);
    auto uuid = QUuid::fromRfc4122(result.decoded);
    Q_ASSERT(!uuid.isNull()); // this may fail if the user manually creates a null UUID
    return uuid;
}

void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const QUrl &url)
{
    auto bytes = url.toEncoded();
    value = json::value_from(json::string_view(bytes.data(), bytes.size()));
}

QUrl tag_invoke(const boost::json::value_to_tag<QUrl> &, const boost::json::value &value)
{
    auto &s = value.as_string();
    return QUrl::fromEncoded(QByteArray::fromRawData(s.data(), s.size()));
}
