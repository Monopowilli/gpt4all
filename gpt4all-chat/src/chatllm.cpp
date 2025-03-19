#include "chatllm.h"

#include "chat.h"
#include "chatmodel.h"
#include "jinja_helpers.h"
#include "llmodel_chat.h"
#include "llmodel_description.h"
#include "llmodel_provider.h"
#include "localdocs.h"
#include "mysettings.h"
#include "network.h"
#include "tool.h"
#include "toolcallparser.h"
#include "toolmodel.h"

#include <QCoro/QCoroAsyncGenerator>
#include <QCoro/QCoroTask>
#include <fmt/format.h>
#include <gpt4all-backend/generation-params.h>
#include <minja/minja.hpp>
#include <nlohmann/json.hpp>

#include <QChar>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QGlobalStatic>
#include <QIODevice> // IWYU pragma: keep
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QMutex> // IWYU pragma: keep
#include <QMutexLocker> // IWYU pragma: keep
#include <QRegularExpression> // IWYU pragma: keep
#include <QRegularExpressionMatch> // IWYU pragma: keep
#include <QSet>
#include <QStringView>
#include <QTextStream>
#include <QUrl>
#include <QVariant>
#include <QWaitCondition>
#include <Qt>
#include <QtAssert>
#include <QtLogging>
#include <QtTypes> // IWYU pragma: keep

#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <ranges>
#include <regex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace Qt::Literals::StringLiterals;
using namespace ToolEnums;
using namespace gpt4all;
using namespace gpt4all::ui;
namespace ranges = std::ranges;
using json = nlohmann::ordered_json;

//#define DEBUG
//#define DEBUG_MODEL_LOADING

// NOTE: not threadsafe
static const std::shared_ptr<minja::Context> &jinjaEnv()
{
    static std::shared_ptr<minja::Context> environment;
    if (!environment) {
        environment = minja::Context::builtins();
        environment->set("strftime_now", minja::simple_function(
            "strftime_now", { "format" },
            [](const std::shared_ptr<minja::Context> &, minja::Value &args) -> minja::Value {
                auto format = args.at("format").get<std::string>();
                using Clock = std::chrono::system_clock;
                time_t nowUnix = Clock::to_time_t(Clock::now());
                auto localDate = *std::localtime(&nowUnix);
                std::ostringstream ss;
                ss << std::put_time(&localDate, format.c_str());
                return ss.str();
            }
        ));
        environment->set("regex_replace", minja::simple_function(
            "regex_replace", { "str", "pattern", "repl" },
            [](const std::shared_ptr<minja::Context> &, minja::Value &args) -> minja::Value {
                auto str     = args.at("str"    ).get<std::string>();
                auto pattern = args.at("pattern").get<std::string>();
                auto repl    = args.at("repl"   ).get<std::string>();
                return std::regex_replace(str, std::regex(pattern), repl);
            }
        ));
    }
    return environment;
}

class BaseResponseHandler {
public:
    virtual void onSplitIntoTwo    (const QString &startTag, const QString &firstBuffer, const QString &secondBuffer) = 0;
    virtual void onSplitIntoThree  (const QString &secondBuffer, const QString &thirdBuffer)                          = 0;
    // "old-style" responses, with all of the implementation details left in
    virtual void onOldResponseChunk(const QByteArray &chunk)                                                          = 0;
    // notify of a "new-style" response that has been cleaned of tool calling
    virtual bool onBufferResponse  (const QString &response, int bufferIdx)                                           = 0;
    // notify of a "new-style" response, no tool calling applicable
    virtual bool onRegularResponse ()                                                                                 = 0;
    virtual bool getStopGenerating () const                                                                           = 0;
};

