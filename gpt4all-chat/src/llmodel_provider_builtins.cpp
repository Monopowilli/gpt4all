#include "llmodel_provider.h"

using namespace Qt::StringLiterals;


namespace gpt4all::ui {


static const QString MODEL_WHITELIST_GROQ[] {
    // last updated 2025-02-24
    u"deepseek-r1-distill-llama-70b"_s,
    u"deepseek-r1-distill-qwen-32b"_s,
    u"gemma2-9b-it"_s,
    u"llama-3.1-8b-instant"_s,
    u"llama-3.2-1b-preview"_s,
    u"llama-3.2-3b-preview"_s,
    u"llama-3.3-70b-specdec"_s,
    u"llama-3.3-70b-versatile"_s,
    u"llama3-70b-8192"_s,
    u"llama3-8b-8192"_s,
    u"mixtral-8x7b-32768"_s,
    u"qwen-2.5-32b"_s,
    u"qwen-2.5-coder-32b"_s,
};

static const QString MODEL_WHITELIST_OPENAI[] {
    // last updated 2025-02-24
    "gpt-3.5-turbo",
    "gpt-3.5-turbo-16k",
    "gpt-4",
    "gpt-4-32k",
    "gpt-4-turbo",
    "gpt-4o",
};

static const QString MODEL_WHITELIST_MISTRAL[] {
    // last updated 2025-02-24
    "codestral-2405",
    "codestral-2411-rc5",
    "codestral-2412",
    "codestral-2501",
    "codestral-latest",
    "codestral-mamba-2407",
    "codestral-mamba-latest",
    "ministral-3b-2410",
    "ministral-3b-latest",
    "ministral-8b-2410",
    "ministral-8b-latest",
    "mistral-large-2402",
    "mistral-large-2407",
    "mistral-large-2411",
    "mistral-large-latest",
    "mistral-medium-2312",
    "mistral-medium-latest",
    "mistral-saba-2502",
    "mistral-saba-latest",
    "mistral-small-2312",
    "mistral-small-2402",
    "mistral-small-2409",
    "mistral-small-2501",
    "mistral-small-latest",
    "mistral-tiny-2312",
    "mistral-tiny-2407",
    "mistral-tiny-latest",
    "open-codestral-mamba",
    "open-mistral-7b",
    "open-mistral-nemo",
    "open-mistral-nemo-2407",
    "open-mixtral-8x22b",
    "open-mixtral-8x22b-2404",
    "open-mixtral-8x7b",
};

const std::array<
    ProviderRegistry::BuiltinProviderData, ProviderRegistry::N_BUILTIN
> ProviderRegistry::s_builtinProviders {
    BuiltinProviderData {
        .id              = QUuid("20f963dc-1f99-441e-ad80-f30a0a06bcac"),
        .name            = u"Groq"_s,
        .icon            = u"qrc:/gpt4all/icons/groq.svg"_s,
        .base_url        = u"https://api.groq.com/openai/v1/"_s,
        .model_whitelist = MODEL_WHITELIST_GROQ,
    },
    BuiltinProviderData {
        .id              = QUuid("6f874c3a-f1ad-47f7-9129-755c5477146c"),
        .name            = u"OpenAI"_s,
        .icon            = u"qrc:/gpt4all/icons/openai.svg"_s,
        .base_url        = u"https://api.openai.com/v1/"_s,
        .model_whitelist = MODEL_WHITELIST_OPENAI,
    },
    BuiltinProviderData {
        .id              = QUuid("7ae617b3-c0b2-4d2c-9ff2-bc3f049494cc"),
        .name            = u"Mistral"_s,
        .icon            = u"qrc:/gpt4all/icons/mistral.svg"_s,
        .base_url        = u"https://api.mistral.ai/v1/"_s,
        .model_whitelist = MODEL_WHITELIST_MISTRAL,
    },
};


} // namespace gpt4all::ui
