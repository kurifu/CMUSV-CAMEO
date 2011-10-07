#include "CLAM/Network.hxx"
#include "CLAM/PushFlowControl.hxx"
#include "CLAM/Err.hxx"
#include "CLAM/SimpleOscillator.hxx"
#include "CLAM/AudioMultiplier.hxx"
#include "CLAM/AudioOut.hxx"
#include <iostream>
#include "CLAM/AudioManager.hxx"
#include <CLAM/JACKNetworkPlayer.hxx>
#include <jack/jack.h>
#include "CLAM/OutControlSender.hxx"
#include <CLAM/XMLStorage.hxx>
#include "CLAM/Channelizer.hxx"
#include "CLAM/AudioMixer.hxx"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstdlib>
#include <cerrno>
#include <sstream>

/* Data/Login Libraries */
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fstream>
#include <fcntl.h>

/* Festival */
#include "/usr/include/festival/festival.h"

#include "request.hxx"
#include <queue>
#include "PriorityModel.hxx"

using namespace std;

/*************
* CLAM Setup *
*************/
CLAM::Network network;
jack_client_t * jackClient;
int channelThatHasFloor = -1;
int prevChannelThatHasFloor = -1;
int numTimesFloorChanges = 0;
string logFilePath;

static int SUPERVISOR_ON = 0;
static string LOG_NOTE = "";

#define IS_NOT_TALKING(x)	(x & NOT_TALKING)
#define IS_START_TALKING(x)	(x & START_TALKING)
#define IS_STILL_TALKING(x)	(x & STILL_TALKING)
#define IS_STOP_TALKING(x)	(x & STOP_TALKING)
#define FLOOR_FREE		-1 == channelThatHasFloor


/*********
* Volume *
**********
const static double LOUD = 40.0;
const static double BG_LOUD = -25.0;
const static double SOFT = 20.0;
const static int VU_NUM_UTTERANCES = 1;
const static int VU_NOTSP_NUM_SAMPLES = 500;

/*********************
* Dominance/Dormancy *
*********************/
// Percentage of activity a channel needs to achieve to be considered dominant
const static double DOMINANCE_THRESHOLD = 45;

// Number of seconds an overlap can occur before we take action
const static double OVERLAP_LENGTH = .5;

// Maximum number of channels we can support, as determined by the CLAM network layout
const int NUMCHANNELS = 4;

// Maximum percentage of activity a channel needs to achieve to be considered dormant
const static double DORMANCY_PERCENTAGE = 30;

// Number of seconds before we encourage the most dormant participant to speak up
const static double DORMANCY_INTERVAL = 60;

double currOverlapLength = 0;
bool isOverlapping = false;

struct timeval _currTime, _beepTimeDiff, _overlapStartTime, _dormancyInterval, _dormancyIntervalHolder;
double diffTime = 0.0;

/**************************
* Reinforcement Scheduler *
**************************/
// Priority queue of Request objects, note that PriorityModel is the function object needed for comparison defined in PriorityModel.hxx
static priority_queue<Request, vector<Request>, PriorityModel> requestQ;

/***********
* Festival *
***********/
const static string TTS_GAIN_PORT = "Gain 4";
const static double TTS_GAIN = 1.5;
const int heap_size = 21000000;  // default scheme heap size
const int load_init_files = 1; // we want the festival init files loaded
const int worked = 0;
/*const static string TTS_TOO_SOFT= "You're too Soft";
const static string TTS_TOO_LOUD = "You're too Loud";
const static string TTS_BG_TOO_LOUD = "Background noise too loud";
*/
const static string TTS_DORMANCY = "Your thoughts ?";
const static string TTS_DOMINANCE = "Take turns";
/********************
* Background Tracks *
********************/
const static string BG_GAIN_PORT = "Gain 5";

/********
* Login *
********/
static int mySocketFD;
int CURRNUMCHANNELS = 0;
static int LISTEN_PORT = 4444;