struct PromptModelWithToolsResult {
    ChatResponseMetadata metadata;
    QStringList          toolCallBuffers;
    bool                 shouldExecuteToolCall;
};
static auto promptModelWithTools(
    ChatLLMInstance *model, BaseResponseHandler &respHandler, const GenerationParams *params, const QByteArray &prompt,
    const QStringList &toolNames
) -> QCoro::Task<PromptModelWithToolsResult>
{
    ToolCallParser toolCallParser(toolNames);
    auto handleResponse = [&toolCallParser, &respHandler](std::string_view piece) -> bool {
        toolCallParser.update(piece.data());

        // Split the response into two if needed
        if (toolCallParser.numberOfBuffers() < 2 && toolCallParser.splitIfPossible()) {
            const auto parseBuffers = toolCallParser.buffers();
            Q_ASSERT(parseBuffers.size() == 2);
            respHandler.onSplitIntoTwo(toolCallParser.startTag(), parseBuffers.at(0), parseBuffers.at(1));
        }

        // Split the response into three if needed
        if (toolCallParser.numberOfBuffers() < 3 && toolCallParser.startTag() == ToolCallConstants::ThinkStartTag
            && toolCallParser.splitIfPossible()) {
            const auto parseBuffers = toolCallParser.buffers();
            Q_ASSERT(parseBuffers.size() == 3);
            respHandler.onSplitIntoThree(parseBuffers.at(1), parseBuffers.at(2));
        }

        respHandler.onOldResponseChunk(QByteArray::fromRawData(piece.data(), piece.size()));

        bool ok;
        const auto parseBuffers = toolCallParser.buffers();
        if (parseBuffers.size() > 1) {
            ok = respHandler.onBufferResponse(parseBuffers.last(), parseBuffers.size() - 1);
        } else {
            ok = respHandler.onRegularResponse();
        }
        if (!ok)
            return false;

        const bool shouldExecuteToolCall = toolCallParser.state() == ToolEnums::ParseState::Complete
            && toolCallParser.startTag() != ToolCallConstants::ThinkStartTag;

        return !shouldExecuteToolCall && !respHandler.getStopGenerating();
    };
    ChatResponseMetadata metadata;
    auto stream = model->generate(QString::fromUtf8(prompt), params, metadata);
    QCORO_FOREACH(auto &piece, stream) {
        if (!handleResponse(std::string_view(piece.toUtf8())))
            break;
    }

    const bool shouldExecuteToolCall = toolCallParser.state() == ToolEnums::ParseState::Complete
        && toolCallParser.startTag() != ToolCallConstants::ThinkStartTag;

    co_return { metadata, toolCallParser.buffers(), shouldExecuteToolCall };
}

class LLModelStore {
public:
    static LLModelStore *globalInstance();

    auto acquireModel() -> std::unique_ptr<ChatLLMInstance>; // will block until llmodel is ready
    void releaseModel(std::unique_ptr<ChatLLMInstance> &&info); // must be called when you are done
    void destroy();

private:
    LLModelStore() { m_availableModel.emplace(); /* seed with empty model */ }
    ~LLModelStore() = default;
    std::optional<std::unique_ptr<ChatLLMInstance>> m_availableModel;
    QMutex m_mutex;
    QWaitCondition m_condition;
    friend class MyLLModelStore;
};

class MyLLModelStore : public LLModelStore { };
Q_GLOBAL_STATIC(MyLLModelStore, storeInstance)
LLModelStore *LLModelStore::globalInstance()
{
    return storeInstance();
}

auto LLModelStore::acquireModel() -> std::unique_ptr<ChatLLMInstance>
{
    QMutexLocker locker(&m_mutex);
    while (!m_availableModel)
        m_condition.wait(locker.mutex());
    auto first = std::move(*m_availableModel);
    m_availableModel.reset();
    return first;
}

void LLModelStore::releaseModel(std::unique_ptr<ChatLLMInstance> &&info)
{
    QMutexLocker locker(&m_mutex);
    Q_ASSERT(!m_availableModel);
    m_availableModel = std::move(info);
    m_condition.wakeAll();
}

void LLModelStore::destroy()
{
    QMutexLocker locker(&m_mutex);
    m_availableModel.reset();
}

ChatLLM::ChatLLM(Chat *parent, bool isServer)
    : QObject{nullptr}
    , m_chat(parent)
    , m_shouldBeLoaded(false)
    , m_forceUnloadModel(false)
    , m_markedForDeletion(false)
    , m_stopGenerating(false)
    , m_timer(nullptr)
    , m_isServer(isServer)
    , m_chatModel(parent->chatModel())
{
    moveToThread(&m_llmThread);
    connect(this, &ChatLLM::shouldBeLoadedChanged, this, &ChatLLM::handleShouldBeLoadedChanged,
        Qt::QueuedConnection); // explicitly queued
    connect(this, &ChatLLM::trySwitchContextRequested, this, &ChatLLM::trySwitchContextOfLoadedModel,
        Qt::QueuedConnection); // explicitly queued
    connect(parent, &Chat::idChanged, this, &ChatLLM::handleChatIdChanged);
    connect(&m_llmThread, &QThread::started, this, &ChatLLM::handleThreadStarted);

    // The following are blocking operations and will block the llm thread
    connect(this, &ChatLLM::requestRetrieveFromDB, LocalDocs::globalInstance()->database(), &Database::retrieveFromDB,
        Qt::BlockingQueuedConnection);

    m_llmThread.setObjectName(parent->id());
    m_llmThread.start();
}

ChatLLM::~ChatLLM()
{
    destroy();
}

void ChatLLM::destroy()
{
    m_stopGenerating = true;
    m_llmThread.quit();
    m_llmThread.wait();

    // The only time we should have a model loaded here is on shutdown
    // as we explicitly unload the model in all other circumstances
    if (isModelLoaded())
        m_llmInstance.reset();
}

void ChatLLM::destroyStore()
{
    LLModelStore::globalInstance()->destroy();
}

void ChatLLM::handleThreadStarted()
{
    m_timer = new TokenTimer(this);
    connect(m_timer, &TokenTimer::report, this, &ChatLLM::reportSpeed);
    emit threadStarted();
}

