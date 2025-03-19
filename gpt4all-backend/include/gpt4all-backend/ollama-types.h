#pragma once

#include "json-helpers.h" // IWYU pragma: keep

#include <boost/describe/class.hpp>
#include <boost/json.hpp> // IWYU pragma: keep

#include <QByteArray>
#include <QString>
#include <QtTypes>

#include <chrono>
#include <optional>
#include <vector>


namespace gpt4all::backend::ollama {


//
// basic types
//

struct Time : std::chrono::sys_time<std::chrono::nanoseconds> {};

void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, Time time);
Time tag_invoke(const boost::json::value_to_tag<Time> &, const boost::json::value &value);

/// ImageData represents the raw binary data of an image file.
struct ImageData : QByteArray {};

void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const ImageData &image);
ImageData tag_invoke(const boost::json::value_to_tag<ImageData> &, const boost::json::value &value);

struct ModelDetails {
    QString              parent_model;
    QString              format;
    QString              family;
    std::vector<QString> families;
    QString              parameter_size;
    QString              quantization_level;
};
BOOST_DESCRIBE_STRUCT(ModelDetails, (), (parent_model, format, family, families, parameter_size, quantization_level))

/// ListModelResponse is a single model description in ListResponse.
struct ListModelResponse {
    QString                     name;
    QString                     model;
    Time                        modified_at;
    qint64                      size;        /// Size of the model on disk.
    QString                     digest;
    std::optional<ModelDetails> details;
};
BOOST_DESCRIBE_STRUCT(ListModelResponse, (), (name, model, modified_at, size, digest, details))

using ToolCallFunctionArguments = boost::json::object;

struct ToolCallFunction {
    std::optional<int>        index;
    QString                   name;
    ToolCallFunctionArguments arguments;
};
BOOST_DESCRIBE_STRUCT(ToolCallFunction, (), (index, name, arguments))

struct ToolCall {
    ToolCallFunction function;
};
BOOST_DESCRIBE_STRUCT(ToolCall, (), (function))

/// Message is a single message in a chat sequence. The message contains the
/// role ("system", "user", or "assistant"), the content and an optional list
/// of images.
struct Message {
    QString                               role;
    QString                               content;
    std::optional<std::vector<ImageData>> images;
    std::optional<std::vector<ToolCall>>  tool_calls;
};
BOOST_DESCRIBE_STRUCT(Message, (), (role, content, images, tool_calls))

//
// request types
//

/// ShowRequest is the request passed to OllamaClient::show().
struct ShowRequest {
    QString                            model;
    std::optional<QString>             system  {};
    std::optional<bool>                verbose {};
    std::optional<boost::json::object> options {};
};
BOOST_DESCRIBE_STRUCT(ShowRequest, (), (model, system, verbose, options))

//
// response types
//

/// VersionRepsonse is the response from OllamaClient::version().
struct VersionResponse {
    QString version; /// The version of the Ollama server.
};
BOOST_DESCRIBE_STRUCT(VersionResponse, (), (version))

/// ShowResponse is the response from OllamaClient::show().
struct ShowResponse {
    std::optional<QString>              license;
    std::optional<QString>              modelfile;  /// The modelfile associated with the model.
    std::optional<QString>              parameters;
    std::optional<QString>              template_;  /// The prompt template for the model.
    std::optional<QString>              system;     /// The system prompt for the model.
    std::optional<ModelDetails>         details;
    std::optional<std::vector<Message>> messages;   /// The default messages for the model.
    std::optional<boost::json::object>  model_info;
    std::optional<boost::json::object>  projector_info;
    std::optional<Time>                 modified_at;
};

void tag_invoke(const boost::json::value_from_tag &, boost::json::value &value, const ShowResponse &resp);
ShowResponse tag_invoke(const boost::json::value_to_tag<ShowResponse> &, const boost::json::value &value);

/// ListResponse is the response from OllamaClient::list().
struct ListResponse {
    std::vector<ListModelResponse> models; /// List of available models.
};
BOOST_DESCRIBE_STRUCT(ListResponse, (), (models))


} // namespace gpt4all::backend::ollama
