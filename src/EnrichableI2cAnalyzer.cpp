#include "EnrichableI2cAnalyzer.h"
#include "EnrichableI2cAnalyzerSettings.h"
#include <AnalyzerChannelData.h>

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <wordexp.h>

std::mutex subprocessLock;

EnrichableI2cAnalyzer::EnrichableI2cAnalyzer()
:	Analyzer2(),  
	mSettings( new EnrichableI2cAnalyzerSettings() ),
	mSimulationInitilized( false ),
	featureMarker(true),
	featureBubble(true),
	featureTabular(true)
{
	SetAnalyzerSettings( mSettings.get() );
}

EnrichableI2cAnalyzer::~EnrichableI2cAnalyzer()
{
	KillThread();
}

void EnrichableI2cAnalyzer::SetupResults()
{
	mResults.reset( new EnrichableI2cAnalyzerResults( this, mSettings.get() ) );
	SetAnalyzerResults( mResults.get() );
	mResults->AddChannelBubblesWillAppearOn( mSettings->mSdaChannel );
}

void EnrichableI2cAnalyzer::WorkerThread()
{
	mSampleRateHz = GetSampleRate();
	mNeedAddress = true;

	StartSubprocess();

	mSda = GetAnalyzerChannelData( mSettings->mSdaChannel );
	mScl = GetAnalyzerChannelData( mSettings->mSclChannel );

	AdvanceToStartBit(); 
	mScl->AdvanceToNextEdge(); //now scl is low.

	for( ; ; )
	{
		GetByte();
		CheckIfThreadShouldExit();
	}
	StopSubprocess();
}

void EnrichableI2cAnalyzer::StartSubprocess() {
	if(pipe(inpipefd) < 0) {
		std::cerr << "Failed to create input pipe: ";
		std::cerr << errno;
		std::cerr << "\n";
		exit(errno);
	}
	if(pipe(outpipefd) < 0) {
		std::cerr << "Failed to create output pipe: ";
		std::cerr << errno;
		std::cerr << "\n";
		exit(errno);
	}
	std::cerr << "Starting fork...\n";
	commandPid = fork();

	if(commandPid == 0) {
		std::cerr << "Forked...\n";
		if(dup2(outpipefd[0], STDIN_FILENO) < 0) {
			std::cerr << "Failed to redirect STDIN: ";
			std::cerr << errno;
			std::cerr << "\n";
			exit(errno);
		}
		if(dup2(inpipefd[1], STDOUT_FILENO) < 0) {
			std::cerr << "Failed to redirect STDOUT: ";
			std::cerr << errno;
			std::cerr << "\n";
			exit(errno);
		}

		wordexp_t cmdParsed;
		char *args[25];

		wordexp(mSettings->mParserCommand, &cmdParsed, 0);
		int i;
		for(i = 0; i < cmdParsed.we_wordc; i++) {
			args[i] = cmdParsed.we_wordv[i];
		}
		args[i] = (char*)NULL;

		close(inpipefd[0]);
		close(inpipefd[1]);
		close(outpipefd[0]);
		close(outpipefd[1]);

		execvp(args[0], args);

		std::cerr << "Failed to spawn analyzer subprocess!\n";
	} else {
		close(inpipefd[1]);
		close(outpipefd[0]);
	}

	// Check script to see which features are enabled;
	// * 'no': This feature can be skipped.  This is used to improve
	//   performance by allowing the script to not receive messages for
	//   features it does not support.
	// * 'yes': Send messages of this type.
	// * Anything else: Send messages of this type.  This might be surprising,
	//   but it's more important to me that the default case be simple
	//   than the default case be high-performance.   Scripts are expected
	//   to respond to even unhandled messages.
	featureBubble = GetFeatureEnablement(BUBBLE_PREFIX);
	featureMarker = GetFeatureEnablement(MARKER_PREFIX);
	featureTabular = GetFeatureEnablement(TABULAR_PREFIX);
}

void EnrichableI2cAnalyzer::StopSubprocess() {
	close(inpipefd[0]);
	close(outpipefd[1]);

	kill(commandPid, SIGINT);
}

bool EnrichableI2cAnalyzer::GetFeatureEnablement(const char* feature) {
	std::stringstream outputStream;
	char result[16];
	std::string value;

	outputStream << FEATURE_PREFIX;
	outputStream << UNIT_SEPARATOR;
	outputStream << feature;
	outputStream << LINE_SEPARATOR;
	value = outputStream.str();

	GetScriptResponse(
		value.c_str(),
		value.length(),
		result,
		16
	);
	if(strcmp(result, "no") == 0) {
		std::cerr << "message type \"";
		std::cerr << feature;
		std::cerr << "\" disabled\n";
		return false;
	}
	return true;
}

