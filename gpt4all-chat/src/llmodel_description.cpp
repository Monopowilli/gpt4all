#include "llmodel_description.h"

#include "llmodel_chat.h"
#include "llmodel_provider.h"


namespace gpt4all::ui {


ModelDescription::~ModelDescription() noexcept = default;

auto ModelDescription::newInstance(QNetworkAccessManager *nam) const -> std::unique_ptr<ChatLLMInstance>
{ return std::unique_ptr<ChatLLMInstance>(newInstanceImpl(nam)); }

bool operator==(const ModelDescription &a, const ModelDescription &b)
{
    if (typeid(a) != typeid(b))
        return false;

    return *a.provider() == *b.provider() && a.key() == b.key();
}


} // namespace gpt4all::ui
