#pragma once

#include <memory>


namespace gpt4all::ui {


/// Helper mixin for classes derived from std::enable_shared_from_this.
template <typename T>
struct Creatable {
    template <typename... Ts>
    static auto create(Ts &&...args) -> std::shared_ptr<T>
    { return std::make_shared<T>(typename T::protected_t(), std::forward<Ts>(args)...); }
};


} // namespace gpt4all::ui
