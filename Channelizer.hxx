#ifndef Channelizer_hxx
#define Channelizer_hxx

#include <CLAM/Processing.hxx>
#include <CLAM/AudioInPort.hxx>
#include <stdio.h>
#include <cmath>
#include <sys/time.h>
#include <bitset>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

// XMLRPC Libraries 
#include <cassert>
#include <cstdlib>
#include <string>
#include <xmlrpc-c/girerr.hpp>
#include <xmlrpc-c/base.hpp>
#include <xmlrpc-c/client.hpp> 

// Priority Queue
#include <queue>
#include "/home/rahul/Multiparty/Projects/multipartyspeech/request.hxx"
#include "/home/rahul/Multiparty/Projects/multipartyspeech/PriorityModel.hxx"

/* Speaker Macros */
#define NOT_TALKING		0x1000
#define START_TALKING		0x0100
#define STILL_TALKING		0x0010
#define STOP_TALKING		0x0001

#define IS_NOT_TALKING(x)	(x & NOT_TALKING)
#define IS_START_TALKING(x)	(x & START_TALKING)
#define IS_STILL_TALKING(x)	(x & STILL_TALKING)
#define IS_STOP_TALKING(x)	(x & STOP_TALKING)

/* Floor Macros */
#define NO_FLOOR		0x1000
#define TAKE_FLOOR		0x0100
#define HOLD_FLOOR		0x0010
#define RELEASE_FLOOR		0x0001

#define IS_NO_FLOOR(x)		(x & NO_FLOOR)
#define IS_TAKE_FLOOR(x)	(x & TAKE_FLOOR)
#define IS_HOLD_FLOOR(x)	(x & HOLD_FLOOR)
#define IS_RELEASE_FLOOR(x)	(x & RELEASE_FLOOR)

const string TTS_TOO_SOFT= "You're too Soft";
const string TTS_TOO_LOUD = "You're too Loud";
const string TTS_BG_TOO_LOUD = "Background noise too loud";
const static double LOUD = 35.0;
const static double BG_LOUD = -25.0;
const static double SOFT = 25.0;
const static int VU_NUM_UTTERANCES = 1;
const static int VU_NOTSP_NUM_SAMPLES = 500;


using namespace std;

namespace CLAM
{

class Channelizer : public CLAM::Processing {

	const static float LOW_NOISE = -10.0;
	const static float HIGH_NOISE = 0.0;
	const static float LOWSR = 3;
	const static float HIGHSR = 20;
	const static double UTTERANCE_LENGTH = 1.0;

	AudioInPort _input;
	double _max;
	float _average;
	unsigned _bufferCount;
	string _name;
	struct timeval _starttime,_endtime,_timediff, _sessionStart;
	unsigned int windowSize;
	short *pData;
	unsigned windowSNS; //speech or no speech
	float total;

	// Statistics
	float avgSpeakingEnergy; //cchien total log energy when speaking (each buffer)
	float energySpeakingCount; //cchien used in log energy average
	float totalSpeakingEnergy;
	float currentSpeakingEnergy;
	int numUtterances;


public:

	priority_queue<Request, vector<Request>, PriorityModel> * globalQ;
        priority_queue<Request, vector<Request>, PriorityModel> internalQ;
	float avgNotSpeakingEnergy; //cchien total log energy noise floor (each buffer)
	float energyNotSpeakingCount; //cchien used in log energy noise floor average
	float totalNotSpeakingEnergy;
	float vuMeter;
	float vuMeterNotSp;
	float avgVuMeter;
	float avgVuMeterNotSp;
	int counterVu;
	int counterNotSp;

	// Statistics
	int numTimesTakenFloor;
	int numTimesNotified;
	double diffTime, totalSpeakingLength, totalSpeakingLengthNoUtterances, totalActivityLevel;
	unsigned int totalSpeakingTurns, totalSpeakingInterrupts, totalSpeakingSuccessfulInterrupts, totalSpeakingUnsuccessfulInterrupts;
	unsigned short state, floorAction;
	int overlapCounter;
	bool isDominant;
	bool isBeingBeeped;
	bool isGonnaGetBeeped;
	bool newUtterance;

