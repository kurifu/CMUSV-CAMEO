#include <string>
#include <sys/time.h>
#include "request.hxx"
#include <iostream>
using namespace std;


void Request::setTimeSent() {
 	time_t rawtime;
        time(&rawtime);
        timeSent = localtime(&rawtime);
        mktime(timeSent);
}

struct tm* Request::getTimeSent() {
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
