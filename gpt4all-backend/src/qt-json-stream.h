#pragma once

#include <boost/json.hpp> // IWYU pragma: keep

#include <QIODevice>
#include <QtTypes>

class QObject;


namespace gpt4all::backend {


class JsonStreamDevice : public QIODevice
{
public:
    explicit JsonStreamDevice(const boost::json::value *jv, QObject *parent = nullptr);

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private:
    boost::json::serializer m_sr;
};


} // namespace gpt4all::backend