	double sessionTime;
	int channelNum;
	string logFileName;
	double logEnergy;
	// Tracks whether or not we've logged an interrupt yet, used in main's updateFloorState function for TSI
	bool interruptLogged;
	struct timeval _beepStartTime, _overlapTime, _currentBeepLength;

	Channelizer( const Config& config = Config())
		: _input("Input", this)
		, _max(0.)
		, _bufferCount(-1) //previously 0
		
	{
		Configure( config );
		totalSpeakingLength = 0.0;					// TSL: DONE
		totalSpeakingLengthNoUtterances = 0.0;				// TSLNoU: DONE
		totalActivityLevel = 0.0;
		totalSpeakingTurns = 0;						// TST: # times grabbed floor
		totalSpeakingInterrupts = 0;					// TSI: # times barge in
		totalSpeakingSuccessfulInterrupts = 0;				// TSSI: # times successful barge in
		totalSpeakingUnsuccessfulInterrupts = 0;			// TSUI: # times unsuccessful barge in
		_average = 0.0;		

		diffTime = 0.0;
		gettimeofday(&_starttime,0x0);		
		gettimeofday(&_endtime,0x0);
		gettimeofday(&_timediff,0x0);
		gettimeofday(&_sessionStart,0x0);
		gettimeofday(&_beepStartTime,0x0);
		gettimeofday(&_overlapTime,0x0);
		gettimeofday(&_currentBeepLength,0x0);
		windowSize = 30;
		pData = new short[windowSize];	
		windowSNS = 0;
		total = 0;

		totalSpeakingEnergy = 0.0;
		totalNotSpeakingEnergy = 0.0;
		avgSpeakingEnergy = 0.0; //cchien total log energy when speaking (each buffer)
		energySpeakingCount = 0.01; //cchien used in log energy average
		avgNotSpeakingEnergy = 0.0; //cchien total log energy noise floor (each buffer)
		energyNotSpeakingCount = 0.01; //cchien used in log energy noise floor average

		currentSpeakingEnergy = 0.0;
		vuMeter = 0.0;
		vuMeterNotSp = 0.0;
		avgVuMeter = 0.0;
		avgVuMeterNotSp = 0.0;
		counterVu = 0;
		counterNotSp = 0;

		state = NOT_TALKING;
		floorAction = NO_FLOOR;
		overlapCounter = 0;

		isDominant = false;		
		isBeingBeeped = false;
		isGonnaGetBeeped = false;
		newUtterance = false;
		interruptLogged = false;
		numTimesNotified = 0;
		numTimesTakenFloor = 0;
	}

	void setGlobalQ(priority_queue<Request, vector<Request>, PriorityModel> * q) {
		globalQ = q;
	}


	void processRequests() {
	
	while(!internalQ.empty()) {
		Request r = internalQ.top();
		internalQ.pop();
		globalQ->push(r);
        }
	}

/* 
* Checks the background noise level of each channel and their speaking energy level
* Generates an alert if:
* 1. Background noise is above BG_NOISE_THRESHOLD
* 2. SNR is low
* 3. SNR is high
*/
string checkSoundLevels() {
        ostringstream oss;

                // If they're speaking too soft
                if(counterVu == VU_NUM_UTTERANCES) {
                        if(avgVuMeter > LOUD) {
                                // alert you're too loud
                                oss << "Channel " << channelNum << " is too loud at " << avgVuMeter << endl;
                                Request* r3 = new Request();
                                r3->setTimeSent();
                                r3->setChannel(channelNum-1);
                                r3->setPriority(1);
                                r3->setMessage(TTS_TOO_LOUD);
                                //requestQ.push(*r3);
                          	internalQ.push(*r3);
				cout << "size of internalQ is now " << internalQ.size() << endl;
                        }
                        else if(avgVuMeter < SOFT) {
                                // alert you're too soft
                                oss << "Channel " << channelNum << " is too soft" << endl;
                                Request* r4 = new Request();
                                r4->setTimeSent();
                                r4->setChannel(channelNum-1);
                                r4->setPriority(1);
                                r4->setMessage(TTS_TOO_SOFT);
				internalQ.push(*r4);
				cout << "size of internalQ is now " << internalQ.size() << endl;
                        }
                        avgVuMeter = 0.0;
                        vuMeter = 0.0;
                        counterVu = 0;
                }

                if(energyNotSpeakingCount >= VU_NOTSP_NUM_SAMPLES) {
                        if(avgNotSpeakingEnergy >= BG_LOUD) {
                                oss << "Channel " << channelNum << " background noise is too loud at " << avgNotSpeakingEnergy << endl;
                                Request* r5 = new Request();
                                r5->setTimeSent();
                                r5->setChannel(channelNum-1);
                                r5->setPriority(1);
                                r5->setMessage(TTS_BG_TOO_LOUD);
	                        internalQ.push(*r5);
				cout << "size of internalQ is now " << internalQ.size() << endl;
			}
                        totalNotSpeakingEnergy = 0.0;
                        energyNotSpeakingCount = 0;
                }
        return oss.str();
}

	

