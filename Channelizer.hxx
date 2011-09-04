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

namespace CLAM
{

class Channelizer : public CLAM::Processing
{
	AudioInPort _input;
	double _max;
	int loudSoft; // 0 = ok, -1 = mic too soft, -2 = speaking too soft
	float _average;
	unsigned _bufferCount;
	std::string _name;
	struct timeval _starttime,_endtime,_timediff, _sessionStart;
	unsigned int windowSize;
	short *pData;
	unsigned windowSNS; //speech or no speech
	float total;
	float totalEnergySpeaking; //cchien total log energy when speaking (each buffer)
	float energySpeakingCount; //cchien used in log energy average
	float totalEnergyNotSpeaking; //cchien total log energy noise floor (each buffer)
	float energyNotSpeakingCount; //cchien used in log energy noise floor average

public:
	double logEnergy;
	double diffTime, totalSpeakingLength, sessionTime, totalSpeakingLengthNoUtterances, totalActivityLevel;
	unsigned int totalSpeakingTurns, totalSpeakingInterrupts, totalSpeakingSuccessfulInterrupts, totalSpeakingUnsuccessfulInterrupts;
	const static double utteranceLength = 1.0;
	//const static double dominanceThreshold = .25;
	unsigned short state, floorAction;
	int overlapCounter;
	bool isDominant, isBeingBeeped, isGonnaGetBeeped, newUtterance;
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

		totalEnergySpeaking = 0.0; //cchien total log energy when speaking (each buffer)
		energySpeakingCount = 0.0; //cchien used in log energy average
		totalEnergyNotSpeaking = 0.0; //cchien total log energy noise floor (each buffer)
		energyNotSpeakingCount = 0.0; //cchien used in log energy noise floor average

		state = NOT_TALKING;
		floorAction = NO_FLOOR;
		overlapCounter = 0;

		isDominant = false;		
		isBeingBeeped = false;
		isGonnaGetBeeped = false;
		newUtterance = false;
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
			totalEnergySpeaking += logEnergy; //cchien
			energySpeakingCount++; // used in log energy average
		}
		else {
			totalEnergyNotSpeaking += logEnergy;
			energyNotSpeakingCount++;
		}
		_bufferCount++;
		_max = 1e-10;

		//Threshold buffer and add to moving average
		if (_bufferCount < windowSize)
		{
			
			pData[_bufferCount] = bufferSNS;
			//std::cout << "pdata[i]: " << pData[_bufferCount % windowSize] << std::endl; 				
			total += bufferSNS;
			//printf("window size is %d, bufferCount is %d, total is %f\n", windowSize, _bufferCount, total);
			if (windowSize - _bufferCount == 1) {
				_average = total/(float)windowSize; 
				//std::cout << "** setting average! its " << _average << std::endl;
			}
 		}
		else 
		{	//std::cout << "pdata[i]: " << pData[_bufferCount % windowSize] << std::endl; 
			total -= pData[_bufferCount % windowSize];
			pData[_bufferCount % windowSize] = bufferSNS;
			total += bufferSNS;
			//printf("bufferCount: %d, index: %d, bufferSNS: %d\n", _bufferCount, (_bufferCount % windowSize), bufferSNS);
			//printf("stepSize: %d, index: %d\n", stepSize, (_bufferCount % windowSize));
			if (_bufferCount % stepSize == 0) {
				_average = total/windowSize; 
				//std::cout << "logEnergy: " << logEnergy << ", average: " << _average << ", total: " << total << std::endl;
				if (_average >= 0.5) windowSNS = 1;
				else windowSNS = 0;
			}
		}

		if (windowSNS<1 && IS_NOT_TALKING(state)) {
			//std::cout << "not talking! state is " << state << std::endl;
			state = NOT_TALKING;
		}
		else if (windowSNS==1 && IS_NOT_TALKING(state)) {
			gettimeofday(&_starttime,0x0);				
			state = START_TALKING;
			newUtterance = true;
			//std::cout << "** started talking! " << getPName() << " state is " << state << std::endl;
		}
		else if (windowSNS==1 && (IS_START_TALKING(state) || (IS_STILL_TALKING(state)))) {		
			state = STILL_TALKING;
			//std::cout << "** still talking! state is " << state << std::endl;
		}
		else if (windowSNS<1 && IS_STILL_TALKING(state)) {
			//std::cout << "** stopped talking! **";
			state = STOP_TALKING;
			gettimeofday(&_endtime,0x0);				
			timeval_subtract(&_timediff, &_endtime, &_starttime);
			diffTime = (double)_timediff.tv_sec + (double)0.001*_timediff.tv_usec/1000; //time in sec.ms
			totalSpeakingLength += diffTime;
			if (diffTime >= utteranceLength) {
				totalSpeakingLengthNoUtterances += diffTime;
			}
			
			printSpeakerStats();
			writeSpeakerStats();
			writeVolStats();
			
			diffTime = 0.0;
		}
		else if (windowSNS<1 && IS_STOP_TALKING(state)) {
			//std::cout << "stopped talking!\n";
			state = NOT_TALKING;
			newUtterance = false; // TODO
		}

