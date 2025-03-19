#include "qt-json-stream.h"

#include <QtPreprocessorSupport>

#include <cstddef>


namespace json = boost::json;


namespace gpt4all::backend {


JsonStreamDevice::JsonStreamDevice(const json::value *jv, QObject *parent)
    : QIODevice(parent)
{
    m_sr.reset(jv);
    open(QIODevice::ReadOnly);
}

qint64 JsonStreamDevice::readData(char *data, qint64 maxSize)
{
    if (m_sr.done()) return -1; // EOF
    auto chunk = m_sr.read(data, size_t(maxSize));
    return qint64(chunk.size());
}

qint64 JsonStreamDevice::writeData(const char *data, qint64 maxSize)
{
    Q_UNUSED(data)
    Q_UNUSED(maxSize)
    return -1;
}


} // namespace gpt4all::backend
