#include <sys/time.h>
#ifndef REQUEST_H
#define REQUEST_H

#define PRIORITY_HIGH 3
#define PRIORITY_MEDIUM 2
#define PRIORITY_LOW 1

using namespace std;

class Request {
        public:
                void setTimeSent();
		struct timeval getTimeSent();
		void setChannel(int inChannelNum);
		int getChannel() const;
		void setPriority(int inPriority);
		int getPriority() const;
		void setMessage(string inMsg);
		string getMessage() const;
        private:
                int priority;
                struct timeval timeSent;
                int channel;
		string message;
};

#endif
