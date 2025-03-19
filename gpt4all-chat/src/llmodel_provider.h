#pragma once

#include "store_provider.h"

#include "qmlsharedptr.h" // IWYU pragma: keep
#include "utils.h" // IWYU pragma: keep

#include <QCoro/QCoroQmlTask> // IWYU pragma: keep
#include <gpt4all-backend/ollama-client.h>

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine> // IWYU pragma: keep
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList> // IWYU pragma: keep
#include <QUrl>
#include <QUuid>
#include <QtPreprocessorSupport>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

class QByteArray;
class QJSEngine;
template <typename Key, typename T> class QHash;
namespace QCoro {
    template <typename T> class Task;
}


namespace gpt4all::ui {


class ModelDescription;

namespace detail {

template <typename T>
struct is_expected_impl : std::false_type {};

template <typename T, typename E>
struct is_expected_impl<std::expected<T, E>> : std::true_type {};

template <typename T>
concept is_expected = is_expected_impl<std::remove_cvref_t<T>>::value;

} // namespace detail

/// Drop the type and error information from a QCoro::Task<DataOrRespErr<T>> so it can be used by QML.
template <typename C, typename F, typename... Args>
    requires (!detail::is_expected<typename std::invoke_result_t<F, C *, Args...>::value_type>)
QCoro::QmlTask wrapQmlTask(C *obj, F f, QString prefix, Args &&...args);

template <typename C, typename F, typename... Args>
    requires detail::is_expected<typename std::invoke_result_t<F, C *, Args...>::value_type>
QCoro::QmlTask wrapQmlTask(C *obj, F f, QString prefix, Args &&...args);

/// Drop the error information from a DataOrRespErr<T> so it can be used by QML.
template <typename C, typename F, typename... Args>
bool wrapQmlFunc(C *obj, F &&f, QStringView prefix, Args &&...args);

inline namespace llmodel_provider {

Q_NAMESPACE

enum class GenerationParam  {
    NPredict,
    Temperature,
    TopP,
    TopK,
    MinP,
    RepeatPenalty,
    RepeatLastN,
};
Q_ENUM_NS(GenerationParam)

} // inline namespace llmodel_provider

class GenerationParams {
public:
    virtual ~GenerationParams() noexcept = 0;

    virtual QMap<QLatin1StringView, QVariant> toMap() const = 0;
    virtual bool isNoop() const = 0;

protected:
    void parse(QMap<GenerationParam, QVariant> values);
    virtual void parseInner(QMap<GenerationParam, QVariant> &values) = 0;

    static QVariant tryParseValue(QMap<GenerationParam, QVariant> &values, GenerationParam key, const QMetaType &type);

    template <typename T, typename S, typename C>
    void tryParseValue(this S &self, QMap<GenerationParam, QVariant> &values, GenerationParam key, T C::* dest);
};

class ProviderStatus {
    Q_GADGET
    Q_PROPERTY(bool    ok     READ ok     CONSTANT)
    Q_PROPERTY(QString detail READ detail CONSTANT)

public:
    explicit ProviderStatus(QString okMsg): m_ok(true), m_detail(std::move(okMsg)) {}
    explicit ProviderStatus(const backend::ResponseError &error);

    bool           ok    () const { return m_ok;     }
    const QString &detail() const { return m_detail; }

private:
    bool    m_ok;
    QString m_detail;
};

class ModelProvider : public std::enable_shared_from_this<ModelProvider> {
protected:
    struct protected_t { explicit protected_t() = default; };

    explicit ModelProvider(protected_t, QUuid id, QString name, QUrl baseUrl);

public:
    virtual ~ModelProvider() noexcept = 0;

    virtual       QObject *asQObject() = 0;
    virtual const QObject *asQObject() const = 0;

    virtual bool         isBuiltin() const = 0;
    virtual ProviderType type     () const = 0;

    // getters
    [[nodiscard]] const QUuid   &id     () const { return m_id;      }
    [[nodiscard]] const QString &name   () const { return m_name;    }
    [[nodiscard]] const QUrl    &baseUrl() const { return m_baseUrl; }

    virtual auto supportedGenerationParams() const -> QSet<GenerationParam> = 0;
    virtual auto makeGenerationParams(const QMap<GenerationParam, QVariant> &values) const -> GenerationParams * = 0;

    // endpoints
    virtual auto status    () -> QCoro::Task<ProviderStatus                     > = 0;
    virtual auto listModels() -> QCoro::Task<backend::DataOrRespErr<QStringList>> = 0;

    // QML endpoints
    QCoro::QmlTask statusQml()
    { return wrapQmlTask(this, &ModelProvider::status,     QStringLiteral("ModelProvider::status")    ); }
    QCoro::QmlTask listModelsQml()
    { return wrapQmlTask(this, &ModelProvider::listModels, QStringLiteral("ModelProvider::listModels")); }

    /// create a model using this provider
    [[nodiscard]] auto newModel(const QVariant &key) const -> std::shared_ptr<ModelDescription>;

    friend bool operator==(const ModelProvider &a, const ModelProvider &b)
    { return a.m_id == b.m_id; }

protected:
    [[nodiscard]] virtual auto newModelImpl(const QVariant &key) const -> std::shared_ptr<ModelDescription> = 0;