inline void initializeSocket() {
        mySocketFD = socket(AF_INET, SOCK_STREAM, 0);

        int optval = 1;
        setsockopt(mySocketFD, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        if(mySocketFD < 0) {
                cout << "ERROR opening socket on port " << LISTEN_PORT << endl;
                return;
        }
}

int checkIfOn(CLAM::Channelizer* channels[] ) {
        ifstream inputFile;
        inputFile.open("./config.txt");
        int data;
	string data2;
        inputFile >> data;

        ofstream logFile;
        logFile.open(logFilePath.c_str(), ios::app);
        if(SUPERVISOR_ON != data) {
                cout << "Supervisor is now ";
                (data) ? cout << "on!" << endl : cout << "off!" << endl;

		for(int i = 0; i < NUMCHANNELS; i++) {
			double avgSpkLen = channels[i]->totalSpeakingLength / channels[i]->numTimesTakenFloor;
			logFile << endl << channels[i]->getPName() << " Dom%: " << channels[i]->totalActivityLevel << "\tAvg Speaking Length: " << avgSpkLen;
		}

		logFile << endl << endl;
                logFile << "Supervisor turned";
		(data) ? logFile << " ON " : logFile << " OFF " << endl;
                logFile << "CurrTime\t\tChannelName\tSpeaking Length\tTSL\t\tTSLNoU\t\tTSI\tDom%\t\tIsDom\tAvgSpEnergy\tAvgNSpEnergy\t#Spoken\t#Notified\tTotalTime\n";
                SUPERVISOR_ON = data;

		for(int i = 0; i < NUMCHANNELS; i++) {
			channels[i]->reset();
		}
        }

	// If the 2nd line in the config.txt file changes, calculate all stats up until now and 
	inputFile >> data2;
	if(data2.compare(LOG_NOTE)) {
		logFile << data2 << endl;
		LOG_NOTE = data2;
	}
        logFile.close();

        inputFile.close();
        return SUPERVISOR_ON;
}

/**
* Finds the IP address of the client connected at the provided socket file descriptor
* Returns: char* containing ip, or NULL
*/
char* getSocketPeerIp(int sock) {
        struct sockaddr_storage ss;
        socklen_t salen = sizeof(ss);
        struct sockaddr *sa;
        memset(&ss, 0, salen);
        sa = (sockaddr*) &ss;
        if(getpeername(sock, sa, &salen) != 0) {
                return NULL;
        }

        char* ip = NULL;
        if(sa->sa_family == AF_INET) {
                ip = inet_ntoa( ((struct sockaddr_in *) sa)->sin_addr);
        }
        else {
                cout << "IPV6 problem?" << endl;
        }
        return ip;
}

/**
* Function that handles incoming connections at LISTEN_PORT by blocking
* This function runs in a separate thread called by runLoginManager
*/
void* connect(void* portno) {
//void connect(int portno) {
        //while(1) {
		//cout << "* Connect thread listening on port " << (int)portno << endl;
                socklen_t clientAddrLen;
                struct sockaddr_in serverAddr, clientAddr;
                int clientSocketFD;

                initializeSocket();
                bzero((char*) &serverAddr, sizeof(serverAddr));

                // Initialize members of struct sockaddr_in
                // INADDR_ANY gets the address of the machine the server is running on
                // need to convert port number to network byte order via htons
                serverAddr.sin_family = AF_INET;
                serverAddr.sin_addr.s_addr = INADDR_ANY;
                //serverAddr.sin_port = htons(LISTEN_PORT);
                serverAddr.sin_port = htons((int)portno);

                if(bind(mySocketFD, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0) {
                        cout << "Still doesn't work on port " << portno << endl;
			//return;
			pthread_exit(NULL);
		}

                cout << "Waiting for incoming client on port " << (int)portno << endl;
                flush(cout);
                // 2nd arg: size of backlog queue, 5 for now, really only need 1
                listen(mySocketFD, 5);
                clientAddrLen = sizeof(clientAddr);

                clientSocketFD = accept(mySocketFD, (struct sockaddr*) &clientAddr, &clientAddrLen);
                cout << "\t... connected!" << endl;
                if(clientSocketFD < 0) {
                        cout << "ERROR on accepting connection, returning..." << endl;
			//return;
			pthread_exit(NULL);
                }
		else {
	                char* ip = getSocketPeerIp(clientSocketFD);
                        CURRNUMCHANNELS++;
                        string command = "jack_netsource -H " + (string)ip + " &";
                        system(command.c_str());

			Request *r = new Request();
			r->setTimeSent();
			r->setChannel(NUMCHANNELS);
			r->setPriority(4);
			r->setMessage("Someone entered");
			requestQ.push(*r);

			sleep(1);

			if(CURRNUMCHANNELS == 1) {
				cout << "Hooking up 2nd channel with ip " << ip << endl;
				jack_connect(jackClient, "netjack:capture_1", "client1:AudioSource_2");
				jack_connect(jackClient, "netjack:capture_2", "client1:AudioSource_2");
				jack_connect(jackClient, "client1:AudioSink_2", "netjack:playback_1");
				jack_connect(jackClient, "client1:AudioSink_2", "netjack:playback_2");
			}
			else if(CURRNUMCHANNELS == 2) {
				cout << "Hooking up 3rd channel with ip " << ip << endl;
				jack_connect(jackClient, "netjack-01:capture_1", "client1:AudioSource_3");
				jack_connect(jackClient, "netjack-01:capture_2", "client1:AudioSource_3");
				jack_connect(jackClient, "client1:AudioSink_3", "netjack-01:playback_1");
				jack_connect(jackClient, "client1:AudioSink_3", "netjack-01:playback_2");
			}
			else if(CURRNUMCHANNELS == 3) {
				cout << "Hooking up 4th channel with ip " << ip << endl;
				jack_connect(jackClient, "netjack-02:capture_1", "client1:AudioSource_4");
				jack_connect(jackClient, "netjack-02:capture_2", "client1:AudioSource_4");
				jack_connect(jackClient, "client1:AudioSink_4", "netjack-02:playback_1");
				jack_connect(jackClient, "client1:AudioSink_4", "netjack-02:playback_2");
			}

                close(clientSocketFD);
                close(mySocketFD);
        }
	//LISTEN_PORT++;
	pthread_exit(NULL);
}

/**
* Spawns off a thread to run the 'connect' function which handles incoming socket connecitons
*/
void runLoginManager() {
        pthread_t connect1, connect2, connect3, connect4;
        //char* dummyMsg = "return msg?";
	int port1 = 4444;
	int port2 = 4445;
	int port3 = 4446;
	int port4 = 4447;

        int retval_connect1, retval_connect2, retval_connect3, retval_connect4;

        cout << "Initializing Connect 1..." << endl;
        retval_connect1 = pthread_create(&connect1, NULL, connect, (void*)port1);
        cout << "Initializing Connect 2..." << endl;
        retval_connect2 = pthread_create(&connect2, NULL, connect, (void*)port2);
        cout << "Initializing Connect 3..." << endl;
        retval_connect3 = pthread_create(&connect3, NULL, connect, (void*)port3);
        cout << "Initializing Connect 4..." << endl;
        retval_connect4 = pthread_create(&connect4, NULL, connect, (void*)port4);
        //pthread_join(thread_connect, NULL);


/*	pid_t pID1 = fork();
	pid_t pID2 = fork();
	pid_t pID3 = fork();
	pid_t pID4 = fork();
	cout << "pid1 is " << pID1 << ", pid2 is " << pID2 << ", pid3 is " << pID3 << ", pid4 is " << pID4 << endl;
	if(pID1 == 0) {
		cout << "Waiting for 1st Connection on port 4444" << endl;
		connect(4444);	
		cout << "** got a connection on port 4444, exiting." << endl;
		_Exit(1);
	}
	if(pID2 == 0) {
		cout << "Waiting for 2nd Connection on port 4445" << endl;
		connect(4445);	
		cout << "** got a connection on port 4445, exiting." << endl;
		_Exit(1);
	}
	if(pID3 == 0) {
		cout << "Waiting for 3rd Connection on port 4446" << endl;
		connect(4446);	
		cout << "** got a connection on port 4446, exiting." << endl;
		_Exit(1);
	}
	if(pID4 == 0) {
		cout << "Waiting for 1st Connection on port 4447" << endl;
		connect(4447);	
		cout << "** got a connection on port 4447, exiting." << endl;
		_Exit(1);
	}
*/
}

int error(const string & msg) {
	cout << msg << endl;
	return -1;
}

// Determines if each person is dominant or not based on their total Activity Level (how long they've been talking)
inline void calculateDominance(CLAM::Channelizer* channels[]) {
	double totalTSL = channels[0]->totalSpeakingLength + channels[1]->totalSpeakingLength + channels[2]->totalSpeakingLength + channels[3]->totalSpeakingLength;
	for(int i = 0; i < NUMCHANNELS; i++) {
		channels[i]->totalActivityLevel = channels[i]->totalSpeakingLength / totalTSL * 100;
		(channels[i]->totalActivityLevel >= DOMINANCE_THRESHOLD && channels[i]->totalActivityLevel < 100) ? channels[i]->isDominant = true : channels[i]->isDominant = false;
	}
}

// Cliff: this is outdated code written by Peter. This is for volume normalization, we may or may not want this, less so if we focus on feedback-based approach
// PGAO AMP ADJUST
// Volume Control
void adjustAmps(CLAM::Channelizer* channels[], CLAM::Processing* amps[]){
	int j=0.5;
	for(int i=0; i < NUMCHANNELS; i++){
		if(IS_START_TALKING(channels[i]->state)||IS_STILL_TALKING(channels[i]->state)){
			while(channels[i]->logEnergy<50 && (IS_START_TALKING(channels[i]->state) || (IS_STILL_TALKING(channels[i]->state)))) {
				cout << "Increasing volume" << endl;
				j+=0.1;
				CLAM::SendFloatToInControl(*(amps[i]), "Gain", j);
			}
			while(channels[i]->logEnergy>60 && (IS_START_TALKING(channels[i]->state) || (IS_STILL_TALKING(channels[i]->state)))) {
				cout << "Decreasing volume" << endl;
				j-=0.1;
				CLAM::SendFloatToInControl(*(amps[i]), "Gain", j);
			}
		}
	}
}

// Cliff: this is outdated code, written by Peter and Christine. We use bg tracks differently now
inline void playTracks(CLAM::Channelizer* channels[], CLAM::Processing* tracks[]) {
	while(true) {
		// Nobody speaking
		while(IS_NOT_TALKING(channels[0]->state) && IS_NOT_TALKING(channels[1]->state) && IS_NOT_TALKING(channels[2]->state) && IS_NOT_TALKING(channels[3]->state)) {
			for(int i = 0; i < NUMCHANNELS; i++) {
				CLAM::SendFloatToInControl(*(tracks[i]), "Gain", 5.0);
			}
		}

		//Person speaking
		for(int i = 0; i < NUMCHANNELS; i++) {
			if(IS_START_TALKING(channels[i]->state)) {
				CLAM::SendFloatToInControl(*(tracks[i]), "Gain", 5.0);
			}
			else if(IS_STILL_TALKING(channels[i]->state) || IS_STOP_TALKING(channels[i]->state)) {
				CLAM::SendFloatToInControl(*(tracks[i]), "Gain", 5.0);
			}
			else {
				CLAM::SendFloatToInControl(*(tracks[i]), "Gain", 0.0);
			}
		}
	}
}

/**
* If channel is the maximum number of channels, send TTS to every channel, otherwise only send to the channel specified by arg 'channel'
*/
void textToSpeech(string msg, int channel, CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {
	cout << "Inside textToSpeech, message is " << msg <<  endl;
	EST_Wave wave;

	CLAM::Processing& tts = network.GetProcessing("TTS");
	festival_text_to_wave(msg.c_str(), wave);
	//festival_text_to_wave(msg, wave);
        wave.save("/home/rahul/Multiparty/Projects/multipartyspeech/wave.wav","riff");

	if(channel == NUMCHANNELS) {
		CLAM::SendFloatToInControl(*(mixers[0]), TTS_GAIN_PORT, TTS_GAIN);
		CLAM::SendFloatToInControl(*(mixers[1]), TTS_GAIN_PORT, TTS_GAIN);
		CLAM::SendFloatToInControl(*(mixers[2]), TTS_GAIN_PORT, TTS_GAIN);
		CLAM::SendFloatToInControl(*(mixers[3]), TTS_GAIN_PORT, TTS_GAIN);
	}
	else {
		for(int i = 0; i < NUMCHANNELS; i++) {
			if(i == channel) {
				CLAM::SendFloatToInControl(*(mixers[i]), TTS_GAIN_PORT, TTS_GAIN);
				channels[i]->numTimesNotified++;
			}
			else {
				CLAM::SendFloatToInControl(*(mixers[i]), TTS_GAIN_PORT, 0.0);
			}
		}
	}
	CLAM::SendFloatToInControl(tts, "Seek", 0.0);
}

// Beeps any channels that have been overlapping for at least 5 seconds and do not have the floor
// Stops beeping after 3 seconds and starts all over again
inline void adjustAlerts(CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {

	int channelToAlert = -1;
	int lowestActivityLevel = 100;

	gettimeofday(&_currTime, 0x0);
	channels[channelThatHasFloor]->timeval_subtract(&_dormancyIntervalHolder, &_currTime, &_dormancyInterval);
	double diff = (double)_dormancyIntervalHolder.tv_sec + (double)0.001*_dormancyIntervalHolder.tv_usec/1000;
	if(diff >= DORMANCY_INTERVAL) {
		for(int i = 0; i < NUMCHANNELS; i++) {
			if((channels[i]->totalActivityLevel <= DORMANCY_PERCENTAGE) && (channels[i]->totalActivityLevel < lowestActivityLevel) && (channels[i]->totalActivityLevel != 0) && (IS_NOT_TALKING(channels[i]->state))) {
				channelToAlert = i;
			}
		}
		// TTS Dormancy alert
		Request* r2 = new Request();
		r2->setTimeSent();
		r2->setChannel(channelToAlert);
		r2->setPriority(10);
		r2->setMessage(TTS_DORMANCY);
		requestQ.push(*r2);
		gettimeofday(&_dormancyInterval, 0x0);
	}
	
        for(int i = 0; i < NUMCHANNELS; i++) {
//TODO
		if(channels[i]->isGonnaGetBeeped) {
			Request* r1 = new Request();
			// TTS Dominance alert
			r1->setTimeSent();
			r1->setChannel(i);
			r1->setPriority(1);
			r1->setMessage(TTS_DOMINANCE);
			requestQ.push(*r1);
			channels[i]->isGonnaGetBeeped = false;
                }
        }

}

// Looks at each Speaker State, updates each channel's Floor Action State
void updateFloorActions(CLAM::Channelizer* channels[]) {
	for(int i = 0; i < NUMCHANNELS; i++) {
		if(IS_START_TALKING(channels[i]->state)) {
			channels[i]->floorAction = TAKE_FLOOR;
		}
		else if (IS_STILL_TALKING(channels[i]->state)) {
			channels[i]->floorAction = HOLD_FLOOR;
		}
		else if (IS_STOP_TALKING(channels[i]->state)) {
			channels[i]->floorAction = RELEASE_FLOOR;
		}
		else if (IS_NOT_TALKING(channels[i]->state)) {
			channels[i]->floorAction = NO_FLOOR;
		}
	}
}

// Find the number of people who are speaking now
inline int findNumSpeakers(CLAM::Channelizer* channels[]) {
	int speakers = 0;
	for(int i = 0; i < NUMCHANNELS; i++) {
		if(IS_TAKE_FLOOR(channels[i]->floorAction) || IS_HOLD_FLOOR(channels[i]->floorAction))
			speakers++;
	}
	return speakers;
}



inline void giveFloorToLeastDominantGuy(CLAM::Channelizer* channels[] ) {
	//cout << "giveFloorToLeastDominantGuy" << endl;
	short channelThatIsLeastDominant = channelThatHasFloor;
	ostringstream oss;

	for(int i = 0; i < NUMCHANNELS; i++) {
		// If you're talking, we'll look at your activity levels, if you haven't been active, you get floor
		if(IS_TAKE_FLOOR(channels[i]->floorAction) || IS_HOLD_FLOOR(channels[i]->floorAction)) {
			if(channels[i]->totalActivityLevel < channels[channelThatIsLeastDominant]->totalActivityLevel) {
				channelThatIsLeastDominant = i;
			}
		}
	}
}


// Looks at each Floor Action, updates the global Floor State
string updateFloorState(CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {
	string outputMsg;
	ostringstream oss;

	int numSpeakers = findNumSpeakers(channels);

	// Case 1: No one has floor, only 1 person talking
	// 	   Figure out who to give it to
	if(FLOOR_FREE) {
		//cout << "Floor is free!" << endl;
		int numWhoWantFloor = 0;
		short channelWhoWantsFloor = -1;
		for(int i = 0; i < NUMCHANNELS; i++) {
			if(IS_TAKE_FLOOR(channels[i]->floorAction)) {
				channelWhoWantsFloor = i;
				numWhoWantFloor++;
			}
		}

		if(numWhoWantFloor == 1) {
			channelThatHasFloor = channelWhoWantsFloor;
			if(prevChannelThatHasFloor != channelThatHasFloor) {
				numTimesFloorChanges++;
			}
			prevChannelThatHasFloor = channelThatHasFloor;

			channels[channelThatHasFloor]->numTimesTakenFloor++;
			oss << (channelThatHasFloor+1) << ", Floor has changed " << numTimesFloorChanges << " times";
			outputMsg = "Giving Floor to Channel " + oss.str();
			isOverlapping = false;

	                //ofstream logFile;
	                //logFile.open(logFilePath.c_str(), ios::app);
	                //logFile << "\tFloor Changed " << numTimesFloorChanges << " times\n";
	                //logFile.close();

		}
		else {
			// More than 1 guy started at the same time; very very rare case, deal with it later
		}
	}
	// Case 2: Someone has floor; check everyone else's status before updating anything
	else {
		//cout << "Floor is NOT free!" << endl;
		// Case 2.1: Only 1 guy talking and holding the floor, most common case, let him continue
		if(IS_HOLD_FLOOR(channels[channelThatHasFloor]->floorAction) && (1 == numSpeakers)) {
			isOverlapping = false;
		}

		// Case 2.2: 1 guy talking and holding the floor, 1 or more guys interrupt him: BARGE IN
		// 	If there is an overlap for longer than OVERLAP_LENGTH, mark everyone who does not 
		// 	have the floor and is talking; whoever is marked will get beeped
		else if(IS_HOLD_FLOOR(channels[channelThatHasFloor]->floorAction) && (1 != numSpeakers)) {
			//cout << "BARGE IN! " << endl;
			for (int i = 0; i < NUMCHANNELS; i++) {
				if((i != channelThatHasFloor) && (IS_HOLD_FLOOR(channels[i]->floorAction))) {
					// Only log if we haven't logged yet; this gets reset automatically by the Channelizer
					if(channels[i]->interruptLogged == false) {
						channels[i]->totalSpeakingInterrupts++;
						channels[i]->interruptLogged = true;
					}
				}
			}

			//if(channels[channelThatHasFloor]->isDominant)
			//	outputMsg = giveFloorToLeastDominantGuy(channels);

			if(!isOverlapping) {
				gettimeofday(&_overlapStartTime, 0x0);
				isOverlapping = true;
			}

			gettimeofday(&_currTime, 0x0);
			channels[channelThatHasFloor]->timeval_subtract(&_beepTimeDiff, &_currTime, &_overlapStartTime);
			diffTime = (double)_beepTimeDiff.tv_sec + (double)0.001*_beepTimeDiff.tv_usec/1000;
			currOverlapLength = diffTime;
			if(currOverlapLength >= OVERLAP_LENGTH && isOverlapping) {
				cout << "overlapped for more than 2 sec" << endl;
				
				currOverlapLength = 0.0;
				isOverlapping = false;	// may not be a good var name

				// Mark everyone who is talking that doesn't have the floor
				for (int i = 0; i < NUMCHANNELS; i++) {
					// Dominant Case
					if(((i == channelThatHasFloor) && (channels[i]->isDominant) ) || ((channels[i]->isDominant) && (i != channelThatHasFloor))) {
						channels[i]->isGonnaGetBeeped = true;
//TODO
					}
				}
			}
		}
		// When someone's done talking, play their BG track
		/*else if (0 == numSpeakers) {
			channelThatHasFloor = -1;
			if(checkIfOn(channels)) {
			//if(SUPERVISOR_ON) {
				for(int i = 0; i < NUMCHANNELS; i++) {
					if(IS_STOP_TALKING(channels[i]->state)) {
	
						//Turn everyone's BG channel on
						CLAM::SendFloatToInControl(*(mixers[0]), BG_GAIN_PORT, 1.0);
						CLAM::SendFloatToInControl(*(mixers[1]), BG_GAIN_PORT, 1.0);
						CLAM::SendFloatToInControl(*(mixers[2]), BG_GAIN_PORT, 1.0);
						CLAM::SendFloatToInControl(*(mixers[3]), BG_GAIN_PORT, 1.0);

						//cout << "Channel " << channels[i]->channelNum << " just stopped talking" << endl;

						string track;
						if(i == 0)
							track = "Track";
						else if(i == 1) {
							track = "Track_1";
						}
						else if(i == 2) {
							track = "Track_2";
						}
						else if(i == 3) {
							track = "Track_3";
						}
					
	       	 				CLAM::Processing& targetTrack = network.GetProcessing(track);
	        				CLAM::SendFloatToInControl(targetTrack, "Seek", 0.0);
						//flush(cout);
						//cout << "\nTarget track is " << targetTrack << ", target seek is " << targetSeek << endl << endl;
						//flush(cout);
					}
				}
			}
		}*/
	}

	return outputMsg;
}

/**
* Determines when to play a background tone for each channel
* Plays a tone as specified in the CLAM network file when someone starts talking
*/
void playBgTones(CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {
	if(checkIfOn(channels)) {
		//if(SUPERVISOR_ON) {
		for(int i = 0; i < NUMCHANNELS; i++) {
			if(IS_START_TALKING(channels[i]->state)) {
	
				//Turn everyone's BG channel on
				CLAM::SendFloatToInControl(*(mixers[0]), BG_GAIN_PORT, 1.0);
				CLAM::SendFloatToInControl(*(mixers[1]), BG_GAIN_PORT, 1.0);
				CLAM::SendFloatToInControl(*(mixers[2]), BG_GAIN_PORT, 1.0);
				CLAM::SendFloatToInControl(*(mixers[3]), BG_GAIN_PORT, 1.0);

				string track;
				if(i == 0) {
					track = "Track";
				}
				else if(i == 1) {
					track = "Track_1";
				}
				else if(i == 2) {
					track = "Track_2";
				}
				else if(i == 3) {
					track = "Track_3";
				}

				CLAM::Processing& targetTrack = network.GetProcessing(track);
				CLAM::SendFloatToInControl(targetTrack, "Seek", 0.0);
			}
		}
	}
}

/* 
* Checks the background noise level of each channel and their speaking energy level
* Generates an alert if:
* 1. Background noise is above BG_NOISE_THRESHOLD
* 2. SNR is low
* 3. SNR is high

string checkSoundLevels(CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {
        ostringstream oss;

	for(int i = 0; i < NUMCHANNELS; i++) {
		// If they're speaking too soft
		if(channels[i]->counterVu == VU_NUM_UTTERANCES) {
			if(channels[i]->avgVuMeter > LOUD) {
				// alert you're too loud
				oss << "Channel " << i << " is too loud at " << channels[i]->avgVuMeter << endl;
	 	               	//textToSpeech("You're too loud", i, channels, mixers);
				Request* r3 = new Request();
				r3->setTimeSent();
				r3->setChannel(i);
				r3->setPriority(1);
				r3->setMessage(TTS_TOO_LOUD);
				//requestQ.push(*r3);
				channels[i]->pushInternalQ(*r3);

			}
			else if(channels[i]->avgVuMeter < SOFT) {
				// alert you're too soft
				oss << "Channel " << i << " is too soft" << endl;
		                //textToSpeech("You're too soft", i, channels, mixers);
				Request* r4 = new Request();
				r4->setTimeSent();
				r4->setChannel(i);
				r4->setPriority(1);
				r4->setMessage(TTS_TOO_SOFT);
				requestQ.push(*r4);

			}
			channels[i]->avgVuMeter = 0.0;
			channels[i]->vuMeter = 0.0;
			channels[i]->counterVu = 0;
		}

		//if(i == 0) 
		//	cout << "Not sp energy is " << channels[i]->avgNotSpeakingEnergy << endl;

		// check everyone's background noise constantly
		if(channels[i]->energyNotSpeakingCount >= VU_NOTSP_NUM_SAMPLES) {
			if(channels[i]->avgNotSpeakingEnergy >= BG_LOUD) {
				oss << "Channel " << i << " background noise is too loud at " << channels[i]->avgNotSpeakingEnergy << endl;
		                //textToSpeech("Background noise too loud", NUMCHANNELS, channels, mixers);
				Request* r5 = new Request();
				r5->setTimeSent();
				r5->setChannel(i);
				r5->setPriority(1);
				r5->setMessage(TTS_BG_TOO_LOUD);
				requestQ.push(*r5);
			}
			channels[i]->totalNotSpeakingEnergy = 0.0;
			channels[i]->energyNotSpeakingCount = 0;
		}
	}
	return oss.str();
}*/



//TODO
void processRequests(CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {
        while(!requestQ.empty()) {
                cout << "Request Priority: " << requestQ.top().getPriority() << " for Channel " << requestQ.top().getChannel() << ", number of msgs in Q:" << requestQ.size() << endl;
		string msg = (string)(requestQ.top()).getMessage();
		cout << "\tmessgae: " << msg << endl; 
        	textToSpeech(msg, requestQ.top().getChannel(), channels, mixers);
		//textToSpeech("Take turns", 0, channels, mixers);		
		requestQ.pop();
        }
}

/**
* Main steps the Supervisor takes
*/

string updateFloorStuff(CLAM::Channelizer* channels[], string prevMsg, CLAM::Processing* mixers[]) {
	string notifyMsg = "";

	// Process any Request objects we have
	processRequests(channels, mixers);

	// Calculates the activity level for each channel
	calculateDominance(channels);

	// If Supervisor is on, send out alerts to dominant channels
	if(checkIfOn(channels))
		adjustAlerts(channels, mixers);
	
	// See who started talking, play their track
	playBgTones(channels, mixers);

	//notifyMsg = checkSoundLevels(channels, mixers);

	updateFloorActions(channels);

	// Figure out if we have any overlaps or barge-ins
	notifyMsg += updateFloorState(channels, mixers);


	if(("" != notifyMsg) && (prevMsg != notifyMsg)) {
		cout << notifyMsg << endl;
	}
	return notifyMsg;
}

/*******************************************************************/
/*-----------------------SUPERVISOR STUFF--------------------------*/
/*******************************************************************/

/*
1) Detect when overlap happens: IS_TALKING(myp.state) on more than one channel
        solution steered towards increasing intelligibility

2) Who barged-in: How is natural turn-taking different from barge-in?
        while someone IS_TALKING, detect START_TALKING on another channel

3) Concept of Floor: Untill someone else starts speaking, floor belongs to last speaker
*/

int main( int argc, char* argv[] ) {	
	cout << "Welcome to the Supervisor Conference Call system. All log files are saved in ./logs" << endl;

	if(argc < 2) {
		perror("Please specify file name for the logfile, i.e. './MyProgram test1.log', or './MyProgram 20110815_test1.log'\n");
		exit(-1);
	}

	try {

/*Request r1;
r1.setPriority(10);
Request r2;
r2.setPriority(1);
Request r3;
r3.setPriority(7);
requestQ.push(r3);
requestQ.push(r2);
requestQ.push(r1);
while(!requestQ.empty()) {
	cout << requestQ.top().getPriority() << endl;
	requestQ.pop();
}

exit(1);*/

/*******************************************************************/
/*-----------------------------SETUP-------------------------------*/
/*******************************************************************/
		// These values will be used in some configurations, so we declare it now.
		int SIZE = 128;
		int SAMPLERATE = 16000;

		// We need to deploy the audio manager class in order to get audio sound.
		CLAM::AudioManager manager( SAMPLERATE, SIZE );
		manager.Start();

		//CLAM::Network network;
		network.SetPlayer(new CLAM::JACKNetworkPlayer("client1"));

		try {
			CLAM::XMLStorage::Restore(network, "/home/rahul/Multiparty/Projects/multipartyspeech/windowing.clamnetwork");
			//CLAM::XMLStorage::Restore(network, argv[1]);			
		}
		catch (CLAM::XmlStorageErr & e) {
			return error("Could not open the network file");
		}

		CLAM::Processing& mixer1 = network.GetProcessing("AudioMixer");
		CLAM::Processing& mixer2 = network.GetProcessing("AudioMixer_1");
		CLAM::Processing& mixer3 = network.GetProcessing("AudioMixer_2");
		CLAM::Processing& mixer4 = network.GetProcessing("AudioMixer_3");
		CLAM::SendFloatToInControl(mixer1, "Gain 3",0.0);
		CLAM::SendFloatToInControl(mixer2, "Gain 3",0.0);
		CLAM::SendFloatToInControl(mixer3, "Gain 3",0.0);
		CLAM::SendFloatToInControl(mixer4, "Gain 3",0.0);

		// Volume Adjust
		CLAM::Processing& amp0 = network.GetProcessing("Amp");
		CLAM::Processing& amp1 = network.GetProcessing("Amp_1");
		CLAM::Processing& amp2 = network.GetProcessing("Amp_2");
		CLAM::Processing& amp3 = network.GetProcessing("Amp_3");
		CLAM::Processing* amps[4] = {&amp0, &amp1, &amp2, &amp3};
		for(int i = 0; i < NUMCHANNELS; i++) {
			CLAM::SendFloatToInControl(*(amps[i]), "Gain", 0.5);
		}


		CLAM::Processing& generator = network.GetProcessing("Generator");


		CLAM::Processing& mic = network.GetProcessing("AudioSource");
		CLAM::Channelizer& myp1 = (CLAM::Channelizer&) network.GetProcessing("Channelizer");
		CLAM::Channelizer& myp2 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_1");
		CLAM::Channelizer& myp3 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_2");
		CLAM::Channelizer& myp4 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_3");

		myp1.SetPName("Channel 1");
		myp2.SetPName("Channel 2");
		myp3.SetPName("Channel 3");
		myp4.SetPName("Channel 4");		

		myp1.setGlobalQ(&requestQ);
		myp2.setGlobalQ(&requestQ);
		myp3.setGlobalQ(&requestQ);
		myp4.setGlobalQ(&requestQ);

		myp1.channelNum = 1;
		myp2.channelNum = 2;
		myp3.channelNum = 3;
		myp4.channelNum = 4;
		// Data Logging Stuff
		string filePath = argv[1];
		filePath = "/home/rahul/Multiparty/Projects/multipartyspeech/logs/" + filePath;
		logFilePath = filePath;
/*		myp1.setFileName(logFilePath);
		myp2.setFileName(logFilePath);
		myp3.setFileName(logFilePath);
		myp4.setFileName(logFilePath);*/
		ofstream logFile;
		logFile.open(filePath.c_str(), ios::app);
		logFile << "New Test Started At " << myp1.getDate() << "\n";
		logFile << "CurrTime\t\tChannelName\tSpeaking Length\tTSL\t\tTSLNoU\t\tTSI\tDom%\t\tIsDom\tAvgSpEnergy\tAvgNSpEnergy\t#Spoken\t#Notified\tTotalTime\n";
		logFile.close();

		int winSize = mic.GetOutPort("1").GetSize();
		myp1.GetInPort("Input").SetSize(winSize);	
		myp2.GetInPort("Input").SetSize(winSize);	
		myp3.GetInPort("Input").SetSize(winSize);	
		myp4.GetInPort("Input").SetSize(winSize);	
		
		//JACK CODE
         	//jack_client_t * jackClient;
         	string jackClientName;
		jack_status_t jackStatus;
		jackClient = jack_client_open ( "test", JackNullOption, &jackStatus );
		
		network.Start();
		
		jack_connect(jackClient, "system:capture_1", "client1:AudioSource_1");
		jack_connect(jackClient, "dev2:capture_1", "client1:AudioSource_2");
		jack_connect(jackClient, "netjack:capture_1", "client1:AudioSource_3");
		jack_connect(jackClient, "client1:AudioSink_2", "dev2:playback_1");
		jack_connect(jackClient, "client1:AudioSink_2", "dev2:playback_2");
		jack_connect(jackClient, "client1:AudioSink_1", "system:playback_1");
		jack_connect(jackClient, "client1:AudioSink_1", "system:playback_2");
		jack_connect(jackClient, "client1:AudioSink_3", "netjack:playback_1");
		jack_connect(jackClient, "client1:AudioSink_3", "netjack:playback_2");

		// Notify user only when something has changed, i.e. floor changes, collisions, etc.
		string prevMsg = "";

		CLAM::Channelizer* channels[4];
		channels[0] = &myp1;
		channels[1] = &myp2;
		channels[2] = &myp3;
		channels[3] = &myp4;

		CLAM::Processing* mixers[4];
		mixers[0] = & mixer1;
		mixers[1] = & mixer2;
		mixers[2] = & mixer3;
		mixers[3] = & mixer4;

		CLAM::SendFloatToInControl(*(mixers[0]), BG_GAIN_PORT, 0.0);
		CLAM::SendFloatToInControl(*(mixers[1]), BG_GAIN_PORT, 0.0);
		CLAM::SendFloatToInControl(*(mixers[2]), BG_GAIN_PORT, 0.0);
		CLAM::SendFloatToInControl(*(mixers[3]), BG_GAIN_PORT, 0.0);

		gettimeofday(&_currTime, 0x0);
		gettimeofday(&_beepTimeDiff, 0x0);
		gettimeofday(&_dormancyInterval, 0x0);
		runLoginManager();

   		/*int heap_size = 21000000;  // default scheme heap size
    		int load_init_files = 1; // we want the festival init files loaded
		int worked = 0;*/


    		festival_initialize(load_init_files,heap_size);
	        textToSpeech("Welcome to the Supervisor Conference Call System", NUMCHANNELS, channels, mixers);


/*while(1) {
 	textToSpeech("Any thoughts ?", 0, channels, mixers);
	sleep(2);
 	textToSpeech("Your thoughts ?", 0, channels, mixers);
	sleep(2);
 	textToSpeech("Take turns", 0, channels, mixers);
	sleep(2);
 	textToSpeech("Speak up", 0, channels, mixers);
	sleep(2);
 	textToSpeech("Any thoughts ?", 0, channels, mixers);
	sleep(2);
 	textToSpeech("Any ideas ?", 0, channels, mixers);
	sleep(2);
}*/

			//Request* r1 = new Request();
                        //r1->setTimeSent();
                        //r1->setChannel(0);
                        //r1->setPriority(10);
                        //r1->setMessage("Take turns");

		while(1) {		
			prevMsg = updateFloorStuff(channels, prevMsg, mixers);
			//adjustAmps(channels, amps);
			//playTracks(channels, tracks);
		}
		delete [] channels;
		delete [] mixers;
		network.Stop();
		myp2.printSpeakerStats();
		myp3.printSpeakerStats();
		myp4.printSpeakerStats();
	} //try

	catch ( CLAM::Err& e ) {
		e.Print();
		exit(-1);
	}
	catch( exception& e ) {
		cout << e.what() << endl;
		exit(-1);		
	}


	return 0;
}
