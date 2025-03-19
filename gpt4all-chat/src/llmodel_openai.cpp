#include "llmodel_openai.h"

#include "main.h"
#include "mysettings.h"
#include "utils.h"

#include <QCoro/QCoroAsyncGenerator> // IWYU pragma: keep
#include <QCoro/QCoroNetworkReply> // IWYU pragma: keep
#include <QCoro/QCoroTask> // IWYU pragma: keep
#include <boost/json.hpp> // IWYU pragma: keep
#include <fmt/format.h>
#include <gpt4all-backend/formatters.h> // IWYU pragma: keep
#include <gpt4all-backend/rest.h>

#include <QAnyStringView>
#include <QByteArray>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QMetaEnum>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRestAccessManager>
#include <QRestReply>
#include <QSet>
#include <QStringView>
#include <QUtf8StringView> // IWYU pragma: keep
#include <QVariant>
#include <QXmlStreamReader>
#include <QtAssert>

#include <coroutine>
#include <expected>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace json = boost::json;
using namespace Qt::Literals::StringLiterals;

//#define DEBUG


static auto processRespLine(const QByteArray &line) -> std::optional<QString>
{
    auto jsonData = line.trimmed();
    if (jsonData.startsWith("data:"_ba))
        jsonData.remove(0, 5);
    jsonData = jsonData.trimmed();
    if (jsonData.isEmpty())
        return std::nullopt;
    if (jsonData == "[DONE]")
        return std::nullopt;

    QJsonParseError err;
    auto document = QJsonDocument::fromJson(jsonData, &err);
    if (document.isNull())
        throw std::runtime_error(fmt::format("OpenAI chat response parsing failed: {}", err.errorString()));

    auto root = document.object();
    auto choices = root.value("choices").toArray();
    auto choice = choices.first().toObject();
    auto delta = choice.value("delta").toObject();
    return delta.value("content").toString();
}


namespace gpt4all::ui {


void OpenaiGenerationParams::parseInner(QMap<GenerationParam, QVariant> &values)
{
    tryParseValue(values, GenerationParam::NPredict,    &OpenaiGenerationParams::n_predict  );
    tryParseValue(values, GenerationParam::Temperature, &OpenaiGenerationParams::temperature);
    tryParseValue(values, GenerationParam::TopP,        &OpenaiGenerationParams::top_p      );
}

auto OpenaiGenerationParams::toMap() const -> QMap<QLatin1StringView, QVariant>
{
    return {
        {  "max_completion_tokens"_L1,  n_predict   },
        {  "temperature"_L1,            temperature },
        {  "top_p"_L1,                  top_p       },
    };
}

OpenaiProvider::OpenaiProvider()
{ QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership); }

OpenaiProvider::OpenaiProvider(QString apiKey)
    : m_apiKey(std::move(apiKey))
{ QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership); }

OpenaiProvider::~OpenaiProvider() noexcept = default;

auto OpenaiProvider::supportedGenerationParams() const -> QSet<GenerationParam>
{
    using enum GenerationParam;
    return { NPredict, Temperature, TopP };
}

auto OpenaiProvider::makeGenerationParams(const QMap<GenerationParam, QVariant> &values) const
    -> OpenaiGenerationParams *
{ return new OpenaiGenerationParams(values); }

auto OpenaiProvider::status() -> QCoro::Task<ProviderStatus>
{
    auto resp = co_await listModels();
    if (resp)
        co_return ProviderStatus(tr("OK"));
    co_return ProviderStatus(resp.error());
}

auto OpenaiProvider::listModels() -> QCoro::Task<backend::DataOrRespErr<QStringList>>
{
    auto *mySettings = MySettings::globalInstance();
    auto *nam = networkAccessManager();

    QNetworkRequest request(m_baseUrl.resolved(u"models"_s));
    request.setHeader   (QNetworkRequest::UserAgentHeader, mySettings->userAgent());
    request.setRawHeader("authorization"_ba, fmt::format("Bearer {}", m_apiKey).c_str());

    std::unique_ptr<QNetworkReply> reply(nam->get(request));
    QRestReply restReply(reply.get());

    if (reply->error())
        co_return std::unexpected(&restReply);

    QStringList models;
    try {
        json::stream_parser parser;
        auto coroReply = qCoro(*reply);
        for (;;) {
            auto chunk = co_await coroReply.readAll();
            if (!restReply.isSuccess())
                co_return std::unexpected(&restReply);
            if (chunk.isEmpty()) {
                Q_ASSERT(reply->atEnd());
                break;
            }
            parser.write(chunk.data(), chunk.size());
        }
        parser.finish();
        auto resp = parser.release().as_object();
        for (auto &entry : resp.at("data").as_array())
            models << json::value_to<QString>(entry.at("id"));
    } catch (const boost::system::system_error &e) {
        co_return std::unexpected(e);
    }
    co_return models;
}

