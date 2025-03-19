#pragma once

#include "store_base.h"

#include <boost/describe/class.hpp>
#include <boost/describe/enum.hpp>
#include <boost/json.hpp> // IWYU pragma: keep

#include <QObject>
#include <QString>
#include <QUrl>
#include <QUuid>


namespace gpt4all::ui {


inline namespace store_provider {

Q_NAMESPACE

// indices of this enum should be consistent with indices of ProviderDetails
enum class ProviderType {
    openai = 0,
    ollama = 1,
};
BOOST_DESCRIBE_ENUM(ProviderType, openai, ollama)
Q_ENUM_NS(ProviderType)

} // inline namespace provider

struct CustomProviderDetails {
    QString name;
    QUrl    base_url;
};

struct OpenaiProviderDetails {
    QString api_key;
};

struct ModelProviderData {
    using ProviderDetails = std::variant<OpenaiProviderDetails, std::monostate>;
    QUuid                                id;
    std::optional<CustomProviderDetails> custom_details;
    ProviderDetails                      provider_details;

    ProviderType type() const { return ProviderType(provider_details.index()); }
};
void tag_invoke(const boost::json::value_from_tag &, boost::json::value &jv, ModelProviderData data);
auto tag_invoke(const boost::json::value_to_tag<ModelProviderData> &, const boost::json::value &jv)
    -> ModelProviderData;

class ProviderStore : public DataStore<ModelProviderData> {
private:
    using Super = DataStore<ModelProviderData>;

public:
    using Super::Super;

    auto create(ModelProviderData data) -> DataStoreResult<>;
};


} // namespace gpt4all::ui