bool ChatLLM::loadDefaultModel()
{
    ModelInfo defaultModel = ModelList::globalInstance()->defaultModelInfo();
    if (defaultModel.filename().isEmpty()) {
        emit modelLoadingError(u"Could not find any model to load"_s);
        return false;
    }
    return QCoro::waitFor(loadModel(defaultModel));
}

void ChatLLM::trySwitchContextOfLoadedModel(const ModelInfo &modelInfo)
{
    // We're trying to see if the store already has the model fully loaded that we wish to use
    // and if so we just acquire it from the store and switch the context and return true. If the
    // store doesn't have it or we're already loaded or in any other case just return false.

    // If we're already loaded or a server or the modelInfo is empty, then this should fail
    if (
        isModelLoaded() || m_isServer || modelInfo.name().isEmpty() || !m_shouldBeLoaded
    ) {
        emit trySwitchContextOfLoadedModelCompleted(0);
        return;
    }

    acquireModel();
#if defined(DEBUG_MODEL_LOADING)
        qDebug() << "acquired model from store" << m_llmThread.objectName() << m_llmInstance.get();
#endif

    // The store gave us no already loaded model, the wrong type of model, then give it back to the
    // store and fail
    if (!m_llmInstance || *m_llmInstance->description() != *modelInfo.modelDesc() || !m_shouldBeLoaded) {
        LLModelStore::globalInstance()->releaseModel(std::move(m_llmInstance));
        emit trySwitchContextOfLoadedModelCompleted(0);
        return;
    }

#if defined(DEBUG_MODEL_LOADING)
    qDebug() << "store had our model" << m_llmThread.objectName() << m_llmInstance.model.get();
#endif

    emit trySwitchContextOfLoadedModelCompleted(2);
    emit modelLoadingPercentageChanged(1.0f);
    emit trySwitchContextOfLoadedModelCompleted(0);
}

auto ChatLLM::loadModel(const ModelInfo &modelInfo) -> QCoro::Task<bool>
{
    bool alreadyAcquired = isModelLoaded();
    if (alreadyAcquired && *modelInfo.modelDesc() == *m_modelInfo.modelDesc()) {
        // already acquired -> keep it
        if (modelInfo != m_modelInfo) {
            // switch to different clone of same model
            Q_ASSERT(modelInfo.isClone() || m_modelInfo.isClone());
            m_modelInfo = modelInfo;
        }
        co_return true;
    }

    // reset status
    emit modelLoadingPercentageChanged(std::numeric_limits<float>::min()); // small non-zero positive value
    emit modelLoadingError("");

    if (alreadyAcquired) {
        // we own a different model -> destroy it and load the requested one
        m_llmInstance.reset();
    } else if (!m_isServer) { // (the server loads models lazily rather than eagerly)
        // wait for the model to become available
        acquireModel(); // (blocks)

        // check if request was canceled while we were waiting
        if (!m_shouldBeLoaded) {
            LLModelStore::globalInstance()->releaseModel(std::move(m_llmInstance));
            emit modelLoadingPercentageChanged(0.0f);
            co_return false;
        }

        // if it was the requested model, we are done
        if (m_llmInstance && *m_llmInstance->description() == *modelInfo.modelDesc()) {
            emit modelLoadingPercentageChanged(1.0f);
            setModelInfo(modelInfo);
            Q_ASSERT(!m_modelInfo.filename().isEmpty());
            if (m_modelInfo.filename().isEmpty())
                emit modelLoadingError(u"Modelinfo is left null for %1"_s.arg(modelInfo.filename()));
            co_return true;
        }

        // we own a different model -> destroy it and load the requested one
        m_llmInstance.reset();
    }

    QVariantMap modelLoadProps;
    if (!co_await loadNewModel(modelInfo, modelLoadProps))
        co_return false; // m_shouldBeLoaded became false

    emit modelLoadingPercentageChanged(isModelLoaded() ? 1.0f : 0.0f);
    emit loadedModelInfoChanged();

    modelLoadProps.insert("model", modelInfo.filename());
    Network::globalInstance()->trackChatEvent("model_load", modelLoadProps);

    if (m_llmInstance)
        setModelInfo(modelInfo);
    co_return bool(m_llmInstance);
}

/* Returns false if the model should no longer be loaded (!m_shouldBeLoaded).
 * Otherwise returns true, even on error. */
