#include "ollama-types.h"

#include "json-helpers.h"

#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#include <date/date.h>

#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace json = boost::json;


template <typename T>
static T get_optional(const json::object &o, json::string_view key)
{
    if (auto *p = o.if_contains(key))
        return value_to<typename T::value_type>(*p);
    return std::nullopt;
}


namespace gpt4all::backend::ollama {


void tag_invoke(const json::value_from_tag &, json::value &value, Time time)
{
    value = json::value_from(fmt::format(
        "{:%FT%T}Z",
        static_cast<const std::chrono::sys_time<std::chrono::nanoseconds> &>(time)
    ));
}

Time tag_invoke(const json::value_to_tag<Time> &, const json::value &value)
{
    namespace sys = boost::system;

    Time time;
    std::istringstream iss(json::string_view(value.as_string()));
    iss >> date::parse("%FT%T%Ez", time);
    if (!iss && !iss.eof())
        throw sys::system_error(std::make_error_code(std::errc::invalid_argument), __func__);
    return time;
}

void tag_invoke(const json::value_from_tag &, json::value &value, const ImageData &image)
{
    auto base64 = image.toBase64();
    value = json::value_from(json::string_view(base64.data(), base64.size()));
}

ImageData tag_invoke(const json::value_to_tag<ImageData> &, const json::value &value)
{
    auto &str = value.as_string();
    return ImageData(QByteArray::fromBase64(QByteArray::fromRawData(str.data(), str.size())));
}

void tag_invoke(const json::value_from_tag &, json::value &value, const ShowResponse &resp)
{
    auto &o = value.emplace_object();

    auto maybe_add = [&o](json::string_view key, auto &v) { if (v) o[key] = json::value_from(*v); };
    maybe_add("license",        resp.license       );
    maybe_add("modelfile",      resp.modelfile     );
    maybe_add("parameters",     resp.parameters    );
    maybe_add("template",       resp.template_     );
    maybe_add("system",         resp.system        );
    maybe_add("details",        resp.details       );
    maybe_add("messages",       resp.messages      );
    maybe_add("model_info",     resp.model_info    );
    maybe_add("projector_info", resp.projector_info);
    maybe_add("modified_at",    resp.modified_at   );
}

ShowResponse tag_invoke(const json::value_to_tag<ShowResponse> &, const json::value &value)
{
    auto &o = value.as_object();
    return {
#define T(name) std::remove_reference_t<decltype(std::declval<ShowResponse>().name)>
        .license        = get_optional<T(license       )>(o, "license"       ),
        .modelfile      = get_optional<T(modelfile     )>(o, "modelfile"     ),
        .parameters     = get_optional<T(parameters    )>(o, "parameters"    ),
        .template_      = get_optional<T(template_     )>(o, "template"      ), // :(
        .system         = get_optional<T(system        )>(o, "system"        ),
        .details        = get_optional<T(details       )>(o, "details"       ),
        .messages       = get_optional<T(messages      )>(o, "messages"      ),
        .model_info     = get_optional<T(model_info    )>(o, "model_info"    ),
        .projector_info = get_optional<T(projector_info)>(o, "projector_info"),
        .modified_at    = get_optional<T(modified_at   )>(o, "modified_at"   ),
#undef T
    };
}


} // namespace gpt4all::backend::ollama
