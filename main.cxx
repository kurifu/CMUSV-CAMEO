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

#define IS_NOT_TALKING(x)	(x & NOT_TALKING)
#define IS_START_TALKING(x)	(x & START_TALKING)
#define IS_STILL_TALKING(x)	(x & STILL_TALKING)
#define IS_STOP_TALKING(x)	(x & STOP_TALKING)
#define FLOOR_FREE		-1 == channelThatHasFloor

using namespace std;

const static double DOMINANCE_THRESHOLD = .4;
const static double BEEPLENGTH = 3.0;
const static double overlapLength = 2.0;
double currOverlapLength = 0;
struct timeval _currTime, _beepTimeDiff, _overlapStartTime;
bool isOverlapping = false;
double diffTime = 0.0;
const int NUMCHANNELS = 4;

//////////////////////////////
// CLAM Setup
//////////////////////////////

/********
* Login *
********/
static int mySocketFD;
int CURRNUMCHANNELS = 0;
//const int SLEEP_WAIT = 5;
static int LISTEN_PORT = 4444;
jack_client_t * jackClient;

// Number of seconds to wait for someone to login before starting supervisor
//static const int LOGIN_WAIT = 5;

inline void initialize() {
        mySocketFD = socket(AF_INET, SOCK_STREAM, 0);

        int optval = 1;
        setsockopt(mySocketFD, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        if(mySocketFD < 0) {
                cerr << "ERROR opening socket on port " << LISTEN_PORT << endl;
                return;
        }
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
                cerr << "IPV6 problem?" << endl;
        }
        return ip;
}