auto ChatLLM::loadNewModel(const ModelInfo &modelInfo, QVariantMap &modelLoadProps) -> QCoro::Task<bool>
{
    auto *mysettings = MySettings::globalInstance();

    QElapsedTimer modelLoadTimer;
    modelLoadTimer.start();

    // TODO: pass these as generation params
    int n_ctx = mysettings->modelContextLength(modelInfo);
    int ngl   = mysettings->modelGpuLayers    (modelInfo);

    m_llmInstance = modelInfo.modelDesc()->newInstance(&m_nam);

    // TODO: progress callback
#if 0
    m_llmInstance->setProgressCallback([this](float progress) -> bool {
        progress = std::max(progress, std::numeric_limits<float>::min()); // keep progress above zero
        emit modelLoadingPercentageChanged(progress);
        return m_shouldBeLoaded;
    });
#endif
    co_await m_llmInstance->preload();

    if (!m_shouldBeLoaded) {
        m_llmInstance.reset();
        if (!m_isServer)
            LLModelStore::globalInstance()->releaseModel(std::move(m_llmInstance));
        resetModel();
        emit modelLoadingPercentageChanged(0.0f);
        co_return false;
    }

    bool success = true; // TODO: check for failure
    if (!success) {
        m_llmInstance.reset();
        if (!m_isServer)
            LLModelStore::globalInstance()->releaseModel(std::move(m_llmInstance));
        resetModel();
        emit modelLoadingError(u"Could not load model due to invalid model file for %1"_s.arg(modelInfo.filename()));
        modelLoadProps.insert("error", "loadmodel_failed");
        co_return true;
    }

    modelLoadProps.insert("$duration", modelLoadTimer.elapsed() / 1000.);
    co_return true;
}

bool ChatLLM::isModelLoaded() const
{ return bool(m_llmInstance); }

static QString &removeLeadingWhitespace(QString &s)
{
    auto firstNonSpace = ranges::find_if_not(s, [](auto c) { return c.isSpace(); });
    s.remove(0, firstNonSpace - s.begin());
    return s;
}

template <ranges::input_range R>
    requires std::convertible_to<ranges::range_reference_t<R>, QChar>
bool isAllSpace(R &&r)
{
    return ranges::all_of(std::forward<R>(r), [](QChar c) { return c.isSpace(); });
}

void ChatLLM::regenerateResponse(int index)
{
    Q_ASSERT(m_chatModel);
    if (m_chatModel->regenerateResponse(index)) {
        emit responseChanged();
        prompt(m_chat->collectionList());
    }
}

std::optional<QString> ChatLLM::popPrompt(int index)
{
    Q_ASSERT(m_chatModel);
    return m_chatModel->popPrompt(index);
}

ModelInfo ChatLLM::modelInfo() const
{
    return m_modelInfo;
}

void ChatLLM::setModelInfo(const ModelInfo &modelInfo)
{
    m_modelInfo = modelInfo;
    emit modelInfoChanged(modelInfo);
}

void ChatLLM::acquireModel()
{ m_llmInstance = LLModelStore::globalInstance()->acquireModel(); }

void ChatLLM::resetModel()
{ m_llmInstance.reset(); }

void ChatLLM::modelChangeRequested(const ModelInfo &modelInfo)
{
    // ignore attempts to switch to the same model twice
    if (!isModelLoaded() || this->modelInfo() != modelInfo) {
        m_shouldBeLoaded = true;
        QCoro::waitFor(loadModel(modelInfo));
    }
}

auto ChatLLM::modelProvider() -> const ModelProvider *
{ return m_llmInstance->description()->provider(); }

void ChatLLM::prompt(const QStringList &enabledCollections)
{
    auto *mySettings = MySettings::globalInstance();

    if (!isModelLoaded()) {
        emit responseStopped(0);
        return;
    }

    try {
        QCoro::waitFor(promptInternalChat(enabledCollections, mySettings->modelGenParams(m_modelInfo).get()));
    } catch (const std::exception &e) {
        // FIXME(jared): this is neither translated nor serialized
        m_chatModel->setResponseValue(u"Error: %1"_s.arg(QString::fromUtf8(e.what())));
        m_chatModel->setError();
        emit responseStopped(0);
    }
}

std::vector<MessageItem> ChatLLM::forkConversation(const QString &prompt) const
{
    Q_ASSERT(m_chatModel);
    if (m_chatModel->hasError())
        throw std::logic_error("cannot continue conversation with an error");

    std::vector<MessageItem> conversation;
    {
        auto items = m_chatModel->messageItems();
        // It is possible the main thread could have erased the conversation while the llm thread,
        // is busy forking the conversatoin but it must have set stop generating first
        Q_ASSERT(items.size() >= 2 || m_stopGenerating); // should be prompt/response pairs
        conversation.reserve(items.size() + 1);
        conversation.assign(items.begin(), items.end());
    }
    qsizetype nextIndex = conversation.empty() ? 0 : conversation.back().index().value() + 1;
    conversation.emplace_back(nextIndex, MessageItem::Type::Prompt, prompt.toUtf8());
    return conversation;
}

