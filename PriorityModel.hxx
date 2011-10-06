#ifndef PRIORITY_MODEL
#define PRIORITY_MODEL

#include "request.hxx"

/**
* TODO: use a more sophisticated model for determining a Requests's priority
*/
class PriorityModel {
	public:
	bool operator()(const Request& a, const Request& b) const {
		if(a.getPriority() > b.getPriority())
			return false;
		return true;
	}
};
#endif