/**
* Function that handles incoming connections at LISTEN_PORT by blocking
* This function runs in a separate thread called by runLoginManager
*/
//void* connect(void* ptr) {
void connect() {
        while(1) {
                socklen_t clientAddrLen;
                struct sockaddr_in serverAddr, clientAddr;
                int clientSocketFD;

                initialize();
                bzero((char*) &serverAddr, sizeof(serverAddr));

                // Initialize members of struct sockaddr_in
                // INADDR_ANY gets the address of the machine the server is running on
                // need to convert port number to network byte order via htons
                serverAddr.sin_family = AF_INET;
                serverAddr.sin_addr.s_addr = INADDR_ANY;
                serverAddr.sin_port = htons(LISTEN_PORT);

                if(bind(mySocketFD, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0) {
                        cerr << "ERROR on binding socket, trying port " << ++LISTEN_PORT << endl;
			
			initialize();
                	bzero((char*) &serverAddr, sizeof(serverAddr));
                	serverAddr.sin_family = AF_INET;
                	serverAddr.sin_addr.s_addr = INADDR_ANY;
                	serverAddr.sin_port = htons(LISTEN_PORT);
                	
			if(bind(mySocketFD, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0) {
				cerr << "New port doesn't work either...??? " << endl;
				return;
			}
                }

                cerr << "Waiting for incoming client... " << endl;
                flush(cerr);
                // 2nd arg: size of backlog queue, 5 for now, really only need 1
                listen(mySocketFD, 5);
                clientAddrLen = sizeof(clientAddr);

                clientSocketFD = accept(mySocketFD, (struct sockaddr*) &clientAddr, &clientAddrLen);
                cerr << "\t... connected!" << endl;
                if(clientSocketFD < 0) {
                        cerr << "ERROR on accepting connection!" << endl;
                        pthread_exit(0);
                }

                char* ip = getSocketPeerIp(clientSocketFD);
		//cerr << "Connected ip is " << ip << endl;
		//flush(cerr);
                //if(ip != NULL) {
                        //cerr << "Got it, gonna execute 'jack_netsource -H " << ip << endl;
                        CURRNUMCHANNELS++;
                        string command = "jack_netsource -H " + (string)ip + " &";
                        system(command.c_str());

			sleep(1);

			//cout << "curr num channels is " << CURRNUMCHANNELS << endl;
		//flush(cout);
			if(CURRNUMCHANNELS == 1) {
		//		cout << "Hooking up 2nd channel with ip " << ip << endl;
		//flush(cout);
				jack_connect(jackClient, "netjack:capture_1", "client1:AudioSource_2");
				jack_connect(jackClient, "netjack:capture_2", "client1:AudioSource_2");
				jack_connect(jackClient, "client1:AudioSink_2", "netjack:playback_1");
				jack_connect(jackClient, "client1:AudioSink_2", "netjack:playback_2");
			}
			else if(CURRNUMCHANNELS == 2) {
				cerr << "Hooking up 3rd channel with ip " << ip << endl;
				jack_connect(jackClient, "netjack-01:capture_1", "client1:AudioSource_3");
				jack_connect(jackClient, "netjack-01:capture_2", "client1:AudioSource_3");
				jack_connect(jackClient, "client1:AudioSink_3", "netjack-01:playback_1");
				jack_connect(jackClient, "client1:AudioSink_3", "netjack-01:playback_2");
			}
			else if(CURRNUMCHANNELS == 3) {
				cerr << "Hooking up 4th channel with ip " << ip << endl;
				jack_connect(jackClient, "netjack-02:capture_1", "client1:AudioSource_4");
				jack_connect(jackClient, "netjack-02:capture_2", "client1:AudioSource_4");
				jack_connect(jackClient, "client1:AudioSink_4", "netjack-02:playback_1");
				jack_connect(jackClient, "client1:AudioSink_4", "netjack-02:playback_2");
			}
                //}
               /* else {
                        cerr << "Error getting IP!" << endl;
                }*/

                close(clientSocketFD);
                close(mySocketFD);
		//pthread_exit(0);
        }
}

/**
* Spawns off a thread to run the 'connect' function which handles incoming socket connecitons
*/
void runLoginManager() {
/*        pthread_t thread_connect;
        char* dummyMsg = "return msg?";
        int retval_connect_supervisor;

        cerr << "Initializing Login Manager..." << endl;
        retval_connect_supervisor = pthread_create(&thread_connect, NULL, connect, (void*) dummyMsg);
        //pthread_join(thread_connect, NULL);
        cerr << "LoginManager started successfully." << endl;
*/
	pid_t pID = fork();
	if(pID == 0) {
		cerr << "Login Manager Process running" << endl;
		connect();	
	}

}

//////////////////////////////
// Floor Management
//////////////////////////////
int channelThatHasFloor = -1;

int error(const string & msg)
{
	cerr << msg << endl;
	return -1;
}

// Determines if each person is dominant or not based on their total Activity Level (how long they've been talking)
inline void calculateDominance(CLAM::Channelizer* channels[]) {
	double totalTSL = channels[0]->totalSpeakingLength + channels[1]->totalSpeakingLength + channels[2]->totalSpeakingLength + channels[3]->totalSpeakingLength;
	for(int i = 0; i < NUMCHANNELS; i++) {
		channels[i]->totalActivityLevel = channels[i]->totalSpeakingLength / totalTSL;
		(channels[i]->totalActivityLevel >= DOMINANCE_THRESHOLD && channels[i]->totalActivityLevel < 1) ? channels[i]->isDominant = true : channels[i]->isDominant = false;
	}
}

// PGAO AMP ADJUST
// Volume Control
void adjustAmps(CLAM::Channelizer* channels[], CLAM::Processing* amps[]){
	int j=0.5;
	while(true){
		for(int i=0; i < NUMCHANNELS; i++){
			if(IS_START_TALKING(channels[i]->state)||IS_STILL_TALKING(channels[i]->state)){
	
				while(channels[i]->logEnergy<50) {
					j+=0.1;
					CLAM::SendFloatToInControl(*(amps[i]), "Gain", j);
				}
				while(channels[i]->logEnergy>60){
					j-=0.1;
					CLAM::SendFloatToInControl(*(amps[i]), "Gain", j);
				}
			}
		}
	}
}

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
			// TODO
			else if(IS_STILL_TALKING(channels[i]->state) || IS_STOP_TALKING(channels[i]->state)) {
				CLAM::SendFloatToInControl(*(tracks[i]), "Gain", 5.0);
			}
			else {
				CLAM::SendFloatToInControl(*(tracks[i]), "Gain", 0.0);
			}
		}
	}
}

