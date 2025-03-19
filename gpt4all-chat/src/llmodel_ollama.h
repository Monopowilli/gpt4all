#pragma once

#include "creatable.h"
#include "llmodel_chat.h"
#include "llmodel_description.h"
#include "llmodel_provider.h"

#include <QCoro/QCoroQmlTask> // IWYU pragma: keep
#include <gpt4all-backend/ollama-client.h>

#include <QByteArray>
#include <QLatin1StringView> // IWYU pragma: keep
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtTypes> // IWYU pragma: keep

#include <utility>

class QNetworkAccessManager;
template <typename Key, typename T> class QMap;
template <typename T> class QSet;


namespace gpt4all::ui {


class OllamaChatModel;
class OllamaModelDescription;

struct OllamaGenerationParamsData {
    uint n_predict;
    // TODO(jared): include ollama-specific generation params
};

class OllamaGenerationParams : public GenerationParams, public OllamaGenerationParamsData {
public:
    explicit OllamaGenerationParams(QMap<GenerationParam, QVariant> values) { parse(std::move(values)); }
    auto toMap() const -> QMap<QLatin1StringView, QVariant> override;
    bool isNoop() const override { return !n_predict; }

protected:
    void parseInner(QMap<GenerationParam, QVariant> &values) override;
};

class OllamaProvider : public QObject, public virtual ModelProvider {
    Q_OBJECT
    Q_PROPERTY(QUuid        id        READ id        CONSTANT)
    Q_PROPERTY(bool         isBuiltin READ isBuiltin CONSTANT)
    Q_PROPERTY(ProviderType type      READ type      CONSTANT)

protected:
    explicit OllamaProvider();

public:
    ~OllamaProvider() noexcept override = 0;

          QObject *asQObject()       override { return this; }
    const QObject *asQObject() const override { return this; }

    ProviderType type() const final { return ProviderType::ollama; }

    auto supportedGenerationParams() const -> QSet<GenerationParam> override;
    auto makeGenerationParams(const QMap<GenerationParam, QVariant> &values) const -> OllamaGenerationParams * override;

    // endpoints
    auto status    () -> QCoro::Task<ProviderStatus                     > override;
    auto listModels() -> QCoro::Task<backend::DataOrRespErr<QStringList>> override;

    // QML endpoints
    Q_INVOKABLE QCoro::QmlTask statusQml    () { return ModelProvider::statusQml    (); }
    Q_INVOKABLE QCoro::QmlTask listModelsQml() { return ModelProvider::listModelsQml(); }

    [[nodiscard]] auto newModel(const QByteArray &modelHash) const -> std::shared_ptr<OllamaModelDescription>;

protected:
    [[nodiscard]] auto newModelImpl(const QVariant &key) const -> std::shared_ptr<ModelDescription> final;

private:
    backend::OllamaClient makeClient();
};

class OllamaProviderBuiltin : public OllamaProvider, public Creatable<OllamaProviderBuiltin> {
    Q_OBJECT
    Q_PROPERTY(QString name    READ name    CONSTANT)
    Q_PROPERTY(QUrl    baseUrl READ baseUrl CONSTANT)

public:
    /// Create a new built-in Ollama provider (transient).
    explicit OllamaProviderBuiltin(protected_t p, QUuid id, QString name, QUrl baseUrl)
        : ModelProvider(p, std::move(id), std::move(name), std::move(baseUrl)) {}
};

class OllamaProviderCustom final
    : public OllamaProvider, public ModelProviderCustom, public Creatable<OllamaProviderCustom>
{
    Q_OBJECT
    Q_PROPERTY(QString name    READ name    NOTIFY nameChanged   )
    Q_PROPERTY(QUrl    baseUrl READ baseUrl NOTIFY baseUrlChanged)

public:
    /// Load an existing OllamaProvider from disk.
    explicit OllamaProviderCustom(protected_t p, ProviderStore *store, QUuid id, QString name, QUrl baseUrl);

    /// Create a new OllamaProvider on disk.
    explicit OllamaProviderCustom(protected_t p, ProviderStore *store, QString name, QUrl baseUrl);

    // QML setters
    Q_INVOKABLE bool setNameQml   (QString value) { return ModelProviderCustom::setNameQml   (std::move(value)); }
    Q_INVOKABLE bool setBaseUrlQml(QString value) { return ModelProviderCustom::setBaseUrlQml(std::move(value)); }

Q_SIGNALS:
    void nameChanged   (const QString &value);
    void baseUrlChanged(const QUrl    &value);

protected:
    auto asData() -> ModelProviderData override;
};

class OllamaModelDescription : public ModelDescription, public Creatable<OllamaModelDescription> {
    Q_GADGET
    Q_PROPERTY(QByteArray modelHash READ modelHash CONSTANT)

public:
    explicit OllamaModelDescription(protected_t, std::shared_ptr<const OllamaProvider> provider, QByteArray modelHash);

    // getters
    [[nodiscard]] auto              provider () const -> const OllamaProvider * override { return m_provider.get(); }
    [[nodiscard]] QVariant          key      () const                           override { return m_modelHash;      }
    [[nodiscard]] const QByteArray &modelHash() const                                    { return m_modelHash;      }

    [[nodiscard]] auto newInstance(QNetworkAccessManager *nam) const -> std::unique_ptr<OllamaChatModel>;

protected:
    [[nodiscard]] auto newInstanceImpl(QNetworkAccessManager *nam) const -> ChatLLMInstance * override;

private:
    std::shared_ptr<const OllamaProvider> m_provider;
    QByteArray                            m_modelHash;
};

class OllamaChatModel : public ChatLLMInstance {
public:
    explicit OllamaChatModel(std::shared_ptr<const OllamaModelDescription> description, QNetworkAccessManager *nam);

    auto description() const -> const OllamaModelDescription * override
    { return m_description.get(); }

    auto preload() -> QCoro::Task<void> override;

    auto generate(QStringView prompt, const GenerationParams *params, /*out*/ ChatResponseMetadata &metadata)
        -> QCoro::AsyncGenerator<QString> override;

private:
    std::shared_ptr<const OllamaModelDescription> m_description;
    // TODO: implement generate using Ollama backend
    QNetworkAccessManager                         *m_nam;
};


} // namespace gpt4all::ui
