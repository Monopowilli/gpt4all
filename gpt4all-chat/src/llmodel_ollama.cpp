#include "llmodel_ollama.h"

#include "main.h"
#include "mysettings.h"

#include <QCoro/QCoroAsyncGenerator>
#include <QCoro/QCoroTask>
#include <fmt/format.h>
#include <gpt4all-backend/formatters.h> // IWYU pragma: keep

#include <QJSEngine>

using namespace Qt::Literals::StringLiterals;


namespace gpt4all::ui {


void OllamaGenerationParams::parseInner(QMap<GenerationParam, QVariant> &values)
{
    tryParseValue(values, GenerationParam::NPredict, &OllamaGenerationParams::n_predict);
}

auto OllamaGenerationParams::toMap() const -> QMap<QLatin1StringView, QVariant>
{
    return {
        { "n_predict"_L1, n_predict },
    };
}

OllamaProvider::OllamaProvider()
{ QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership); }

OllamaProvider::~OllamaProvider() noexcept = default;

auto OllamaProvider::supportedGenerationParams() const -> QSet<GenerationParam>
{
    using enum GenerationParam;
    return { NPredict };
}

auto OllamaProvider::makeGenerationParams(const QMap<GenerationParam, QVariant> &values) const
    -> OllamaGenerationParams *
{ return new OllamaGenerationParams(values); }

auto OllamaProvider::status() -> QCoro::Task<ProviderStatus>
{
    auto client = makeClient();
    auto resp = co_await client.version();
    if (resp)
        co_return ProviderStatus(tr("Version: %1").arg(resp->version));
    co_return ProviderStatus(resp.error());
}

auto OllamaProvider::listModels() -> QCoro::Task<backend::DataOrRespErr<QStringList>>
{
    auto client = makeClient();
    auto resp = co_await client.list();
    if (!resp)
        co_return std::unexpected(resp.error());
    QStringList res;
    for (auto &model : resp->models)
        res << model.name;
    co_return res;
}

auto OllamaProvider::newModel(const QByteArray &modelHash) const -> std::shared_ptr<OllamaModelDescription>
{ return std::static_pointer_cast<OllamaModelDescription>(newModelImpl(modelHash)); }

auto OllamaProvider::newModelImpl(const QVariant &key) const -> std::shared_ptr<ModelDescription>
{
    if (!key.canConvert<QByteArray>())
        throw std::invalid_argument(fmt::format("expected modelHash type QByteArray, got {}", key.typeName()));
    return OllamaModelDescription::create(
        std::shared_ptr<const OllamaProvider>(shared_from_this(), this), key.toByteArray()
    );
}

auto OllamaProvider::makeClient() -> backend::OllamaClient
{
    auto *mySettings = MySettings::globalInstance();
    return backend::OllamaClient(m_baseUrl, mySettings->userAgent(), networkAccessManager());
}

/// load
OllamaProviderCustom::OllamaProviderCustom(protected_t p, ProviderStore *store, QUuid id, QString name, QUrl baseUrl)
    : ModelProvider      (p, std::move(id), std::move(name), std::move(baseUrl))
    , ModelProviderCustom(store)
{
    if (auto res = m_store->acquire(m_id); !res)
        res.error().raise();
}

/// create
OllamaProviderCustom::OllamaProviderCustom(protected_t p, ProviderStore *store, QString name, QUrl baseUrl)
    : ModelProvider      (p, QUuid::createUuid(), std::move(name), std::move(baseUrl))
    , ModelProviderCustom(store)
{
    if (auto res = m_store->acquire(m_id); !res)
        res.error().raise();
}

auto OllamaProviderCustom::asData() -> ModelProviderData
{
    return {
        .id               = m_id,
        .custom_details   = CustomProviderDetails { m_name, m_baseUrl },
        .provider_details = std::monostate(),
    };
}

OllamaModelDescription::OllamaModelDescription(protected_t, std::shared_ptr<const OllamaProvider> provider,
                                               QByteArray modelHash)
    : m_provider (std::move(provider ))
    , m_modelHash(std::move(modelHash))
    {}

auto OllamaModelDescription::newInstance(QNetworkAccessManager *nam) const -> std::unique_ptr<OllamaChatModel>
{ return std::unique_ptr<OllamaChatModel>(&dynamic_cast<OllamaChatModel &>(*newInstanceImpl(nam))); }

auto OllamaModelDescription::newInstanceImpl(QNetworkAccessManager *nam) const -> ChatLLMInstance *
{ return new OllamaChatModel({ shared_from_this(), this }, nam); }

OllamaChatModel::OllamaChatModel(std::shared_ptr<const OllamaModelDescription> description, QNetworkAccessManager *nam)
    : m_description(std::move(description))
    , m_nam        (nam                   )
    {}

auto OllamaChatModel::preload() -> QCoro::Task<>
{
    // TODO: implement
    co_return;
}

auto OllamaChatModel::generate(QStringView prompt, const GenerationParams *params,
                               /*out*/ ChatResponseMetadata &metadata)
    -> QCoro::AsyncGenerator<QString>
{
    // TODO: implement
    co_yield QStringLiteral("(TODO: response from ollama)");
}


} // namespace gpt4all::ui