// Beeps any channels that have been overlapping for at least 5 seconds and do not have the floor
// Stops beeping after 3 seconds and starts all over again
inline void adjustAlerts(CLAM::Channelizer* channels[], CLAM::Processing* mixers[]) {

	gettimeofday(&_currTime, 0x0);
	diffTime = 0.0;

	for(int i = 0; i < NUMCHANNELS; i++) {
/*
		// If you've been marked but haven't been beeped yet
		if(channels[i]->isGonnaGetBeeped && !channels[i]->isBeingBeeped) {
			gettimeofday(&(channels[i]->_beepStartTime),0x0);
			channels[i]->isBeingBeeped = true;
			channels[i]->isGonnaGetBeeped = false;
			//cerr << "starting beep\n";
			CLAM::SendFloatToInControl(*(mixers[i]), "Gain 3", 1.0);
		}
		// If you're currently being beeped, make sure we don't beep you longer than X seconds
		else if (channels[i]->isBeingBeeped) {
			channels[i]->timeval_subtract(&_beepTimeDiff, &_currTime, &(channels[i]->_beepStartTime));
			diffTime = (double)_beepTimeDiff.tv_sec + (double)0.001*_beepTimeDiff.tv_usec/1000;

			// Turn off beep
			if(diffTime >= BEEPLENGTH) {
				CLAM::SendFloatToInControl(*(mixers[i]), "Gain 3", 0.0);
				channels[i]->isBeingBeeped = false;
			}
		}
*/
		if(channels[i]->isDominant) {
			
		}
	}
}


