#pragma once

#include "creatable.h"
#include "llmodel_chat.h"
#include "llmodel_description.h"
#include "llmodel_provider.h"

#include <QCoro/QCoroQmlTask> // IWYU pragma: keep
#include <gpt4all-backend/ollama-client.h>

#include <QLatin1StringView> // IWYU pragma: keep
#include <QObject> // IWYU pragma: keep
#include <QString>
#include <QStringList> // IWYU pragma: keep
#include <QUrl>
#include <QVariant>
#include <QtTypes> // IWYU pragma: keep

#include <memory>
#include <unordered_set>
#include <utility>

class QNetworkAccessManager;
template <typename Key, typename T> class QMap;
template <typename T> class QSet;
namespace QCoro { template <typename T> class Task; }


namespace gpt4all::ui {


class OpenaiChatModel;
class OpenaiModelDescription;

struct OpenaiGenerationParamsData {
    uint  n_predict;
    float temperature;
    float top_p;
};

class OpenaiGenerationParams : public GenerationParams, public OpenaiGenerationParamsData {
public:
    explicit OpenaiGenerationParams(QMap<GenerationParam, QVariant> values) { parse(std::move(values)); }
    auto toMap() const -> QMap<QLatin1StringView, QVariant> override;
    bool isNoop() const override { return !n_predict; }

protected:
    void parseInner(QMap<GenerationParam, QVariant> &values) override;
};

class OpenaiProvider : public QObject, public virtual ModelProvider {
    Q_OBJECT
    Q_PROPERTY(QUuid        id        READ id        CONSTANT            )
    Q_PROPERTY(QString      apiKey    READ apiKey    NOTIFY apiKeyChanged)
    Q_PROPERTY(bool         isBuiltin READ isBuiltin CONSTANT            )
    Q_PROPERTY(ProviderType type      READ type      CONSTANT            )

protected:
    explicit OpenaiProvider();
    explicit OpenaiProvider(QString apiKey);

public:
    ~OpenaiProvider() noexcept override = 0;

          QObject *asQObject()       override { return this; }
    const QObject *asQObject() const override { return this; }

    ProviderType type() const final { return ProviderType::openai; }

    [[nodiscard]] const QString &apiKey() const { return m_apiKey; }

    [[nodiscard]] virtual DataStoreResult<> setApiKey(QString value) = 0;
    Q_INVOKABLE bool setApiKeyQml(QString value)
    { return wrapQmlFunc(this, &OpenaiProvider::setApiKey, u"setApiKey", std::move(value)); }

    auto supportedGenerationParams() const -> QSet<GenerationParam> override;
    auto makeGenerationParams(const QMap<GenerationParam, QVariant> &values) const -> OpenaiGenerationParams * override;

    // endpoints
    auto status    () -> QCoro::Task<ProviderStatus                     > override;
    auto listModels() -> QCoro::Task<backend::DataOrRespErr<QStringList>> override;

    // QML endpoints
    Q_INVOKABLE QCoro::QmlTask statusQml    () { return ModelProvider::statusQml    (); }
    Q_INVOKABLE QCoro::QmlTask listModelsQml() { return ModelProvider::listModelsQml(); }

    [[nodiscard]] auto newModel(const QString &modelName) const -> std::shared_ptr<OpenaiModelDescription>;

Q_SIGNALS:
    void apiKeyChanged(const QString &value);

protected:
    [[nodiscard]] auto newModelImpl(const QVariant &key) const -> std::shared_ptr<ModelDescription> final;