auto OpenaiProvider::newModel(const QString &modelName) const -> std::shared_ptr<OpenaiModelDescription>
{ return std::static_pointer_cast<OpenaiModelDescription>(newModelImpl(modelName)); }

auto OpenaiProvider::newModelImpl(const QVariant &key) const -> std::shared_ptr<ModelDescription>
{
    if (!key.canConvert<QString>())
        throw std::invalid_argument(fmt::format("expected modelName type QString, got {}", key.typeName()));
    return OpenaiModelDescription::create(
        std::shared_ptr<const OpenaiProvider>(shared_from_this(), this), key.toString()
    );
}

OpenaiProviderBuiltin::OpenaiProviderBuiltin(protected_t p, ProviderStore *store, QUuid id, QString name, QUrl icon,
                                             QUrl baseUrl, std::unordered_set<QString> modelWhitelist)
    : ModelProvider(p, std::move(id), std::move(name), std::move(baseUrl))
    , ModelProviderBuiltin(std::move(icon))
    , ModelProviderMutable(store)
    , m_modelWhitelist(std::move(modelWhitelist))
{
    auto res = m_store->acquire(m_id);
    if (!res)
        res.error().raise();
    if (auto maybeData = *res) {
        auto &details = std::get<size_t(ProviderType::openai)>((*maybeData)->provider_details);
        m_apiKey = details.api_key;
    }
}

auto OpenaiProviderBuiltin::listModels() -> QCoro::Task<backend::DataOrRespErr<QStringList>>
{
    auto models = co_await OpenaiProvider::listModels();
    if (!models)
        co_return std::unexpected(models.error());
    models->removeIf([&](auto &m) { return !m_modelWhitelist.contains(m); });
    co_return *models; 
}

auto OpenaiProviderBuiltin::asData() -> ModelProviderData
{
    return {
        .id               = m_id,
        .custom_details   = {},
        .provider_details = OpenaiProviderDetails { m_apiKey },
    };
}

/// load
OpenaiProviderCustom::OpenaiProviderCustom(protected_t p, ProviderStore *store, QUuid id, QString name, QUrl baseUrl,
                                           QString apiKey)
    : ModelProvider(p, std::move(id), std::move(name), std::move(baseUrl))
    , OpenaiProvider(std::move(apiKey))
    , ModelProviderCustom(store)
{
    if (auto res = m_store->acquire(m_id); !res)
        res.error().raise();
}

/// create
OpenaiProviderCustom::OpenaiProviderCustom(protected_t p, ProviderStore *store, QString name, QUrl baseUrl,
                                           QString apiKey)
    : ModelProvider(p, QUuid::createUuid(), std::move(name), std::move(baseUrl))
    , ModelProviderCustom(std::move(store))
    , OpenaiProvider(std::move(apiKey))
{
    if (auto res = m_store->acquire(m_id); !res)
        res.error().raise();
}

auto OpenaiProviderCustom::asData() -> ModelProviderData
{
    return {
        .id               = m_id,
        .custom_details   = CustomProviderDetails { m_name, m_baseUrl },
        .provider_details = OpenaiProviderDetails { m_apiKey          },
    };
}

OpenaiModelDescription::OpenaiModelDescription(protected_t, std::shared_ptr<const OpenaiProvider> provider,
                                               QString modelName)
    : m_provider (std::move(provider ))
    , m_modelName(std::move(modelName))
    {}

auto OpenaiModelDescription::newInstance(QNetworkAccessManager *nam) const -> std::unique_ptr<OpenaiChatModel>
{ return std::unique_ptr<OpenaiChatModel>(&dynamic_cast<OpenaiChatModel &>(*newInstanceImpl(nam))); }

auto OpenaiModelDescription::newInstanceImpl(QNetworkAccessManager *nam) const -> ChatLLMInstance *
{ return new OpenaiChatModel({ shared_from_this(), this }, nam); }

