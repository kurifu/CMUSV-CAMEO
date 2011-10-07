#include <string>
#include <sys/time.h>
#include "request.hxx"

void Request::setTimeSent() {
        gettimeofday(&timeSent, 0x0);
}

struct timeval Request::getTimeSent() {
	return timeSent;
}

void Request::setChannel(int inChannelNum) {
	channel = inChannelNum;
}

int Request::getChannel() const {
	return channel;
}

void Request::setPriority(int inPriority) {
	priority = inPriority;
}

int Request::getPriority() const {
	return priority;
}

void Request::setMessage(string inMsg) {
	message = inMsg;
}

string Request::getMessage() const {
	return message;
}