// version 0 (default): HF compatible
// version 1: explicit LocalDocs formatting
static uint parseJinjaTemplateVersion(QStringView tmpl)
{
    static uint MAX_VERSION = 1;
    static QRegularExpression reVersion(uR"(\A{#-?\s+gpt4all v(\d+)-?#}\s*$)"_s, QRegularExpression::MultilineOption);
    if (auto match = reVersion.matchView(tmpl); match.hasMatch()) {
        uint ver = match.captured(1).toUInt();
        if (ver > MAX_VERSION)
            throw std::out_of_range(fmt::format("Unknown template version: {}", ver));
        return ver;
    }
    return 0;
}

static std::shared_ptr<minja::TemplateNode> loadJinjaTemplate(const std::string &source)
{
    return minja::Parser::parse(source, { .trim_blocks = true, .lstrip_blocks = true, .keep_trailing_newline = false });
}

std::optional<std::string> ChatLLM::checkJinjaTemplateError(const std::string &source)
{
    try {
        loadJinjaTemplate(source);
    } catch (const std::runtime_error &e) {
        return e.what();
    }
    return std::nullopt;
}

std::string ChatLLM::applyJinjaTemplate(std::span<const MessageItem> items) const
{
    Q_ASSERT(items.size() >= 1);

    auto *mySettings = MySettings::globalInstance();

    QString chatTemplate, systemMessage;
    auto chatTemplateSetting = mySettings->modelChatTemplate(m_modelInfo);
    if (auto tmpl = chatTemplateSetting.asModern()) {
        chatTemplate = *tmpl;
    } else if (chatTemplateSetting.isLegacy()) {
        throw std::logic_error("cannot apply Jinja to a legacy prompt template");
    } else {
        throw std::logic_error("cannot apply Jinja without setting a chat template first");
    }
    if (isAllSpace(chatTemplate)) {
        throw std::logic_error("cannot apply Jinja with a blank chat template");
    }
    if (auto tmpl = mySettings->modelSystemMessage(m_modelInfo).asModern()) {
        systemMessage = *tmpl;
    } else {
        throw std::logic_error("cannot apply Jinja with a legacy system message");
    }

    uint version = parseJinjaTemplateVersion(chatTemplate);

    auto makeMap = [version](const MessageItem &item) {
        return JinjaMessage(version, item).AsJson();
    };

    std::unique_ptr<MessageItem> systemItem;
    bool useSystem = !isAllSpace(systemMessage);

    json::array_t messages;
    messages.reserve(useSystem + items.size());
    if (useSystem) {
        systemItem = std::make_unique<MessageItem>(MessageItem::system_tag, systemMessage.toUtf8());
        messages.emplace_back(makeMap(*systemItem));
    }
    for (auto &item : items)
        messages.emplace_back(makeMap(item));

    json::array_t toolList;
    const int toolCount = ToolModel::globalInstance()->count();
    for (int i = 0; i < toolCount; ++i) {
        Tool *t = ToolModel::globalInstance()->get(i);
        toolList.push_back(t->jinjaValue());
    }

    json::object_t params {
        { "messages",              std::move(messages) },
        { "add_generation_prompt", true                },
        { "toolList",              toolList            },
    };
    // TODO: implement special tokens
#if 0
    for (auto &[name, token] : m_llmInstance->specialTokens())
        params.emplace(std::move(name), std::move(token));
#endif

    try {
        auto tmpl = loadJinjaTemplate(chatTemplate.toStdString());
        auto context = minja::Context::make(minja::Value(std::move(params)), jinjaEnv());
        return tmpl->render(context);
    } catch (const std::runtime_error &e) {
        throw std::runtime_error(fmt::format("Failed to parse chat template: {}", e.what()));
    }
    Q_UNREACHABLE();
}

auto ChatLLM::promptInternalChat(const QStringList &enabledCollections, const GenerationParams *params,
                                 qsizetype startOffset) -> QCoro::Task<ChatPromptResult>
{
    Q_ASSERT(isModelLoaded());
    Q_ASSERT(m_chatModel);

    // Return a vector of relevant messages for this chat.
    // "startOffset" is used to select only local server messages from the current chat session.
    auto getChat = [&]() {
        auto items = m_chatModel->messageItems();
        if (startOffset > 0)
            items.erase(items.begin(), items.begin() + startOffset);
        Q_ASSERT(items.size() >= 2);
        return items;
    };

    QList<ResultInfo> databaseResults;
    if (!enabledCollections.isEmpty()) {
        std::optional<std::pair<int, QString>> query;
        {
            // Find the prompt that represents the query. Server chats are flexible and may not have one.
            auto items = getChat();
            if (auto peer = m_chatModel->getPeer(items, items.end() - 1)) // peer of response
                query = { (*peer)->index().value(), (*peer)->content() };
        }

        if (query) {
            auto &[promptIndex, queryStr] = *query;
            const int retrievalSize = MySettings::globalInstance()->localDocsRetrievalSize();
            emit requestRetrieveFromDB(enabledCollections, queryStr, retrievalSize, &databaseResults); // blocks
            m_chatModel->updateSources(promptIndex, databaseResults);
            emit databaseResultsChanged(databaseResults);
        }
    }

    auto messageItems = getChat();
    messageItems.pop_back(); // exclude new response

    auto result = co_await promptInternal(messageItems, params, !databaseResults.isEmpty());
    co_return {
        /*PromptResult*/ {
            .response       = std::move(result.response),
            .promptTokens   = result.promptTokens,
            .responseTokens = result.responseTokens,
        },
        /*databaseResults*/ std::move(databaseResults),
    };
}

