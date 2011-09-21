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

using namespace std;

namespace CLAM
{

class Channelizer : public CLAM::Processing {

	const static float LOW_NOISE = -10.0;
	const static float HIGH_NOISE = 0.0;
	const static float LOWSR = 5;
	const static float HIGHSR = 20;
	const static double UTTERANCE_LENGTH = 1.0;

	AudioInPort _input;
	double _max;
	int loudSoft; // 0 = ok, -1 = mic too soft, -2 = speaking too soft
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
	float avgNotSpeakingEnergy; //cchien total log energy noise floor (each buffer)
	float energyNotSpeakingCount; //cchien used in log energy noise floor average
	float totalSpeakingEnergy;
	float totalNotSpeakingEnergy;
	int numUtterances;

public:
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
	// Tracks whether or not we've logged an interrupt yet, used in main's updateFloorState function
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

		loudSoft = 0;
		
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
	

	bool Do() //for each buffer
	{
		
		const unsigned stepSize = 1;
		unsigned int bufferSNS = 0; //speech or no speech
		unsigned bufferSize = _input.GetAudio().GetBuffer().Size(); //128
		const CLAM::TData * data = &(_input.GetAudio().GetBuffer()[0]);
	
		//Find max in buffer
		for (unsigned i=0; i<bufferSize; i++) 
		{
			const CLAM::TData & current = data[i];
			if (current>_max) _max=current;
			if (current<-_max) _max=-current;
		}
		logEnergy = 60 + 20*log(_max);
		if (logEnergy > 0) { //previously 15 
			bufferSNS = 1;
			//avgSpeakingEnergy += logEnergy; //cchien
			totalSpeakingEnergy += logEnergy;
			energySpeakingCount++; // used in log energy average
		}
		else {
			avgNotSpeakingEnergy += logEnergy;
			energyNotSpeakingCount++;
		}
		_bufferCount++;
		_max = 1e-10;

		//Threshold buffer and add to moving average
		if (_bufferCount < windowSize)
		{
			
			pData[_bufferCount] = bufferSNS;
			//cout << "pdata[i]: " << pData[_bufferCount % windowSize] << endl; 				
			total += bufferSNS;
			//printf("window size is %d, bufferCount is %d, total is %f\n", windowSize, _bufferCount, total);
			if (windowSize - _bufferCount == 1) {
				_average = total/(float)windowSize; 
				//cout << "** setting average! its " << _average << endl;
			}
 		}
		else 
		{	//cout << "pdata[i]: " << pData[_bufferCount % windowSize] << endl; 
			total -= pData[_bufferCount % windowSize];
			pData[_bufferCount % windowSize] = bufferSNS;
			total += bufferSNS;
			//printf("bufferCount: %d, index: %d, bufferSNS: %d\n", _bufferCount, (_bufferCount % windowSize), bufferSNS);
			//printf("stepSize: %d, index: %d\n", stepSize, (_bufferCount % windowSize));
			if (_bufferCount % stepSize == 0) {
				_average = total/windowSize; 
				//cout << "logEnergy: " << logEnergy << ", average: " << _average << ", total: " << total << endl;
				if (_average >= 0.5) windowSNS = 1;
				else windowSNS = 0;
			}
		}

		if (windowSNS<1 && IS_NOT_TALKING(state)) {
			//cout << "not talking! state is " << state << endl;
			state = NOT_TALKING;
		}
		else if (windowSNS==1 && IS_NOT_TALKING(state)) {
			//cout << "** started talking! " << getPName() << " state is " << state << endl;
			gettimeofday(&_starttime,0x0);				
			state = START_TALKING;
			newUtterance = true;
			numUtterances++;
		}
		else if (windowSNS==1 && (IS_START_TALKING(state) || (IS_STILL_TALKING(state)))) {		
			//cout << "** still talking! state is " << state << endl;
			state = STILL_TALKING;
		}
		else if (windowSNS<1 && IS_STILL_TALKING(state)) {
			//cout << "** stopped talking! **";
			state = STOP_TALKING;
			gettimeofday(&_endtime,0x0);				
			timeval_subtract(&_timediff, &_endtime, &_starttime);
			diffTime = (double)_timediff.tv_sec + (double)0.001*_timediff.tv_usec/1000; //time in sec.ms
			totalSpeakingLength += diffTime;

			if (diffTime >= UTTERANCE_LENGTH) {
				totalSpeakingLengthNoUtterances += diffTime;
			}
			
			printSpeakerStats();
			//sendSpeakerStats();			
			writeSpeakerStats();
			//writeVolStats();
			
			diffTime = 0.0;
			interruptLogged = false;
		}
		else if (windowSNS<1 && IS_STOP_TALKING(state)) {
			//cout << "stopped talking!\n";
			state = NOT_TALKING;
			newUtterance = false; // TODO
			//calculateSNR();
			
			avgSpeakingEnergy = totalSpeakingEnergy / energySpeakingCount;
			cout << "AvgSPEnergy: " << avgSpeakingEnergy << "\tenergySpeakingCount: " << energySpeakingCount << endl;
			float signalToNoise = fabs(avgSpeakingEnergy - avgNotSpeakingEnergy) / avgNotSpeakingEnergy;

	                if (avgNotSpeakingEnergy < LOW_NOISE) {
	                        // -2 = speaking too soft, -1 = mic to soft
	                        (signalToNoise < LOWSR) ? loudSoft = -2 : loudSoft = -1;
	                }
	                // Otherwise you're good
	                else {
	                        loudSoft = 0;
	                }
		}


		calculateSNR();

/*		//cchien signal to noise ratio estimate
		float energySpeakingAvg = avgSpeakingEnergy / energySpeakingCount;
		float energyNotSpeakingAvg = avgNotSpeakingEnergy / energyNotSpeakingCount;
		float signalToNoise = fabs(energySpeakingAvg - energyNotSpeakingAvg) / energyNotSpeakingAvg;

		// 
		float LOW_NOISE = -10.0;
		float HIGH_NOISE = 0.0;
		float LOWSR = 5;
		float HIGHSR = 20;

		if (energyNotSpeakingAvg < LOW_NOISE) {
			// -2 = speaking too soft, -1 = mic to soft
			(signalToNoise < LOWSR) ? loudSoft = -2 : loudSoft = -1;
		}
		// Otherwise you're good
		else {
			loudSoft = 0;
		}
*/
		//cout << "\t windowSNS: " << windowSNS << ", state: " << hex << state << endl;
		gettimeofday(&_endtime,0x0);		
		timeval_subtract(&_timediff, &_endtime, &_sessionStart);
		sessionTime = (double)_timediff.tv_sec + (double)0.001*_timediff.tv_usec/1000; //time in sec.ms			
		//totalActivityLevel = totalSpeakingLength / sessionTime;
		
		//(totalActivityLevel >= dominanceThreshold) ? isDominant = true : isDominant = false;

		//cout << _name << " total spoken for " << sessionTime << " secs\n";
		_input.Consume();
		return true;
	}
//TODO
	void calculateSNR() {
		//cout << "AvgSPEnergy: " << avgSpeakingEnergy << "\tenergySpeakingCount: " << energySpeakingCount << endl;
		
		//avgSpeakingEnergy = (energySpeakingCount == 0) ? 0 : avgSpeakingEnergy / energySpeakingCount;
		//avgSpeakingEnergy = avgSpeakingEnergy / energySpeakingCount;
		avgNotSpeakingEnergy = totalNotSpeakingEnergy / energyNotSpeakingCount;

                /*float signalToNoise = fabs(avgSpeakingEnergy - avgNotSpeakingEnergy) / avgNotSpeakingEnergy;

                if (avgNotSpeakingEnergy < LOW_NOISE) {
                        // -2 = speaking too soft, -1 = mic to soft
                        (signalToNoise < LOWSR) ? loudSoft = -2 : loudSoft = -1;
                }
                // Otherwise you're good
                else {
                        loudSoft = 0;
                }*/
	}

