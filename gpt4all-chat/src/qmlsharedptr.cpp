#include "qmlsharedptr.h"

#include <QJSEngine>


namespace gpt4all::ui {


QmlSharedPtr::QmlSharedPtr(std::shared_ptr<QObject> ptr)
    : m_ptr(std::move(ptr))
{ if (m_ptr) { QJSEngine::setObjectOwnership(m_ptr.get(), QJSEngine::CppOwnership); } }


} // namespace gpt4all::ui