	// for each buffer
	bool Do() {
		const unsigned stepSize = 1;
		unsigned int bufferSNS = 0; //speech or no speech
		unsigned bufferSize = _input.GetAudio().GetBuffer().Size(); //128
		const CLAM::TData * data = &(_input.GetAudio().GetBuffer()[0]);
	
		//Find max in buffer
		for (unsigned i=0; i<bufferSize; i++) {
			const CLAM::TData & current = data[i];
			if (current>_max) _max=current;
			if (current<-_max) _max=-current;
		}
		logEnergy = 60 + 20*log(_max);
		if (logEnergy > 0) { //previously 15 
			bufferSNS = 1;
			totalSpeakingEnergy += logEnergy;
			currentSpeakingEnergy = logEnergy;
			energySpeakingCount++; // used in log energy average
		}
		else {
			totalNotSpeakingEnergy += logEnergy;
			energyNotSpeakingCount++;
		}
		_bufferCount++;
		_max = 1e-10;

		//Threshold buffer and add to moving average
		if (_bufferCount < windowSize) {
			pData[_bufferCount] = bufferSNS;
			total += bufferSNS;
			if (windowSize - _bufferCount == 1) {
				_average = total/(float)windowSize; 
			}
 		}
		else {	
			total -= pData[_bufferCount % windowSize];
			pData[_bufferCount % windowSize] = bufferSNS;
			total += bufferSNS;
			if (_bufferCount % stepSize == 0) {
				_average = total/windowSize; 
				if (_average >= 0.5) windowSNS = 1;
				else windowSNS = 0;
			}
		}

		if (windowSNS<1 && IS_NOT_TALKING(state)) {
			state = NOT_TALKING;
		}
		else if (windowSNS==1 && IS_NOT_TALKING(state)) {
			gettimeofday(&_starttime,0x0);				
			state = START_TALKING;
			newUtterance = true;
			numUtterances++;
		}
		else if (windowSNS==1 && (IS_START_TALKING(state) || (IS_STILL_TALKING(state)))) {		
			state = STILL_TALKING;
		}
		else if (windowSNS<1 && IS_STILL_TALKING(state)) {
			state = STOP_TALKING;
			gettimeofday(&_endtime,0x0);				
			timeval_subtract(&_timediff, &_endtime, &_starttime);
			diffTime = (double)_timediff.tv_sec + (double)0.001*_timediff.tv_usec/1000; //time in sec.ms
			totalSpeakingLength += diffTime;

			if (diffTime >= UTTERANCE_LENGTH) {
				totalSpeakingLengthNoUtterances += diffTime;
			}
			
			//printSpeakerStats();
			//sendSpeakerStats();			
			writeSpeakerStats();
			
			diffTime = 0.0;
			interruptLogged = false;
		}
		else if (windowSNS<1 && IS_STOP_TALKING(state)) {
			state = NOT_TALKING;
			newUtterance = false;
			calculateSp();
		}

		calculateBg();

		checkSoundLevels();

		gettimeofday(&_endtime,0x0);		
		timeval_subtract(&_timediff, &_endtime, &_sessionStart);
		sessionTime = (double)_timediff.tv_sec + (double)0.001*_timediff.tv_usec/1000; //time in sec.ms			

		_input.Consume();

		processRequests();

		return true;
	}