void EnrichableI2cAnalyzer::LockSubprocess() {
	subprocessLock.lock();
}

void EnrichableI2cAnalyzer::UnlockSubprocess() {
	subprocessLock.unlock();
}

bool EnrichableI2cAnalyzer::GetScriptResponse(
	const char* outBuffer,
	unsigned outBufferLength,
	char* inBuffer,
	unsigned inBufferLength
) {
	bool result;

	LockSubprocess();
	SendOutputLine(outBuffer, outBufferLength);
	result = GetInputLine(inBuffer, inBufferLength);
	UnlockSubprocess();

	return result;
}

bool EnrichableI2cAnalyzer::SendOutputLine(const char* buffer, unsigned bufferLength) {
	//std::cerr << ">> ";
	//std::cerr << buffer;
	write(outpipefd[1], buffer, bufferLength);

	return true;
}

bool EnrichableI2cAnalyzer::GetInputLine(char* buffer, unsigned bufferLength) {
	unsigned bufferPos = 0;
	bool result = false;

	//std::cerr << "<< ";

	while(true) {
		int result = read(inpipefd[0], &buffer[bufferPos], 1);
		if(buffer[bufferPos] == '\n') {
			break;
		}

		//std::cerr << buffer[bufferPos];

		bufferPos++;

		if(bufferPos == bufferLength - 1) {
			break;
		}
	}
	buffer[bufferPos] = '\0';

	//std::cerr << '\n';

	if(strlen(buffer) > 0) {
		result = true;
	}

	return result;
}

