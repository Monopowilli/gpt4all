#pragma once

class QRestReply;
class QString;


namespace gpt4all::backend {


QString restErrorString(const QRestReply &reply);


} // namespace gpt4all::backend