	/**
	* Calculate speaking energy statistics
	* We reset totalSpeakingEnergy and energySpeakingCount at the end of each utterance, which is when this function is called
	*/
	void calculateSp() {
		avgSpeakingEnergy = totalSpeakingEnergy / energySpeakingCount;

		totalSpeakingEnergy = 0.0;
		energySpeakingCount = 0;
	
		vuMeter += avgSpeakingEnergy;
		counterVu++;
		avgVuMeter = vuMeter / counterVu;
		if(channelNum == 1)
			cout << "avgVuMeter is " << avgVuMeter << endl;
	}

	/**
	* Calculate background noise statistics
	*/
	void calculateBg() {
		avgNotSpeakingEnergy = totalNotSpeakingEnergy / energyNotSpeakingCount;
	}

	int timeval_subtract ( struct timeval *result, struct timeval *x, struct timeval *y) {
		struct timeval temp = *y;
  		/* Perform the carry for the later subtraction by updating y. */
  		if (x->tv_usec < y->tv_usec) {
			int nsec = (y->tv_usec - x->tv_usec) / 1000000L + 1;
			y->tv_usec -= 1000000L * nsec;
			y->tv_sec += nsec;
		}
		if (x->tv_usec - y->tv_usec > 1000000L) {
			int nsec = (y->tv_usec - x->tv_usec) / 1000000L;
			y->tv_usec += 1000000L * nsec;
			y->tv_sec -= nsec;
		}
	
		/* Compute the time remaining to wait.
		   tv_usec is certainly positive. */
		result->tv_sec = x->tv_sec - y->tv_sec;
		result->tv_usec = x->tv_usec - y->tv_usec;
		y->tv_sec = temp.tv_sec;
		y->tv_usec = temp.tv_usec;

		/* Return 1 if result is negative. */
		return x->tv_sec < y->tv_sec;
	}

	
	void SetPName(string d) {
		_name = d;
	}

	const char* GetClassName() const {
		return "Channelizer";
	}

	string getPName() {
		return _name;
	}

	void setFileName(string name) {
		logFileName = name;
	}

	inline void printSpeakerStats() {
		cout << "\t" << _name << " spoke for " << diffTime << " secs\n";
		cout << "\t" << _name << " TSL (total speaking length): " << totalSpeakingLength << " secs\n";
		cout << "\t" << _name << " TSLNoU (total speaking length no utterances): " << totalSpeakingLengthNoUtterances << " secs\n";
		cout << "\t" << _name << " TSI (total speaking interrupts): " << totalSpeakingInterrupts << " times\n";
		cout << "\t" << _name << " TSI (total speaking unsuccessful interrupts): " << totalSpeakingUnsuccessfulInterrupts << " times\n";
		cout << "\t" << _name << " Dominance Percentage: " << totalActivityLevel << "%\n";
		cout << "\t" << _name << " Is Dominant: ";
	       (isDominant) ? cout << "YES\n" : cout << "NO\n";
		cout << "\t" << "Avg Speaking Energy: " << avgSpeakingEnergy << "\n";
		cout << "\t" << "Avg Background Energy: " << avgNotSpeakingEnergy << "\n";
		cout << "\t" << "Number of Times Taken Floor: " << numTimesTakenFloor << "\n";
		cout << "\t\t" << "Number of Times Notified: " << numTimesNotified << "\n";
		cout << "\t" << _name << " Session Time: " << sessionTime << " sec\n";
	}