AnalyzerResults::MarkerType EnrichableI2cAnalyzer::GetMarkerType(char* buffer, unsigned bufferLength) {
	AnalyzerResults::MarkerType markerType = AnalyzerResults::Dot;

	if(strncmp(buffer, "ErrorDot", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::ErrorDot;
	} else if(strncmp(buffer, "Square", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::Square;
	} else if(strncmp(buffer, "ErrorSquare", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::ErrorSquare;
	} else if(strncmp(buffer, "UpArrow", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::UpArrow;
	} else if(strncmp(buffer, "DownArrow", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::DownArrow;
	} else if(strncmp(buffer, "X", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::X;
	} else if(strncmp(buffer, "ErrorX", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::ErrorX;
	} else if(strncmp(buffer, "Start", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::Start;
	} else if(strncmp(buffer, "Stop", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::Stop;
	} else if(strncmp(buffer, "One", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::One;
	} else if(strncmp(buffer, "Zero", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::Zero;
	} else if(strncmp(buffer, "Dot", strlen(buffer)) == 0) {
		markerType = AnalyzerResults::Dot;
	}

	return markerType;
}



void EnrichableI2cAnalyzer::GetByte()
{
	mArrowLocataions.clear();
	U64 value;
	DataBuilder byte;
	byte.Reset( &value, AnalyzerEnums::MsbFirst, 8 );
	U64 starting_stample = 0;
	U64 potential_ending_sample = 0;
	
	for( U32 i=0; i<8; i++ )
	{
		BitState bit_state;
		U64 scl_rising_edge;
		bool result = GetBitPartOne( bit_state, scl_rising_edge, potential_ending_sample );
		result &= GetBitPartTwo();
		if( result == true )
		{
			mArrowLocataions.push_back( scl_rising_edge );
			byte.AddBit( bit_state );

			if( i == 0 )
				starting_stample = scl_rising_edge;
		}else
		{
			return;
		}
	}

	BitState ack_bit_state;
	U64 scl_rising_edge;
	S64 last_valid_sample = mScl->GetSampleNumber();
	bool result = GetBitPartOne( ack_bit_state, scl_rising_edge, potential_ending_sample );//GetBit( ack_bit_state, scl_rising_edge );
	
	Frame frame;
	frame.mStartingSampleInclusive = starting_stample;
	frame.mEndingSampleInclusive = result ? potential_ending_sample : last_valid_sample;
	frame.mData1 = U8( value );
	frame.mData2 = U8( 0 );

	if( !result )
		frame.mFlags = I2C_MISSING_FLAG_ACK;
	else if( ack_bit_state == BIT_HIGH )
		frame.mFlags = DISPLAY_AS_WARNING_FLAG;
	else
		frame.mFlags = I2C_FLAG_ACK;

	if( mNeedAddress == true && result == true ) //if result is false, then we have already recorded a stop bit and toggled mNeedAddress
	{
		mNeedAddress = false;
		frame.mType = I2cAddress;
	}else
	{
		frame.mType = I2cData;
	}
	U64 frame_index = mResults->AddFrame( frame );

	U32 count = mArrowLocataions.size();
	for( U32 i=0; i<count; i++ )
		mResults->AddMarker( mArrowLocataions[i], AnalyzerResults::UpArrow, mSettings->mSclChannel );

	if(featureMarker) {
		U64 packet_id = mResults->GetNumPackets();
		std::stringstream outputStream;

		outputStream << MARKER_PREFIX;
		outputStream << UNIT_SEPARATOR;
		if(packet_id != INVALID_RESULT_INDEX) {
			outputStream << std::hex << packet_id;
		}
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << frame_index;
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << count;
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << frame.mStartingSampleInclusive;
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << frame.mEndingSampleInclusive;
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << (U64)frame.mType;
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << (U64)frame.mFlags;
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << value;
		outputStream << UNIT_SEPARATOR;
		outputStream << std::hex << (U64)0;
		outputStream << LINE_SEPARATOR;

		std::string outputValue = outputStream.str();

		LockSubprocess();
		SendOutputLine(
			outputValue.c_str(),
			outputValue.length()
		);
		char markerMessage[256];
		while(true) {
			GetInputLine(
				markerMessage,
				256
			);
			if(strlen(markerMessage) > 0) {
				char forever[256];
				strcpy(forever, markerMessage);

				char *sampleNumberStr = strtok(markerMessage, "\t");
				char *channelStr = strtok(NULL, "\t");
				char *markerTypeStr = strtok(NULL, "\t");

				if(sampleNumberStr != NULL && channelStr != NULL && markerTypeStr != NULL) {
					U64 sampleNumber = strtoll(sampleNumberStr, NULL, 16);
					Channel* channel = NULL;
					if(strcmp(channelStr, "sda") == 0) {
						channel = &mSettings->mSdaChannel;
					}
					if(channel != NULL) {
						mResults->AddMarker(
							mArrowLocataions[sampleNumber],
							GetMarkerType(markerTypeStr, strlen(markerTypeStr)),
							*channel
						);
					}
				} else {
					std::cerr << "Unable to tokenize marker message input: \"";
					std::cerr << forever;
					std::cerr << "\"; input should be three tab-delimited fields: ";
					std::cerr << "sample_number\tchannel\tmarker_type\n";
				}
			} else {
				break;
			}
		}
		UnlockSubprocess();
	}

	mResults->CommitResults();

	result &= GetBitPartTwo();
}

bool EnrichableI2cAnalyzer::GetBit( BitState& bit_state, U64& sck_rising_edge )
{
	//SCL must be low coming into this function
	mScl->AdvanceToNextEdge(); //posedge
	sck_rising_edge = mScl->GetSampleNumber();
	mSda->AdvanceToAbsPosition( sck_rising_edge );  //data read on SCL posedge

	bit_state = mSda->GetBitState();
	bool result = true;

	//this while loop is only important if you need to be careful and check for things that that might happen at the very end of a data set, and you don't want to get stuck waithing on a channel that never changes.
	while( mScl->DoMoreTransitionsExistInCurrentData() == false )
	{
		// there are no more SCL transtitions, at least yet.
		if( mSda->DoMoreTransitionsExistInCurrentData() == true )
		{
			//there ARE some SDA transtions, let's double check to make sure there's still no SDA activity
			if( mScl->DoMoreTransitionsExistInCurrentData() == true )
				break;

			//ok, for sure we can advance to the next SDA edge without running past any SCL events.
			mSda->AdvanceToNextEdge();
			RecordStartStopBit();
			result = false;
			//return result;
		}
	}

	mScl->AdvanceToNextEdge(); //negedge; we'll leave the clock here
	while( mSda->WouldAdvancingToAbsPositionCauseTransition( mScl->GetSampleNumber() - 1 ) == true )
	{
		//clock is high -- SDA changes indicate start, stop, etc.
		mSda->AdvanceToNextEdge();
		RecordStartStopBit();
		result = false;
	}

	return result;
}

bool EnrichableI2cAnalyzer::GetBitPartOne( BitState& bit_state, U64& sck_rising_edge, U64& frame_end_sample )
{
	//SCL must be low coming into this function
	mScl->AdvanceToNextEdge(); //posedge
	sck_rising_edge = mScl->GetSampleNumber();
	frame_end_sample = sck_rising_edge;
	mSda->AdvanceToAbsPosition( sck_rising_edge );  //data read on SCL posedge

	bit_state = mSda->GetBitState();

	//clock is on the rising edge, and data is at the same location.

	while( mScl->DoMoreTransitionsExistInCurrentData() == false )
	{
		// there are no more SCL transtitions, at least yet.
		if( mSda->DoMoreTransitionsExistInCurrentData() == true )
		{
			//there ARE some SDA transtions, let's double check to make sure there's still no SDA activity
			if( mScl->DoMoreTransitionsExistInCurrentData() == true )
			{
				if( mScl->GetSampleOfNextEdge() < mSda->GetSampleOfNextEdge() )
					break; //there is not a stop or start condition here, everything is normal - we should jump out now.
			}
			

			//ok, for sure we can advance to the next SDA edge without running past any SCL events.
			mSda->AdvanceToNextEdge();
			mScl->AdvanceToAbsPosition( mSda->GetSampleNumber() ); //clock is still high, we're just moving it to the stop condition here.
			RecordStartStopBit();
			return false;
		}
	}

	//ok, so there are more transitions on the clock channel, so the above code path didn't run.
	U64 sample_of_next_clock_falling_edge = mScl->GetSampleOfNextEdge();
	while( mSda->WouldAdvancingToAbsPositionCauseTransition( sample_of_next_clock_falling_edge - 1 ) == true )
	{
		//clock is high -- SDA changes indicate start, stop, etc.
		mSda->AdvanceToNextEdge();
		mScl->AdvanceToAbsPosition( mSda->GetSampleNumber() ); //advance the clock to match the SDA channel.
		RecordStartStopBit();
		return false;
	}

	if( mScl->DoMoreTransitionsExistInCurrentData() == true )
	{
		frame_end_sample = mScl->GetSampleOfNextEdge();
	}

	return true;
	
}

bool EnrichableI2cAnalyzer::GetBitPartTwo()
{
	//the sda and scl should be synced up, and we are either on a stop/start condition (clock high) or we're on a regular bit( clock high).
	//we also should not expect any more start/stop conditions before the next falling edge, I beleive.

	//move to next falling edge.
	bool result = true;
	mScl->AdvanceToNextEdge();
	while( mSda->WouldAdvancingToAbsPositionCauseTransition( mScl->GetSampleNumber() - 1 ) == true )
	{
		//clock is high -- SDA changes indicate start, stop, etc.
		mSda->AdvanceToNextEdge();
		RecordStartStopBit();
		result = false;
	}
	return result;
}

void EnrichableI2cAnalyzer::RecordStartStopBit()
{
	if( mSda->GetBitState() == BIT_LOW )
	{
		//negedge -> START / restart
		mResults->AddMarker( mSda->GetSampleNumber(), AnalyzerResults::Start, mSettings->mSdaChannel );
	}else
	{
		//posedge -> STOP
		mResults->AddMarker( mSda->GetSampleNumber(), AnalyzerResults::Stop, mSettings->mSdaChannel );
	}
	
	mNeedAddress = true;
	mResults->CommitPacketAndStartNewPacket();
	mResults->CommitResults( );

}

void EnrichableI2cAnalyzer::AdvanceToStartBit()
{
	for( ; ; )
	{
		mSda->AdvanceToNextEdge();

		if( mSda->GetBitState() == BIT_LOW )
		{
			//SDA negedge
			mScl->AdvanceToAbsPosition( mSda->GetSampleNumber() );
			if( mScl->GetBitState() == BIT_HIGH )
				break;
		}
	}
	mResults->AddMarker( mSda->GetSampleNumber(), AnalyzerResults::Start, mSettings->mSdaChannel );
}

bool EnrichableI2cAnalyzer::NeedsRerun()
{
	return false;
}

U32 EnrichableI2cAnalyzer::GenerateSimulationData( U64 minimum_sample_index, U32 device_sample_rate, SimulationChannelDescriptor** simulation_channels )
{
	if( mSimulationInitilized == false )
	{
		mSimulationDataGenerator.Initialize( GetSimulationSampleRate(), mSettings.get() );
		mSimulationInitilized = true;
	}

	return mSimulationDataGenerator.GenerateSimulationData( minimum_sample_index, device_sample_rate, simulation_channels );
}

U32 EnrichableI2cAnalyzer::GetMinimumSampleRateHz()
{
	return 2000000;
}

const char* EnrichableI2cAnalyzer::GetAnalyzerName() const
{
	return "I2C (Enrichable)";
}

const char* GetAnalyzerName()
{
	return "I2C (Enrichable)";
}

Analyzer* CreateAnalyzer()
{
	return new EnrichableI2cAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
	delete analyzer;
}