// Looks at each Speaker State, updates each channel's Floor Action State
void updateFloorActions(CLAM::Channelizer* channels[]) {
	for(int i = 0; i < NUMCHANNELS; i++) {
		if(IS_START_TALKING(channels[i]->state))
			channels[i]->floorAction = TAKE_FLOOR;
		else if (IS_STILL_TALKING(channels[i]->state))
			channels[i]->floorAction = HOLD_FLOOR;
		else if (IS_STOP_TALKING(channels[i]->state))
			channels[i]->floorAction = RELEASE_FLOOR;
		else if (IS_NOT_TALKING(channels[i]->state))
			channels[i]->floorAction = NO_FLOOR;
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
	//cerr << "giveFloorToLeastDominantGuy" << endl;
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

	/*oss << (channelThatHasFloor+1);
	string output = "Channel " + oss.str() + ", you've been talking for quite some time, why don't you let Channel ";
	oss.str("");
	oss << (channelThatIsLeastDominant+1);
	output += oss.str() + " take over?";
	channelThatHasFloor = channelThatIsLeastDominant;
	return output;*/
}


// Looks at each Floor Action, updates the global Floor State
string updateFloorState(CLAM::Channelizer* channels[]) {
	string outputMsg;
	ostringstream oss;

	int numSpeakers = findNumSpeakers(channels);

	// Case 1: No one has floor, only 1 person talking
	// 	   Figure out who to give it to
	if(FLOOR_FREE) {
		//cerr << "Floor is free!" << endl;
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
			oss << (channelThatHasFloor+1);
			outputMsg = "Giving Floor to Channel " + oss.str() + "\n";
			isOverlapping = false;
		}
		else {
			// More than 1 guy started at the same time; very very rare case, deal with it later
		}
	}
	// Case 2: Someone has floor; check everyone else's status before updating anything
	else {
		//cerr << "Floor is NOT free!" << endl;
		// Case 2.1: Only 1 guy talking and holding the floor, most common case, let him continue
		if(IS_HOLD_FLOOR(channels[channelThatHasFloor]->floorAction) && (1 == numSpeakers)) {
			isOverlapping = false;
		}

		// Case 2.2: 1 guy talking and holding the floor, 1 or more guys interrupt him: BARGE IN
		// 	If there is an overlap for longer than overlapLength, mark everyone who does not 
		// 	have the floor and is talking; whoever is marked will get beeped
		else if(IS_HOLD_FLOOR(channels[channelThatHasFloor]->floorAction) && (1 != numSpeakers)) {
			//cerr << "BARGE IN! " << endl;
			for (int i = 0; i < NUMCHANNELS; i++) {
				if((i != channelThatHasFloor) && (IS_HOLD_FLOOR(channels[i]->floorAction))) {
					channels[i]->totalSpeakingInterrupts++;
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
			if(currOverlapLength >= overlapLength) {
				
				// Mark everyone who is talking that doesn't have the floor
				for (int i = 0; i < NUMCHANNELS; i++) {
					if((i != channelThatHasFloor) && (IS_TAKE_FLOOR(channels[i]->floorAction) || IS_HOLD_FLOOR(channels[i]->floorAction))) {
						channels[i]->isGonnaGetBeeped = true;
					}
				}
				currOverlapLength = 0.0;
				isOverlapping = false;	// TODO: may not be a good var name
			}
		}
		else if (0 == numSpeakers) {
			channelThatHasFloor = -1;
		}
	}

	return outputMsg;
}

string updateFloorStuff(CLAM::Channelizer* channels[], string prevMsg, CLAM::Processing* mixers[]) {
	string notifyMsg = "";
	
	ofstream dataFile;
	//dataFile.open("multiPartySpeechData.xml");
	//cerr << "Writing start tag!\n";
	//dataFile << "<MultiPartySpeech>\n";
	
	calculateDominance(channels);

	adjustAlerts(channels, mixers);

	updateFloorActions(channels);

	notifyMsg = updateFloorState(channels);

	//dataFile << "</MultiPartySpeech>\n";
	//dataFile.close();

	if(("" != notifyMsg) && (prevMsg != notifyMsg)) {
		cerr << notifyMsg << endl;
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

int main( int argc, char** argv )
{	
	try
	{

/*******************************************************************/
/*-----------------------------SETUP-------------------------------*/
/*******************************************************************/
		// These values will be used in some configurations, so we declare it now.
		int SIZE = 128;
		int SAMPLERATE = 16000;

		// We need to deploy the audio manager class in order to get audio sound.
		CLAM::AudioManager manager( SAMPLERATE, SIZE );
		manager.Start();

		CLAM::Network network;
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

	/*	// Background Noise
		CLAM::Processing& trackVol1 = network.GetProcessing("AudioAmplifier");
		CLAM::Processing& trackVol2 = network.GetProcessing("AudioAmplifier_1");
		CLAM::Processing& trackVol3 = network.GetProcessing("AudioAmplifier_2");
		CLAM::Processing& trackVol4 = network.GetProcessing("AudioAmplifier_3");
		CLAM::Processing* tracks[4] ={&trackVol1, &trackVol2, &trackVol3, &trackVol4};
		for(int i = 0; i < NUMCHANNELS; i++) {
			CLAM::SendFloatToInControl(*tracks[i], "Gain", 0.0);
		}
*/
		CLAM::Processing& generator = network.GetProcessing("Generator");
		//CLAM::SendFloatToInControl(generator, "Amplitude", 1.0);

		CLAM::Processing& mic = network.GetProcessing("AudioSource");
		CLAM::Channelizer& myp1 = (CLAM::Channelizer&) network.GetProcessing("Channelizer");
		CLAM::Channelizer& myp2 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_1");
		CLAM::Channelizer& myp3 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_2");
		CLAM::Channelizer& myp4 = (CLAM::Channelizer&) network.GetProcessing("Channelizer_3");

		myp1.SetPName("Channel 1");
		myp2.SetPName("Channel 2");
		myp3.SetPName("Channel 3");
		myp4.SetPName("Channel 4");		

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

		gettimeofday(&_currTime, 0x0);
		gettimeofday(&_beepTimeDiff, 0x0);

		runLoginManager();

/*		while(CURRNUMCHANNELS == 0) {
			cerr << "No participants, sleeping for " << endl;// << SLEEP_WAIT << " seconds" << endl;
			sleep(5);
		}	
*/
		cerr << "Starting supervisor..." << endl;
		//Festival TTS
	
 		EST_Wave wave;
   		int heap_size = 21000000;  // default scheme heap size
    		int load_init_files = 1; // we want the festival init files loaded
		int worked = 0;

    		festival_initialize(load_init_files,heap_size);

    		// Say simple file
    		//festival_say_file("/etc/motd");

    		//festival_eval_command("(voice_ked_diphone)");
    		// Say some text;
		//char * s = "hello world";
    		//worked = festival_say_text(s);
		//std::cout << worked << "---***Done***\n";

    		// Convert to a waveform
    		festival_text_to_wave("hello goodbye cliff apple birds apple zoo zebra donkey",wave);
	    	wave.save("/home/rahul/Multiparty/Projects/multipartyspeech/wave.wav","riff");

    		// festival_say_file puts the system in async mode so we better
   	        // wait for the spooler to reach the last waveform before exiting
    		// This isn't necessary if only festival_say_text is being used (and
    		// your own wave playing stuff)
    		festival_wait_for_spooler();

		CLAM::Processing& tts = network.GetProcessing("TTS");
		CLAM::SendFloatToInControl(tts, "Seek", 0.0);
    		
		sleep(2);

    		festival_text_to_wave("RAHUL RAHUL RAHUL",wave);
	    	wave.save("/home/rahul/Multiparty/Projects/multipartyspeech/wave.wav","riff");
		
		CLAM::SendFloatToInControl(tts, "Seek", 0.0);

		//festival_text_to_wave("hello",wave);
	    	//wave.save("/home/rahul/Multiparty/Projects/multipartyspeech/wave.wav","riff");
		


		cout << "before main while" << endl;
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
		cerr << e.what() << endl;
		exit(-1);		
	}


	return 0;
}
