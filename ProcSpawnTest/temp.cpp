#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <iostream>

using namespace std;

int globalVariable = 2;

int main(int argc, char* argv[]) {
	string sIdentifier;
	int iStackVariable = 20;

	pid_t pID = fork();
	if(pID == 0) {
		sIdentifier = "Child Process: ";
		globalVariable++;
		iStackVariable++;
		system("echo \"IM THE CHILD!\"");

		cout << "(Child) Enter your IP Address: ";
		string clientIP = "";
		getline(cin, clientIP);
		cout << "(Child) got your IP, it's " << clientIP << endl;
		const char* netJackCommand = "jack_netsource -H 10.0.9.206"; //+ clientIP;
		system(netJackCommand);
	}
	else if (pID < 0) {
		cout << "Failed to fork" << endl;
		exit(1);
	}
	else {
		sIdentifier = "Parent Process:";

		cout << "(Parent) Enter your IP Address: ";
		string clientIP = "";
		getline(cin, clientIP);
		cout << "(Parent) got your IP, it's " << clientIP << endl;
	}

	cout << sIdentifier;
	cout << " Global variable: " << globalVariable;
	cout << " Stack variable: " << iStackVariable << endl;
}
