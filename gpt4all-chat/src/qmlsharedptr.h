#pragma once

#include <QObject>

#include <memory>


namespace gpt4all::ui {


class QmlSharedPtr : public QObject {
    Q_OBJECT

public:
    explicit QmlSharedPtr(std::shared_ptr<QObject> ptr);

    const std::shared_ptr<QObject> &ptr() { return m_ptr; }
    Q_INVOKABLE QObject *get() { return m_ptr.get(); }

private:
    std::shared_ptr<QObject> m_ptr;
};


} // namespace gpt4all::ui
