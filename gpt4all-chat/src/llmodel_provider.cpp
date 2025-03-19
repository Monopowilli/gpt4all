#include "llmodel_provider.h"

#include "llmodel_ollama.h"
#include "llmodel_openai.h"

#include "mysettings.h"

#include <fmt/format.h>
#include <gpt4all-backend/formatters.h> // IWYU pragma: keep

#include <QtAssert>
#include <QModelIndex> // IWYU pragma: keep
#include <QVariant>

#include <algorithm>

namespace ranges = std::ranges;


namespace gpt4all::ui {


GenerationParams::~GenerationParams() noexcept = default;

void GenerationParams::parse(QMap<GenerationParam, QVariant> values)
{
    parseInner(values);
    if (!values.isEmpty()) {
        auto gparamsMeta = QMetaEnum::fromType<GenerationParam>();
        throw std::invalid_argument(fmt::format(
            " unsupported param: {}", gparamsMeta.valueToKey(int(values.keys().constFirst()))
        ));
    }
}

QVariant GenerationParams::tryParseValue(QMap<GenerationParam, QVariant> &values, GenerationParam key,
                                         const QMetaType &type)
{
    auto value = values.take(key);
    if (value.isValid() && !value.canConvert(type)) {
        auto gparamsMeta = QMetaEnum::fromType<GenerationParam>();
        throw std::invalid_argument(fmt::format(
            "expected {} of type {}, got {}", gparamsMeta.valueToKey(int(key)), type.name(), value.typeName()
        ));
    }
    return value;
}

ProviderStatus::ProviderStatus(const backend::ResponseError &error)
    : m_ok(false)
{
    auto &code = error.error();
    if (auto *badStatus = std::get_if<backend::ResponseError::BadStatus>(&code)) {
        m_detail = QObject::tr("HTTP %1%2%3").arg(
            QString::number(badStatus->code),
            badStatus->reason ? u" "_s : QString(),
            badStatus->reason.value_or(QString())
        );
        return;
    }
    if (auto *netErr = std::get_if<QNetworkReply::NetworkError>(&code)) {
        auto meta = QMetaEnum::fromType<QNetworkReply::NetworkError>();
        m_detail = QString::fromUtf8(meta.valueToKey(*netErr));
        return;
    }
    m_detail = QObject::tr("(unknown error)");
}

ModelProvider::ModelProvider(protected_t, QUuid id, QString name, QUrl baseUrl) // create built-in or load
    : m_id(std::move(id)), m_name(std::move(name)), m_baseUrl(std::move(baseUrl))
{ Q_ASSERT(!m_id.isNull()); }

ModelProvider::~ModelProvider() noexcept = default;

auto ModelProvider::newModel(const QVariant &key) const -> std::shared_ptr<ModelDescription>
{ return newModelImpl(key); }

ModelProviderMutable::~ModelProviderMutable() noexcept
{
    if (!m_id.isNull()) // (will be null if constructor throws)
        if (auto res = m_store->release(m_id); !res)
            res.error().raise(); // should not happen - will terminate program
}

DataStoreResult<> ModelProviderCustom::setName(QString value)
{
    if (value.isEmpty())
        return std::unexpected(u"name cannot be empty"_s);
    return setMemberProp<QString>(&ModelProviderCustom::m_name, "name", std::move(value));
}

auto ModelProviderCustom::persist() -> DataStoreResult<>
{
    if (auto res = m_store->create(asData()); !res)
        return res;
    m_persisted = true;
    return {};
}

ProviderRegistry::ProviderRegistry(PathSet paths)
    : m_customStore (std::move(paths.custom ))
    , m_builtinStore(std::move(paths.builtin))
{
    auto *mysettings = MySettings::globalInstance();
    connect(mysettings, &MySettings::modelPathChanged, this, &ProviderRegistry::onModelPathChanged);
    load();
}

namespace {
    class ProviderRegistryInternal : public ProviderRegistry {};
    Q_GLOBAL_STATIC(ProviderRegistryInternal, providerRegistry)
}

ProviderRegistry *ProviderRegistry::globalInstance()
{ return providerRegistry(); }

void ProviderRegistry::load()
{
    auto registerListener = [this](ModelProvider *provider) {
        // listen for any change in the provider so we can tell the model about it
        if (auto *mut = dynamic_cast<ModelProviderMutable *>(provider))
            connect(mut->asQObject(),  SIGNAL(apiKeyChanged (const QString &)), this, SLOT(onProviderChanged()));
        if (auto *cust = dynamic_cast<ModelProviderCustom *>(provider)) {
            connect(cust->asQObject(), SIGNAL(nameChanged   (const QString &)), this, SLOT(onProviderChanged()));
            connect(cust->asQObject(), SIGNAL(baseUrlChanged(const QUrl    &)), this, SLOT(onProviderChanged()));
        }
    };
    for (auto &p : s_builtinProviders) { // (not all builtin providers are stored)
        auto provider = OpenaiProviderBuiltin::create(
            &m_builtinStore, p.id, p.name, p.icon, p.base_url,
            std::unordered_set<QString>(p.model_whitelist.begin(), p.model_whitelist.end())
        );
        auto [it, unique] = m_providers.emplace(p.id, std::move(provider));
        if (!unique)
            throw std::logic_error(fmt::format("duplicate builtin provider id: {}", p.id.toString()));
        m_providerList.push_back(&p.id);
        registerListener(it->second.get());
    }
    for (auto p : m_customStore.list()) { // disk is source of truth for custom providers
        if (!p.custom_details) {
            qWarning() << "ignoring builtin provider in custom store:" << p.id;
            continue;
        }
        auto &cust = *p.custom_details;
        std::shared_ptr<ModelProviderCustom> provider;
        switch (p.type()) {
            using enum ProviderType;
        case ollama:
            provider = OllamaProviderCustom::create(
                &m_customStore, p.id, cust.name, cust.base_url
            );
            break;
        case openai:
            provider = OpenaiProviderCustom::create(
                &m_customStore, p.id, cust.name, cust.base_url,
                std::get<size_t(ProviderType::openai)>(p.provider_details).api_key
            );
        }
        auto [it, unique] = m_providers.emplace(p.id, std::move(provider));
        if (!unique)
            qWarning() << "ignoring duplicate custom provider with id:" << p.id;
        m_providerList.push_back(&it->second->id());
        registerListener(it->second.get());
    }
}

auto ProviderRegistry::add(std::shared_ptr<ModelProviderCustom> provider) -> DataStoreResult<>
{
    if (auto res = provider->persist(); !res)
        return res;
    auto [it, unique] = m_providers.emplace(provider->id(), std::move(provider));
    if (!unique)
        return std::unexpected(u"custom provider already registered: %1"_s.arg(provider->id().toString()));
    m_providerList.push_back(&it->second->id());
    emit customProviderAdded(m_providerList.size() - 1);
    return {};
}

bool ProviderRegistry::addQml(QmlSharedPtr *provider)
{
    auto obj = std::dynamic_pointer_cast<ModelProviderCustom>(provider->ptr());
    if (!obj) {
        qWarning() << "ProviderRegistry::add failed: Expected ModelProviderCustom, got"
                   << provider->metaObject()->className();
        return false;
    }
    auto res = add(obj);
    if (!res)
        qWarning() << "ProviderRegistry::add failed:" << res.error().errorString();
    return bool(res);
}

auto ProviderRegistry::providerAt(size_t i) const -> const ModelProvider *
{
    auto it = m_providers.find(*m_providerList.at(i));
    Q_ASSERT(it != m_providers.end());
    return it->second.get();
}

auto ProviderRegistry::getSubdirs() -> PathSet
{
    auto *mysettings = MySettings::globalInstance();
    auto parent = toFSPath(mysettings->modelPath()) / "providers";
    return { .builtin = parent, .custom = parent / "custom" };
}

void ProviderRegistry::onModelPathChanged()
{
    auto paths = getSubdirs();
    if (paths.builtin != m_builtinStore.path()) {
        emit aboutToBeCleared();
        // delete providers to release store locks
        m_providers.clear();
        m_providerList.clear();
        if (auto res = m_builtinStore.setPath(paths.builtin); !res)
            res.error().raise(); // should not happen
        if (auto res = m_customStore.setPath(paths.custom); !res)
            res.error().raise(); // should not happen
        load();
    }
}

void ProviderRegistry::onProviderChanged()
{
    // notify that this provider has changed
    auto *obj = &dynamic_cast<ModelProvider &>(*QObject::sender());
    auto it = ranges::find_if(m_providerList, [&](auto *id) { return *id == obj->id(); });
    if (it < m_providerList.end())
        emit customProviderChanged(it - m_providerList.begin());
}

ProviderList::ProviderList()
    : m_size(ProviderRegistry::globalInstance()->providerCount())
{
    auto *registry = ProviderRegistry::globalInstance();
    connect(registry, &ProviderRegistry::customProviderAdded, this, &ProviderList::onCustomProviderAdded);
    connect(registry, &ProviderRegistry::customProviderRemoved, this, &ProviderList::onCustomProviderRemoved);
    connect(registry, &ProviderRegistry::customProviderChanged, this, &ProviderList::onCustomProviderChanged);
    connect(registry, &ProviderRegistry::aboutToBeCleared, this, &ProviderList::onAboutToBeCleared,
            Qt::DirectConnection);
}

auto ProviderList::roleNames() const -> QHash<int, QByteArray>
{ return { { Qt::DisplayRole, "provider"_ba } }; }

QVariant ProviderList::data(const QModelIndex &index, int role) const
{
    auto *registry = ProviderRegistry::globalInstance();
    if (index.isValid() && index.row() < rowCount() && role == Qt::DisplayRole)
        return QVariant::fromValue(registry->providerAt(index.row())->asQObject());
    return {};
}

void ProviderList::onCustomProviderAdded(size_t index)
{
    beginInsertRows({}, index, index);
    m_size++;
    endInsertRows();
}

void ProviderList::onCustomProviderRemoved(size_t index)
{
    beginRemoveRows({}, index, index);
    m_size--;
    endRemoveRows();
}

void ProviderList::onCustomProviderChanged(size_t index)
{
    auto i = this->index(index);
    emit dataChanged(i, i);
}

void ProviderList::onAboutToBeCleared()
{
    beginResetModel();
    m_size = 0;
    endResetModel();
}

bool ProviderListSort::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    auto *leftData  = dynamic_cast<ModelProvider *>(sourceModel()->data(left ).value<QObject *>());
    auto *rightData = dynamic_cast<ModelProvider *>(sourceModel()->data(right).value<QObject *>());
    if (leftData && rightData) {
        if (leftData->isBuiltin() != rightData->isBuiltin())
            return leftData->isBuiltin() > rightData->isBuiltin(); // builtins first
        if (leftData->isBuiltin())
            return left.row() < right.row(); // preserve order of builtins
        return QString::localeAwareCompare(leftData->name(), rightData->name()) < 0; // sort by name
    }
    return true;
}


} // namespace gpt4all::ui