OpenaiChatModel::OpenaiChatModel(std::shared_ptr<const OpenaiModelDescription> description, QNetworkAccessManager *nam)
    : m_description(std::move(description))
    , m_nam(nam)
    {}

static auto parsePrompt(QXmlStreamReader &xml) -> std::expected<QJsonArray, QString>
{
    QJsonArray messages;

    auto xmlError = [&xml] {
        return std::unexpected(u"%1:%2: %3"_s.arg(xml.lineNumber()).arg(xml.columnNumber()).arg(xml.errorString()));
    };

    if (xml.hasError())
        return xmlError();
    if (xml.atEnd())
        return messages;

    // skip header
    bool foundElement = false;
    do {
        switch (xml.readNext()) {
            using enum QXmlStreamReader::TokenType;
        case Invalid:
            return xmlError();
        case EndDocument:
            return messages;
        default:
            foundElement = true;
        case StartDocument:
        case Comment:
        case DTD:
        case ProcessingInstruction:
            ;
        }
    } while (!foundElement);

    // document body loop
    bool foundRoot = false;
    for (;;) {
        switch (xml.tokenType()) {
            using enum QXmlStreamReader::TokenType;
        case StartElement:
            {
                auto name = xml.name();
                if (!foundRoot) {
                    if (name != "chat"_L1)
                        return std::unexpected(u"unexpected tag: %1"_s.arg(name));
                    foundRoot = true;
                } else {
                    if (name != "user"_L1 && name != "assistant"_L1 && name != "system"_L1)
                        return std::unexpected(u"unknown role: %1"_s.arg(name));
                    auto content = xml.readElementText();
                    if (xml.tokenType() != EndElement)
                        return xmlError();
                    messages << makeJsonObject({
                        { "role"_L1,    name.toString().trimmed() },
                        { "content"_L1, content                   },
                    });
                }
                break;
            }
        case Characters:
            if (!xml.isWhitespace())
                return std::unexpected(u"unexpected text: %1"_s.arg(xml.text()));
        case Comment:
        case ProcessingInstruction:
        case EndElement:
            break;
        case EndDocument:
            return messages;
        case Invalid:
            return xmlError();
        default:
            return std::unexpected(u"unexpected token: %1"_s.arg(xml.tokenString()));
        }
        xml.readNext();
    }
}

auto OpenaiChatModel::preload() -> QCoro::Task<>
{ co_return; /* not supported -> no-op */ }

auto OpenaiChatModel::generate(QStringView prompt, const GenerationParams *params,
                               /*out*/ ChatResponseMetadata &metadata) -> QCoro::AsyncGenerator<QString>
{
    auto *mySettings = MySettings::globalInstance();

    if (params->isNoop())
        co_return; // nothing requested

    auto reqBody = makeJsonObject({
        { "model"_L1,  m_description->modelName() },
        { "stream"_L1, true                       },
    });
    extend(reqBody, params->toMap());

    // conversation history
    {
        QXmlStreamReader xml(prompt);
        auto messages = parsePrompt(xml);
        if (!messages)
            throw std::invalid_argument(fmt::format("Failed to parse OpenAI prompt: {}", messages.error()));
        reqBody.insert("messages"_L1, *messages);
    }

    auto &provider = *m_description->provider();
    QNetworkRequest request(provider.baseUrl().resolved(QUrl("/v1/chat/completions")));
    request.setHeader   (QNetworkRequest::UserAgentHeader,   mySettings->userAgent());
    request.setHeader   (QNetworkRequest::ContentTypeHeader, "application/json"_ba  );
    request.setRawHeader("authorization"_ba, fmt::format("Bearer {}", provider.apiKey()).c_str());

    QRestAccessManager restNam(m_nam);
    std::unique_ptr<QNetworkReply> reply(restNam.post(request, QJsonDocument(reqBody)));

    auto makeError = [](const QRestReply &reply) {
        return std::runtime_error(fmt::format("OpenAI chat request failed: {}", backend::restErrorString(reply)));
    };

    QRestReply restReply(reply.get());
    if (reply->error())
        throw makeError(restReply);

    auto coroReply = qCoro(reply.get());
    for (;;) {
        auto line = co_await coroReply.readLine();
        if (!restReply.isSuccess())
            throw makeError(restReply);
        if (line.isEmpty()) {
            Q_ASSERT(reply->atEnd());
            break;
        }
        if (auto chunk = processRespLine(line))
            co_yield *chunk;
    }
}


} // namespace gpt4all::ui