    QString m_apiKey;
};

class OpenaiProviderBuiltin
    : public OpenaiProvider
    , public ModelProviderBuiltin
    , public ModelProviderMutable
    , public Creatable<OpenaiProviderBuiltin>
{
    Q_OBJECT
    Q_PROPERTY(QString     name           READ name           CONSTANT)
    Q_PROPERTY(QUrl        icon           READ icon           CONSTANT)
    Q_PROPERTY(QUrl        baseUrl        READ baseUrl        CONSTANT)

public:
    /// Create a new built-in OpenAI provider, loading its API key from disk if known.
    explicit OpenaiProviderBuiltin(protected_t p, ProviderStore *store, QUuid id, QString name, QUrl icon, QUrl baseUrl,
                                   std::unordered_set<QString> modelWhitelist);

    [[nodiscard]] DataStoreResult<> setApiKey(QString value) override
    { return setMemberProp<QString>(&OpenaiProviderBuiltin::m_apiKey, "apiKey", std::move(value), /*create*/ true); }

    // override for model whitelist
    auto listModels() -> QCoro::Task<backend::DataOrRespErr<QStringList>> override;

Q_SIGNALS:
    void apiKeyChanged(const QString &value);

protected:
    auto asData() -> ModelProviderData override;

    std::unordered_set<QString> m_modelWhitelist;
};

class OpenaiProviderCustom final
    : public OpenaiProvider, public ModelProviderCustom, public Creatable<OpenaiProviderCustom>
{
    Q_OBJECT
    Q_PROPERTY(QString name    READ name    NOTIFY nameChanged   )
    Q_PROPERTY(QUrl    baseUrl READ baseUrl NOTIFY baseUrlChanged)

public:
    /// Load an existing OpenaiProvider from disk.
    explicit OpenaiProviderCustom(protected_t p, ProviderStore *store, QUuid id, QString name, QUrl baseUrl, QString apiKey);

    /// Create a new OpenaiProvider on disk.
    explicit OpenaiProviderCustom(protected_t p, ProviderStore *store, QString name, QUrl baseUrl, QString apiKey);

    [[nodiscard]] DataStoreResult<> setApiKey(QString value) override
    { return setMemberProp<QString>(&OpenaiProviderCustom::m_apiKey, "apiKey", std::move(value)); }

    // QML setters
    Q_INVOKABLE bool setNameQml   (QString value) { return ModelProviderCustom::setNameQml   (std::move(value)); }
    Q_INVOKABLE bool setBaseUrlQml(QString value) { return ModelProviderCustom::setBaseUrlQml(std::move(value)); }

Q_SIGNALS:
    void nameChanged   (const QString &value);
    void baseUrlChanged(const QUrl    &value);
    void apiKeyChanged (const QString &value);

protected:
    auto asData() -> ModelProviderData override;
};

class OpenaiModelDescription : public ModelDescription, public Creatable<OpenaiModelDescription> {
    Q_GADGET
    Q_PROPERTY(QString modelName READ modelName CONSTANT)

public:
    explicit OpenaiModelDescription(protected_t, std::shared_ptr<const OpenaiProvider> provider, QString modelName);

    // getters
    [[nodiscard]] auto           provider () const -> const OpenaiProvider * override { return m_provider.get(); }
    [[nodiscard]] QVariant       key      () const                           override { return m_modelName;      }
    [[nodiscard]] const QString &modelName() const                                    { return m_modelName;      }

    [[nodiscard]] auto newInstance(QNetworkAccessManager *nam) const -> std::unique_ptr<OpenaiChatModel>;

protected:
    [[nodiscard]] auto newInstanceImpl(QNetworkAccessManager *nam) const -> ChatLLMInstance * override;

private:
    std::shared_ptr<const OpenaiProvider> m_provider;
    QString                               m_modelName;
};

class OpenaiChatModel : public ChatLLMInstance {
public:
    explicit OpenaiChatModel(std::shared_ptr<const OpenaiModelDescription> description, QNetworkAccessManager *nam);

    auto description() const -> const OpenaiModelDescription * override
    { return m_description.get(); }

    auto preload() -> QCoro::Task<void> override;

    auto generate(QStringView prompt, const GenerationParams *params, /*out*/ ChatResponseMetadata &metadata)
        -> QCoro::AsyncGenerator<QString> override;

private:
    std::shared_ptr<const OpenaiModelDescription>  m_description;
    QNetworkAccessManager                         *m_nam;
};


} // namespace gpt4all::ui
