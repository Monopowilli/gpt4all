#pragma once

class QString;
namespace boost::json {
    class value;
    struct value_from_tag;
    template <typename T> struct value_to_tag;
}


/// Allows QString to be serialized to JSON.
void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const QString &qstr);

/// Allows JSON strings to be deserialized as QString.
QString tag_invoke(const boost::json::value_to_tag<QString> &, const boost::json::value &value);
