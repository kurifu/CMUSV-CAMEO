#include <sys/time.h>
#ifndef REQUEST_H
#define FOO_H

class Request {
        public:
                void setTimeSent();
        private:
                int priority;
                struct timeval timeSent;
                int channel;
};

#endif