class ChatViewResponseHandler : public BaseResponseHandler {
public:
    ChatViewResponseHandler(ChatLLM *cllm, QElapsedTimer *totalTime, ChatLLM::PromptResult *result)
        : m_cllm(cllm), m_totalTime(totalTime), m_result(result) {}

    void onSplitIntoTwo(const QString &startTag, const QString &firstBuffer, const QString &secondBuffer) override
    {
        if (startTag == ToolCallConstants::ThinkStartTag)
            m_cllm->m_chatModel->splitThinking({ firstBuffer, secondBuffer });
        else
            m_cllm->m_chatModel->splitToolCall({ firstBuffer, secondBuffer });
    }

    void onSplitIntoThree(const QString &secondBuffer, const QString &thirdBuffer) override
    {
        m_cllm->m_chatModel->endThinking({ secondBuffer, thirdBuffer }, m_totalTime->elapsed());
    }

    void onOldResponseChunk(const QByteArray &chunk) override
    {
        m_result->responseTokens++;
        m_cllm->m_timer->inc();
        m_result->response.append(chunk);
    }

    bool onBufferResponse(const QString &response, int bufferIdx) override
    {
        Q_UNUSED(bufferIdx)
        try {
            QString r = response;
            m_cllm->m_chatModel->setResponseValue(removeLeadingWhitespace(r));
        } catch (const std::exception &e) {
            // We have a try/catch here because the main thread might have removed the response from
            // the chatmodel by erasing the conversation during the response... the main thread sets
            // m_stopGenerating before doing so, but it doesn't wait after that to reset the chatmodel
            Q_ASSERT(m_cllm->m_stopGenerating);
            return false;
        }
        emit m_cllm->responseChanged();
        return true;
    }

    bool onRegularResponse() override
    {
        auto respStr = QString::fromUtf8(m_result->response);
        return onBufferResponse(respStr, 0);
    }

    bool getStopGenerating() const override
    { return m_cllm->m_stopGenerating; }

private:
    ChatLLM               *m_cllm;
    QElapsedTimer         *m_totalTime;
    ChatLLM::PromptResult *m_result;
};

auto ChatLLM::promptInternal(
    const std::variant<std::span<const MessageItem>, std::string_view> &prompt, const GenerationParams *params,
    bool usedLocalDocs
) -> QCoro::Task<PromptResult>
{
    Q_ASSERT(isModelLoaded());

    auto *mySettings = MySettings::globalInstance();

    // unpack prompt argument
    const std::span<const MessageItem> *messageItems = nullptr;
    std::string                      jinjaBuffer;
    std::string_view                 conversation;
    if (auto *nonChat = std::get_if<std::string_view>(&prompt)) {
        conversation = *nonChat; // complete the string without a template
    } else {
        messageItems    = &std::get<std::span<const MessageItem>>(prompt);
        jinjaBuffer  = applyJinjaTemplate(*messageItems);
        conversation = jinjaBuffer;
    }

    PromptResult result {};

    QElapsedTimer totalTime;
    totalTime.start();
    ChatViewResponseHandler respHandler(this, &totalTime, &result);

    m_timer->start();
    PromptModelWithToolsResult withToolsResult;
    try {
        emit promptProcessing();
        m_stopGenerating = false;
        // TODO: set result.promptTokens based on ollama prompt_eval_count
        // TODO: support interruption via m_stopGenerating
        withToolsResult = co_await promptModelWithTools(
            m_llmInstance.get(), respHandler, params,
            QByteArray::fromRawData(conversation.data(), conversation.size()),
            ToolCallConstants::AllTagNames
        );
    } catch (...) {
        m_timer->stop();
        throw;
    }
    // TODO: use metadata
    auto &[metadata, finalBuffers, shouldExecuteTool] = withToolsResult;

    m_timer->stop();
    qint64 elapsed = totalTime.elapsed();

    // trim trailing whitespace
    auto respStr = QString::fromUtf8(result.response);
    if (!respStr.isEmpty() && (std::as_const(respStr).back().isSpace() || finalBuffers.size() > 1)) {
        if (finalBuffers.size() > 1)
            m_chatModel->setResponseValue(finalBuffers.last().trimmed());
        else
            m_chatModel->setResponseValue(respStr.trimmed());
        emit responseChanged();
    }

    bool doQuestions = false;
    if (!m_isServer && messageItems && !shouldExecuteTool) {
        switch (mySettings->suggestionMode()) {
            case SuggestionMode::On:            doQuestions = true;          break;
            case SuggestionMode::LocalDocsOnly: doQuestions = usedLocalDocs; break;
            case SuggestionMode::Off:           ;
        }
    }
    if (doQuestions)
        generateQuestions(elapsed);
    else
        emit responseStopped(elapsed);

    co_return result;
}

