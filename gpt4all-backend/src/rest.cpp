#include "rest.h"

#include <QNetworkReply>
#include <QRestReply>
#include <QString>

using namespace Qt::Literals::StringLiterals;


namespace gpt4all::backend {


QString restErrorString(const QRestReply &reply)
{
    auto *nr = reply.networkReply();
    if (reply.hasError())
        return nr->errorString();

    if (!reply.isHttpStatusSuccess()) {
        auto code   = reply.httpStatus();
        auto reason = nr->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        return u"HTTP %1%2%3 for URL \"%4\""_s.arg(
            QString::number(code),
            reason.isEmpty() ? QString() : u" "_s,
            reason,
            nr->request().url().toString()
        );
    }

    Q_UNREACHABLE();
}


} // namespace gpt4all::backend
