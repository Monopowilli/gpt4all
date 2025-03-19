#include <fmt/format.h>

#include <QCoro/QCoroTask> // IWYU pragma: keep

#include <QDebug>
#include <QVariant>
#include <QtLogging>

#include <expected>
#include <functional>


namespace gpt4all::ui {


template <typename C, typename F, typename... Args>
    requires (!detail::is_expected<typename std::invoke_result_t<F, C *, Args...>::value_type>)
QCoro::QmlTask wrapQmlTask(C *obj, F f, QString prefix, Args &&...args)
{
    std::shared_ptr<C> ptr(obj->shared_from_this(), obj);
    return [](std::shared_ptr<C> ptr, F f, QString prefix, Args &&...args) -> QCoro::Task<QVariant> {
        co_return QVariant::fromValue(co_await std::invoke(f, ptr.get(), std::forward<Args>(args)...));
    }(std::move(ptr), std::move(f), std::move(prefix), std::forward<Args>(args)...);
}

template <typename C, typename F, typename... Args>
    requires detail::is_expected<typename std::invoke_result_t<F, C *, Args...>::value_type>
QCoro::QmlTask wrapQmlTask(C *obj, F f, QString prefix, Args &&...args)
{
    std::shared_ptr<C> ptr(obj->shared_from_this(), obj);
    return [](std::shared_ptr<C> ptr, F f, QString prefix, Args &&...args) -> QCoro::Task<QVariant> {
        auto result = co_await std::invoke(f, ptr.get(), std::forward<Args>(args)...);
        if (result)
            co_return QVariant::fromValue(*result);
        qWarning().noquote() << prefix << "failed:" << result.error().errorString();
        co_return QVariant::fromValue(nullptr);
    }(std::move(ptr), std::move(f), std::move(prefix), std::forward<Args>(args)...);
}

template <typename C, typename F, typename... Args>
bool wrapQmlFunc(C *obj, F &&f, QStringView prefix, Args &&...args)
{
    auto res = std::invoke(std::forward<F>(f), obj, std::forward<Args>(args)...);
    if (!res) { qWarning().noquote() << prefix << "failed:" << res.error().errorString(); }
    return bool(res);
}

template <typename T, typename S, typename C>
void GenerationParams::tryParseValue(this S &self, QMap<GenerationParam, QVariant> &values, GenerationParam key,
                                     T C::* dest)
{
    if (auto value = tryParseValue(values, key, QMetaType::fromType<T>()); value.isValid())
        self.*dest = value.template value<T>();
}

template <typename T, typename S, typename C>
auto ModelProviderMutable::setMemberProp(this S &self, T C::* member, std::string_view name, T value,
                                         bool create) -> DataStoreResult<>
{
    auto &mpm = static_cast<ModelProviderMutable &>(self);
    auto &cur = self.*member;
    if (cur != value) {
        cur = std::move(value);
        if (mpm.persisted()) {
            auto data = mpm.asData();
            if (auto res = mpm.m_store->setData(std::move(data), mpm.name(), create); !res)
                return res;
        }
        QMetaObject::invokeMethod(self.asQObject(), fmt::format("{}Changed", name).c_str(), cur);
    }
    return {};
}


} // namespace gpt4all::ui