	int timeval_subtract (
     		struct timeval *result,
		struct timeval *x, 
		struct timeval *y)
	{
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

	
	void SetPName(string d)
	{
		_name = d;
	}
	const char* GetClassName() const
	{
		return "Channelizer";
	}
	string getPName() {
		return _name;
	}

	void setFileName(string name) {
		logFileName = name;
	}

	//cchien
	// TODO: update this function, or remove completely since we don't use it
	void writeVolStats() {
		ofstream volFile;
		volFile.open("VolumeData.log", ios::app);
		float energySpeakingAvg = avgSpeakingEnergy / energySpeakingCount;
		float energyNotSpeakingAvg = avgNotSpeakingEnergy / energyNotSpeakingCount;
		float signalToNoise = fabs((energySpeakingAvg - energyNotSpeakingAvg) / energyNotSpeakingAvg);
		volFile << "Speaking\tNot Speaking\tS-to-R\n";
		cout << "Speaking\tNot Speaking\tS-to-R\n";
		volFile << energySpeakingAvg << "\t" << energyNotSpeakingAvg << "\t" << signalToNoise << "\n";
		cout << energySpeakingAvg << "\t" << energyNotSpeakingAvg << "\t" << signalToNoise << "\n";
		volFile.close();
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
		logFile << getDate() << "\t" <<  _name << "\t" << diffTime << "\t" << totalSpeakingLength << "\t" << totalSpeakingLengthNoUtterances << "\t" << totalSpeakingInterrupts << "\t" << totalActivityLevel << "\t";
	       (isDominant) ? logFile << "YES" : logFile << "NO";
		logFile << "\t" << avgSpeakingEnergy << "\t" << avgNotSpeakingEnergy;
		logFile << "\t" << numTimesTakenFloor;
		logFile << "\t" << numTimesNotified;
		logFile << "\t" << sessionTime << "\n";
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
	        //diffTime = 0.0;
		totalSpeakingLength = 0.0;
		totalSpeakingLengthNoUtterances = 0.0;
		totalActivityLevel = 0.0;
	        totalSpeakingTurns = 0;
		totalSpeakingInterrupts = 0; 
		totalSpeakingSuccessfulInterrupts = 0; 
		totalSpeakingUnsuccessfulInterrupts = 0;

	        //state = 0;
		//floorAction = 0;
	        overlapCounter = 0;
	        isDominant = false;
	        isBeingBeeped = false;
	        isGonnaGetBeeped = false;
	        //newUtterance = false;
	}
};

} //namespace

#endif