		//cchien signal to noise ratio estimate
		float energySpeakingAvg = totalEnergySpeaking / energySpeakingCount;
		float energyNotSpeakingAvg = totalEnergyNotSpeaking / energyNotSpeakingCount;
		float signalToNoise = fabs(energySpeakingAvg - energyNotSpeakingAvg) / energyNotSpeakingAvg;

		// 
		float lowNoise = -10.0;
		float highNoise = 0.0;
		float lowSR = 5;
		float highSR = 20;

		if (energyNotSpeakingAvg < lowNoise) {
			// -2 = speaking too soft, -1 = mic to soft
			(signalToNoise < lowSR) ? loudSoft = -2 : loudSoft = -1;
		}
		// Otherwise you're good
		else {
			loudSoft = 0;
		}

		//std::cout << "\t windowSNS: " << windowSNS << ", state: " << std::hex << state << std::endl;
		gettimeofday(&_endtime,0x0);		
		timeval_subtract(&_timediff, &_endtime, &_sessionStart);
		sessionTime = (double)_timediff.tv_sec + (double)0.001*_timediff.tv_usec/1000; //time in sec.ms			
		//totalActivityLevel = totalSpeakingLength / sessionTime;
		
		//(totalActivityLevel >= dominanceThreshold) ? isDominant = true : isDominant = false;

		//std::cout << _name << " total spoken for " << sessionTime << " secs\n";
		_input.Consume();
		return true;
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

	
	void SetPName(char* d)
	{
		_name = d;
	}
	const char* GetClassName() const
	{
		return "Channelizer";
	}
	std::string getPName() {
		return _name;
	}

	//cchien
	void writeVolStats() {
		std::ofstream volFile;
		volFile.open("VolumeData.log", std::ios::app);
		float energySpeakingAvg = totalEnergySpeaking / energySpeakingCount;
		float energyNotSpeakingAvg = totalEnergyNotSpeaking / energyNotSpeakingCount;
		float signalToNoise = fabs((energySpeakingAvg - energyNotSpeakingAvg) / energyNotSpeakingAvg);
		volFile << "Speaking\tNot Speaking\tS-to-R\n";
		std::cout << "Speaking\tNot Speaking\tS-to-R\n";
		volFile << energySpeakingAvg << "\t" << energyNotSpeakingAvg << "\t" << signalToNoise << "\n";
		std::cout << energySpeakingAvg << "\t" << energyNotSpeakingAvg << "\t" << signalToNoise << "\n";
		volFile.close();
	}
	
	inline void printSpeakerStats() {
		/*std::cout << "\t" << _name << " spoke for " << diffTime << " secs\n";
		std::cout << "\t" << _name << " TSL (total speaking length): " << totalSpeakingLength << " secs\n";
		std::cout << "\t" << _name << " TSLNoU (total speaking length no utterances): " << totalSpeakingLengthNoUtterances << " secs\n";
		std::cout << "\t" << _name << " TSI (total speaking interrupts): " << totalSpeakingInterrupts << " times\n";
		std::cout << "\t" << _name << " TSI (total speaking unsuccessful interrupts): " << totalSpeakingUnsuccessfulInterrupts << " times\n";
		std::cout << "\t" << _name << " Dominance Percentage: " << totalActivityLevel*100 << "%\n";
		std::cout << "\t" << _name << " Is Dominant: ";
	       (isDominant) ? std::cout	<< "YES\n" : std::cout << "NO\n";
		std::cout << "\t" << _name << " Session Time: " << sessionTime << " sec\n";*/
	}