	 /**
        * Sends the current speaker's statistics to our Rails/Faye server to broadcast to all users
        * The order in which the statistics are sent are very important, and must be in this specific order:
        * Channel Number, Speaking Length, TSL, TSLNoU, TSI, TSSI, TSUI, Dominance Percentage, Is Dominant
        */
        inline void sendSpeakerStats() {
                cout << "** Sending data" << endl;
                xmlrpc_c::clientXmlTransport_curl myTransport;
                xmlrpc_c::client_xml myClient(&myTransport);
                string const methodName("get_data_rpc");
                string const serverUrl("http://localhost:3000/main/get_data_rpc");
                try {
                        xmlrpc_c::paramList sampleAddParms1;
                        sampleAddParms1.add(xmlrpc_c::value_int(channelNum));
                        sampleAddParms1.add(xmlrpc_c::value_double(diffTime));
                        sampleAddParms1.add(xmlrpc_c::value_double(totalSpeakingLength));
                        sampleAddParms1.add(xmlrpc_c::value_double(totalSpeakingLengthNoUtterances));
                        sampleAddParms1.add(xmlrpc_c::value_int(totalSpeakingInterrupts));
                        sampleAddParms1.add(xmlrpc_c::value_int(totalSpeakingSuccessfulInterrupts));
                        sampleAddParms1.add(xmlrpc_c::value_int(totalSpeakingUnsuccessfulInterrupts));
                        sampleAddParms1.add(xmlrpc_c::value_double(totalActivityLevel));
                        sampleAddParms1.add(xmlrpc_c::value_boolean(isDominant));
// TODO: add new stats 
			// Num times notified here
			// Num times taken floor here

                        xmlrpc_c::rpcPtr rpc1P(methodName, sampleAddParms1);
                        xmlrpc_c::carriageParm_curl0 myCarriageParm(serverUrl);
                        rpc1P->start(&myClient, &myCarriageParm);
                        myClient.finishAsync(xmlrpc_c::timeout()); // infinite timeout?
                        assert(rpc1P->isFinished());
                }
                catch (exception const& e) {
                        cout << "Client threw error: " << e.what() << endl;
                }
                catch (...) {
                        cout << "Client threw unexpected error." << endl;
                }
        }

	// Writes this channelizer's statistics to the logfile specified in the Supervisor
	inline void writeSpeakerStats() {
		ofstream logFile;
		logFile.open(logFileName.c_str(), ios::app);//, ios::app);

		logFile.setf(ios_base::fixed);
		logFile.precision(7);

		// CurrTime, ChannelName, Speaking Length, TSL, TSLNoU, TSI, Dom%, IsDominant, AvgSpeakingEnergy, AvgNotSpeakingEnergy, NumTimesTakenFloor, NumTimesNotified, TotalSessionTime
		logFile << getDate() << "\t";
		logFile <<  _name << "\t";
		logFile << diffTime << "\t";
		logFile << totalSpeakingLength << "\t";
		logFile << totalSpeakingLengthNoUtterances << "\t";
		logFile << totalSpeakingInterrupts << "\t";
		logFile << totalActivityLevel << "\t";
	       (isDominant) ? logFile << "YES\t" : logFile << "NO\t";
		logFile << avgSpeakingEnergy << "\t";
		logFile << avgNotSpeakingEnergy << "\t";
		logFile << numTimesTakenFloor << "\t";
		logFile << numTimesNotified << "\t";
		logFile << sessionTime << "\n";
		logFile.close();
	}

	// Returns the current date and local time
	string getDate() {
		time_t rawtime;
		struct tm* timeinfo;
		int year, month, day;
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		mktime(timeinfo);
		stringstream currTime, fileName;
		currTime << timeinfo->tm_hour << ":" << timeinfo->tm_min << ":" << timeinfo->tm_sec << "_" << timeinfo->tm_mon << "_" << timeinfo->tm_mday << "_" << (timeinfo->tm_year+1900);
		return currTime.str();
	}

	void reset() {
		avgSpeakingEnergy = 0.0;
	        energySpeakingCount = 0.0;
        	avgNotSpeakingEnergy = 0.0;
	        energyNotSpeakingCount = 0.0;
	        totalSpeakingEnergy = 0.0;
	        totalNotSpeakingEnergy = 0.0;
	        numUtterances = 0;

		numTimesTakenFloor = 0;
	        numTimesNotified = 0;
		totalSpeakingLength = 0.0;
		totalSpeakingLengthNoUtterances = 0.0;
		totalActivityLevel = 0.0;
	        totalSpeakingTurns = 0;
		totalSpeakingInterrupts = 0; 
		totalSpeakingSuccessfulInterrupts = 0; 
		totalSpeakingUnsuccessfulInterrupts = 0;

	        overlapCounter = 0;
	        isDominant = false;
	        isBeingBeeped = false;
	        isGonnaGetBeeped = false;

		numTimesNotified = 0;
	}

};

} //namespace

#endif