    QUuid   m_id;
    QString m_name;
    QUrl    m_baseUrl;

    template <typename T> friend struct Creatable;
};

class ModelProviderBuiltin : public virtual ModelProvider {
protected:
    explicit ModelProviderBuiltin(QUrl icon)
        : m_icon(std::move(icon)) {}

public:
    bool isBuiltin() const final { return true; }

    [[nodiscard]] const QUrl &icon() const { return m_icon; }

protected:
    QUrl m_icon;
};

// Mixin with no public interface providing basic load/save
class ModelProviderMutable : public virtual ModelProvider {
protected:
    explicit ModelProviderMutable(ProviderStore *store)
        : m_store(store) {}

public:
    ~ModelProviderMutable() noexcept override;

protected:
    virtual auto asData() -> ModelProviderData = 0;

    template <typename T, typename S, typename C>
    [[nodiscard]] DataStoreResult<> setMemberProp(this S &self, T C::* member, std::string_view name, T value,
                                                  bool create = false);

    [[nodiscard]] virtual bool persisted() const { return true; }

    ProviderStore *m_store;
};

class ModelProviderCustom : public ModelProviderMutable {
protected:
    explicit ModelProviderCustom(ProviderStore *store)
        : ModelProviderMutable(store) {}

public:
    bool isBuiltin() const final { return false; }

    // setters
    [[nodiscard]] DataStoreResult<> setName   (QString value);
    [[nodiscard]] DataStoreResult<> setBaseUrl(QUrl    value)
    { return setMemberProp<QUrl   >(&ModelProviderCustom::m_baseUrl, "baseUrl", std::move(value)); }

    // QML setters
    bool setNameQml   (QString value)
    { return wrapQmlFunc(this, &ModelProviderCustom::setName,    u"setName",    std::move(value)); }
    bool setBaseUrlQml(QString value)
    { return wrapQmlFunc(this, &ModelProviderCustom::setBaseUrl, u"setBaseUrl", std::move(value)); }

    [[nodiscard]] auto persist() -> DataStoreResult<>;

protected:
    [[nodiscard]] bool persisted() const override { return m_persisted; }

    bool m_persisted = false;
};

class ProviderRegistry : public QObject {
    Q_OBJECT

private:
    struct PathSet { std::filesystem::path builtin, custom; };

    struct BuiltinProviderData {
        QUuid                    id;
        QString                  name;
        QUrl                     icon;
        QUrl                     base_url;
        std::span<const QString> model_whitelist;
    };

protected:
    explicit ProviderRegistry(PathSet paths);
    explicit ProviderRegistry(): ProviderRegistry(getSubdirs()) {}

public:
    static ProviderRegistry *globalInstance();

    [[nodiscard]] auto add(std::shared_ptr<ModelProviderCustom> provider) -> DataStoreResult<>;
    Q_INVOKABLE bool addQml(QmlSharedPtr *provider);

    // TODO(jared): implement a way to remove custom providers via the model

    auto operator[](const QUuid &id) -> const ModelProvider * { return m_providers.at(id).get(); }
    [[nodiscard]] size_t providerCount() const { return m_providers.size(); }
    [[nodiscard]] auto   providerAt(size_t i) const -> const ModelProvider *;

    ProviderStore *customStore() { return &m_customStore; }

Q_SIGNALS:
    void customProviderAdded(size_t index);
    void customProviderRemoved(size_t index); // TODO: use
    void customProviderChanged(size_t index);
    void aboutToBeCleared();

private:
    void load();
    static PathSet getSubdirs();

private Q_SLOTS:
    void onModelPathChanged();
    void onProviderChanged();

private:
    static constexpr size_t N_BUILTIN = 3;
    static const std::array<BuiltinProviderData, N_BUILTIN> s_builtinProviders;

    ProviderStore                                             m_customStore;
    ProviderStore                                             m_builtinStore;
    std::unordered_map<QUuid, std::shared_ptr<ModelProvider>> m_providers;
    std::vector<const QUuid *>                                m_providerList;
};

class ProviderList : public QAbstractListModel {
    Q_OBJECT

public:
    explicit ProviderList();

    static ProviderList *create(QQmlEngine *, QJSEngine *) { return new ProviderList(); }

    auto roleNames() const -> QHash<int, QByteArray> override;
    int rowCount(const QModelIndex &parent = {}) const override
    { Q_UNUSED(parent) return int(m_size); }
    QVariant data(const QModelIndex &index, int role) const override;

private Q_SLOTS:
    void onCustomProviderAdded  (size_t index);
    void onCustomProviderRemoved(size_t index);
    void onCustomProviderChanged(size_t index);
    void onAboutToBeCleared();

private:
    size_t m_size;
};

class ProviderListSort : public QSortFilterProxyModel {
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT

private:
    explicit ProviderListSort() { setSourceModel(&m_model); sort(0); }

public:
    static ProviderListSort *create(QQmlEngine *, QJSEngine *) { return new ProviderListSort(); }

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    ProviderList m_model;
};


} // namespace gpt4all::ui


#include "llmodel_provider.inl" // IWYU pragma: export