	void writeSpeakerStats() {
		std::ofstream cumulativeLogFile;
		//std::ofstream dataFile;

		cumulativeLogFile.open("multiPartySpeechData.log", std::ios::app);
		//dataFile.open("multiPartySpeechData.xml");
		//dataFile << "<MultiPartySpeech>\n";
		std::cout << "<MultiPartySpeech>\n";
		//dataFile << "<Channel>\n";
		std::cout << "<Channel>\n";
		//dataFile << "<name>" << _name << "</name>\n";
		std::cout << "<name>" << _name << "</name>\n";
		//dataFile << "<speakingLength>" << diffTime << "</speakingLength>\n";
		std::cout << "<speakingLength>" << diffTime << "</speakingLength>\n";
		//dataFile << "<totalSpeakingLength>" << totalSpeakingLength << "</totalSpeakingLength>\n";
		std::cout << "<totalSpeakingLength>" << totalSpeakingLength << "</totalSpeakingLength>\n";
		//dataFile << "<totalSpeakingLengthNoUtterances>" << totalSpeakingLengthNoUtterances << "</totalSpeakingLengthNoUtterances>\n";
		std::cout << "<totalSpeakingLengthNoUtterances>" << totalSpeakingLengthNoUtterances << "</totalSpeakingLengthNoUtterances>\n";
		//dataFile << "<totalSpeakingInterrupts>" << totalSpeakingInterrupts << "</totalSpeakingInterrupts>\n";
		std::cout << "<totalSpeakingInterrupts>" << totalSpeakingInterrupts << "</totalSpeakingInterrupts>\n";
		//dataFile << "<totalSpeakingSuccessfulInterrupts>" << totalSpeakingSuccessfulInterrupts << "</totalSpeakingSuccessfulInterrupts>\n";
		std::cout << "<totalSpeakingSuccessfulInterrupts>" << totalSpeakingSuccessfulInterrupts << "</totalSpeakingSuccessfulInterrupts>\n";
		//dataFile << "<totalSpeakingUnsuccessfulInterrupts>" << totalSpeakingUnsuccessfulInterrupts << "</totalSpeakingUnsuccessfulInterrupts>\n";
		std::cout << "<totalSpeakingUnsuccessfulInterrupts>" << totalSpeakingUnsuccessfulInterrupts << "</totalSpeakingUnsuccessfulInterrupts>\n";
		//dataFile << "<dominancePercentage>" << totalActivityLevel*100 << "</dominancePercentage>\n";
		std::cout << "<dominancePercentage>" << totalActivityLevel*100 << "</dominancePercentage>\n";
		//dataFile << "<isDominant>" << isDominant << "</isDominant>\n";
		std::cout << "<isDominant>" << isDominant << "</isDominant>\n";
		//dataFile << "</Channel>\n\n";
		std::cout << "</Channel>\n";
		//dataFile << "</MultiPartySpeech>\n";
		std::cout << "</MultiPartySpeech>\n\n";

		cumulativeLogFile << "<MultiPartySpeech>\n";
		cumulativeLogFile << "<Channel>\n";
		cumulativeLogFile << "<name>" << _name << "</name>\n";
		cumulativeLogFile << "<speakingLength>" << diffTime << "</speakingLength>\n";
		cumulativeLogFile << "<totalSpeakingLength>" << totalSpeakingLength << "</totalSpeakingLength>\n";
		cumulativeLogFile << "<totalSpeakingLengthNoUtterances>" << totalSpeakingLengthNoUtterances << "</totalSpeakingLengthNoUtterances>\n";
		cumulativeLogFile << "<totalSpeakingInterrupts>" << totalSpeakingInterrupts << "</totalSpeakingInterrupts>\n";
		cumulativeLogFile << "<totalSpeakingSuccessfulInterrupts>" << totalSpeakingSuccessfulInterrupts << "</totalSpeakingSuccessfulInterrupts>\n";
		cumulativeLogFile << "<totalSpeakingUnsuccessfulInterrupts>" << totalSpeakingUnsuccessfulInterrupts << "</totalSpeakingUnsuccessfulInterrupts>\n";
		cumulativeLogFile << "<dominancePercentage>" << totalActivityLevel*100 << "</dominancePercentage>\n";
		cumulativeLogFile << "<isDominant>" << isDominant << "</isDominant>\n";
		cumulativeLogFile << "</Channel>\n";
		cumulativeLogFile << "</MultiPartySpeech>\n\n";

		//dataFile.close();
		cumulativeLogFile.close();
	}
};

} //namespace

#endif
