#include "config.h"

#include "pretty.h"

#include <QCoro/QCoroTask> // IWYU pragma: keep
#include <boost/json.hpp>
#include <fmt/base.h>
#include <gpt4all-backend/formatters.h> // IWYU pragma: keep
#include <gpt4all-backend/ollama-client.h>

#include <QCoreApplication>
#include <QTimer>
#include <QString>
#include <QUrl>

#include <coroutine>
#include <expected>
#include <variant>

namespace json = boost::json;
using namespace Qt::Literals::StringLiterals;
using gpt4all::backend::OllamaClient;


template <typename T>
static std::string to_json(const T &value)
{ return pretty_print(json::value_from(value)); }

static void run()
{
    fmt::print("Connecting to server at {}\n", OLLAMA_URL);
    OllamaClient provider(OLLAMA_URL);

    auto versionResp = QCoro::waitFor(provider.version());
    if (versionResp) {
        fmt::print("Version response: {}\n", to_json(*versionResp));
    } else {
        fmt::print("Error retrieving version: {}\n", versionResp.error().errorString);
        return QCoreApplication::exit(1);
    }

    auto modelsResponse = QCoro::waitFor(provider.list());
    if (modelsResponse) {
        fmt::print("Available models:\n");
        for (const auto & model : modelsResponse->models)
            fmt::print("{}\n", model.model);
        if (!modelsResponse->models.empty())
            fmt::print("First model: {}\n", to_json(modelsResponse->models.front()));
    } else {
        fmt::print("Error retrieving available models: {}\n", modelsResponse.error().errorString);
        return QCoreApplication::exit(1);
    }

    auto showResponse = QCoro::waitFor(provider.show({ .model = "DeepSeek-R1-Distill-Llama-70B-Q4_K_S" }));
    if (showResponse) {
        fmt::print("Show response: {}\n", to_json(*showResponse));
    } else {
        fmt::print("Error retrieving model info: {}\n", showResponse.error().errorString);
        return QCoreApplication::exit(1);
    }

    QCoreApplication::exit(0);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTimer::singleShot(0, &run);
    return app.exec();
}