void ChatLLM::setShouldBeLoaded(bool b)
{
#if defined(DEBUG_MODEL_LOADING)
    qDebug() << "setShouldBeLoaded" << m_llmThread.objectName() << b << m_llmInstance.model.get();
#endif
    m_shouldBeLoaded = b; // atomic
    emit shouldBeLoadedChanged();
}

void ChatLLM::requestTrySwitchContext()
{
    m_shouldBeLoaded = true; // atomic
    emit trySwitchContextRequested(modelInfo());
}

void ChatLLM::handleShouldBeLoadedChanged()
{
    if (m_shouldBeLoaded)
        reloadModel();
    else
        unloadModel();
}

void ChatLLM::unloadModel()
{
    if (!isModelLoaded() || m_isServer)
        return;

    if (!m_forceUnloadModel || !m_shouldBeLoaded)
        emit modelLoadingPercentageChanged(0.0f);
    else
        emit modelLoadingPercentageChanged(std::numeric_limits<float>::min()); // small non-zero positive value

#if defined(DEBUG_MODEL_LOADING)
    qDebug() << "unloadModel" << m_llmThread.objectName() << m_llmInstance.model.get();
#endif

    if (m_forceUnloadModel) {
        m_llmInstance.reset();
        m_forceUnloadModel = false;
    }

    LLModelStore::globalInstance()->releaseModel(std::move(m_llmInstance));
}

void ChatLLM::reloadModel()
{
    if (isModelLoaded() && m_forceUnloadModel)
        unloadModel(); // we unload first if we are forcing an unload

    if (isModelLoaded() || m_isServer)
        return;

#if defined(DEBUG_MODEL_LOADING)
    qDebug() << "reloadModel" << m_llmThread.objectName() << m_llmInstance.model.get();
#endif
    const ModelInfo m = modelInfo();
    if (m.name().isEmpty())
        loadDefaultModel();
    else
        QCoro::waitFor(loadModel(m));
}

// This class throws discards the text within thinking tags, for use with chat names and follow-up questions.
class SimpleResponseHandler : public BaseResponseHandler {
public:
    SimpleResponseHandler(ChatLLM *cllm)
        : m_cllm(cllm) {}

    void onSplitIntoTwo(const QString &startTag, const QString &firstBuffer, const QString &secondBuffer) override
    { /* no-op */ }

    void onSplitIntoThree(const QString &secondBuffer, const QString &thirdBuffer) override
    { /* no-op */ }

    void onOldResponseChunk(const QByteArray &chunk) override
    { m_response.append(chunk); }

    bool onBufferResponse(const QString &response, int bufferIdx) override
    {
        if (bufferIdx == 1)
            return true; // ignore "think" content
        return onSimpleResponse(response);
    }

    bool onRegularResponse() override
    { return onBufferResponse(QString::fromUtf8(m_response), 0); }

    bool getStopGenerating() const override
    { return m_cllm->m_stopGenerating; }

protected:
    virtual bool onSimpleResponse(const QString &response) = 0;

protected:
    ChatLLM    *m_cllm;
    QByteArray  m_response;
};

class NameResponseHandler : public SimpleResponseHandler {
private:
    // max length of chat names, in words
    static constexpr qsizetype MAX_WORDS = 3;

public:
    using SimpleResponseHandler::SimpleResponseHandler;

protected:
    bool onSimpleResponse(const QString &response) override
    {
        QTextStream stream(const_cast<QString *>(&response), QIODeviceBase::ReadOnly);
        QStringList words;
        while (!stream.atEnd() && words.size() < MAX_WORDS) {
            QString word;
            stream >> word;
            words << word;
        }

        emit m_cllm->generatedNameChanged(words.join(u' '));
        return words.size() < MAX_WORDS || stream.atEnd();
    }
};

void ChatLLM::generateName()
{
    Q_ASSERT(isModelLoaded());
    if (!isModelLoaded() || m_isServer)
        return;

    Q_ASSERT(m_chatModel);

    auto *mySettings = MySettings::globalInstance();

    const QString chatNamePrompt = mySettings->modelChatNamePrompt(m_modelInfo);
    if (isAllSpace(chatNamePrompt)) {
        qWarning() << "ChatLLM: not generating chat name because prompt is empty";
        return;
    }

    NameResponseHandler respHandler(this);

    try {
        // TODO: support interruption via m_stopGenerating
        promptModelWithTools(
            m_llmInstance.get(),
            respHandler, mySettings->modelGenParams(m_modelInfo).get(),
            applyJinjaTemplate(forkConversation(chatNamePrompt)).c_str(),
            { ToolCallConstants::ThinkTagName }
        );
    } catch (const std::exception &e) {
        qWarning() << "ChatLLM failed to generate name:" << e.what();
    }
}

