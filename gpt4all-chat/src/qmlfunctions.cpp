#include "qmlfunctions.h"

#include "llmodel_ollama.h"
#include "llmodel_openai.h"
#include "llmodel_provider.h"

#include <exception>
#include <utility>


namespace gpt4all::ui {


QmlSharedPtr *QmlFunctions::newCustomOpenaiProvider(QString name, QUrl baseUrl, QString apiKey) const
{
    auto *store = ProviderRegistry::globalInstance()->customStore();
    std::shared_ptr<OpenaiProviderCustom> ptr;
    try {
        ptr = OpenaiProviderCustom::create(store, std::move(name), std::move(baseUrl), std::move(apiKey));
    } catch (const std::exception &e) {
        qWarning() << "newCustomOpenaiProvider failed:" << e.what();
        return nullptr;
    }
    return new QmlSharedPtr(std::move(ptr));
}

QmlSharedPtr *QmlFunctions::newCustomOllamaProvider(QString name, QUrl baseUrl) const
{
    auto *store = ProviderRegistry::globalInstance()->customStore();
    std::shared_ptr<OllamaProviderCustom> ptr;
    try {
        ptr = OllamaProviderCustom::create(store, std::move(name), std::move(baseUrl));
    } catch (const std::exception &e) {
        qWarning() << "newCustomOllamaProvider failed:" << e.what();
        return nullptr;
    }
    return new QmlSharedPtr(std::move(ptr));
}


} // namespace gpt4all::ui
