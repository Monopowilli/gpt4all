#pragma once

#include "qmlsharedptr.h" // IWYU pragma: keep

#include <QObject>
#include <QQmlEngine>
#include <QString> // IWYU pragma: keep
#include <QUrl> // IWYU pragma: keep


namespace gpt4all::ui {


// The singleton through which all static methods and free functions are called in QML.
class QmlFunctions : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    explicit QmlFunctions() = default;

public:

    static QmlFunctions *create(QQmlEngine *, QJSEngine *) { return new QmlFunctions; }

    Q_INVOKABLE QmlSharedPtr *newCustomOpenaiProvider(QString name, QUrl baseUrl, QString apiKey) const;
    Q_INVOKABLE QmlSharedPtr *newCustomOllamaProvider(QString name, QUrl baseUrl) const;
};


} // namespace gpt4all::ui
