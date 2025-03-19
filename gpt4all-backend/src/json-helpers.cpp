#include "json-helpers.h"

#include <boost/json.hpp> // IWYU pragma: keep

#include <QByteArray>
#include <QString>

namespace json = boost::json;


void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const QString &qstr)
{
    auto utf8 = qstr.toUtf8();
    value = json::value_from(json::string_view(utf8.data(), utf8.size()));
}

QString tag_invoke(const boost::json::value_to_tag<QString> &, const boost::json::value &value)
{
    auto &s = value.as_string();
    return QString::fromUtf8(s.data(), s.size());
}