void ChatLLM::handleChatIdChanged(const QString &id)
{
    m_llmThread.setObjectName(id);
}

class QuestionResponseHandler : public SimpleResponseHandler {
public:
    using SimpleResponseHandler::SimpleResponseHandler;

protected:
    bool onSimpleResponse(const QString &response) override
    {
        auto responseUtf8Bytes = response.toUtf8().slice(m_offset);
        auto responseUtf8 = std::string(responseUtf8Bytes.begin(), responseUtf8Bytes.end());
        // extract all questions from response
        ptrdiff_t lastMatchEnd = -1;
        auto it = std::sregex_iterator(responseUtf8.begin(), responseUtf8.end(), s_reQuestion);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            auto pos = it->position();
            auto len = it->length();
            lastMatchEnd = pos + len;
            emit m_cllm->generatedQuestionFinished(QString::fromUtf8(&responseUtf8[pos], len));
        }

        // remove processed input from buffer
        if (lastMatchEnd != -1)
            m_offset += lastMatchEnd;
        return true;
    }

private:
    // FIXME: This only works with response by the model in english which is not ideal for a multi-language
    // model.
    // match whole question sentences
    static inline const std::regex s_reQuestion { R"(\b(?:What|Where|How|Why|When|Who|Which|Whose|Whom)\b[^?]*\?)" };

    qsizetype m_offset = 0;
};

void ChatLLM::generateQuestions(qint64 elapsed)
{
    Q_ASSERT(isModelLoaded());
    if (!isModelLoaded()) {
        emit responseStopped(elapsed);
        return;
    }

    auto *mySettings = MySettings::globalInstance();

    QString suggestedFollowUpPrompt = mySettings->modelSuggestedFollowUpPrompt(m_modelInfo);
    if (isAllSpace(suggestedFollowUpPrompt)) {
        qWarning() << "ChatLLM: not generating follow-up questions because prompt is empty";
        emit responseStopped(elapsed);
        return;
    }

    emit generatingQuestions();

    QuestionResponseHandler respHandler(this);

    QElapsedTimer totalTime;
    totalTime.start();
    try {
        // TODO: support interruption via m_stopGenerating
        promptModelWithTools(
            m_llmInstance.get(),
            respHandler, mySettings->modelGenParams(m_modelInfo).get(),
            applyJinjaTemplate(forkConversation(suggestedFollowUpPrompt)).c_str(),
            { ToolCallConstants::ThinkTagName }
        );
    } catch (const std::exception &e) {
        qWarning() << "ChatLLM failed to generate follow-up questions:" << e.what();
    }
    elapsed += totalTime.elapsed();
    emit responseStopped(elapsed);
}

bool ChatLLM::serialize(QDataStream &stream, int version)
{
    static constexpr int VERSION_MIN = 13;
    if (version < VERSION_MIN)
        throw std::runtime_error(fmt::format("ChatLLM does not support serializing as version {} (min is {})",
                                             version, VERSION_MIN));
    // nothing to do here; ChatLLM doesn't serialize any state itself anymore
    return stream.status() == QDataStream::Ok;
}

bool ChatLLM::deserialize(QDataStream &stream, int version)
{
    // discard all state since we are initialized from the ChatModel as of v11
    if (version < 11) {
        union { int intval; quint32 u32; quint64 u64; };

        bool deserializeKV = true;
        if (version >= 6)
            stream >> deserializeKV;

        if (version >= 2) {
            stream >> intval; // model type
            auto llModelType = (version >= 6 ? parseLLModelTypeV1 : parseLLModelTypeV0)(intval);
            if (llModelType == LLModelTypeV1::NONE) {
                qWarning().nospace() << "error loading chat id " << m_chat->id() << ": unrecognized model type: "
                                     << intval;
                return false;
            }

            /* note: prior to chat version 10, API models and chats with models removed in v2.5.0 only wrote this because of
             * undefined behavior in Release builds */
            stream >> intval; // state version
            if (intval) {
                qWarning().nospace() << "error loading chat id " << m_chat->id() << ": unrecognized internal state version";
                return false;
            }
        }

        {
            QString dummy;
            stream >> dummy; // response
            stream >> dummy; // name response
        }
        stream >> u32; // prompt + response token count

        // We don't use the raw model state anymore.
        if (deserializeKV) {
            if (version < 4) {
                stream >> u32; // response logits
            }
            stream >> u32; // n_past
            if (version >= 7) {
                stream >> u32; // n_ctx
            }
            if (version < 9) {
                stream >> u64; // logits size
                stream.skipRawData(u64 * sizeof(float)); // logits
            }
            stream >> u64; // token cache size
            stream.skipRawData(u64 * sizeof(int)); // token cache
            QByteArray dummy;
            stream >> dummy; // state
        }
    }
    return stream.status() == QDataStream::Ok;
}
