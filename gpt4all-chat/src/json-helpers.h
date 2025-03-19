#pragma once

class QUrl;
class QUuid;
namespace boost::json {
    class value;
    struct value_from_tag;
    template <typename T> struct value_to_tag;
}

void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const QUuid &uuid);
QUuid tag_invoke(const boost::json::value_to_tag<QUuid> &, const boost::json::value &value);

void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const QUrl &url);
QUrl tag_invoke(const boost::json::value_to_tag<QUrl> &, const boost::json::value &value);
