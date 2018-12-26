#include "EnrichableI2cAnalyzer.h"
#include "EnrichableI2cAnalyzerSettings.h"
#include <AnalyzerChannelData.h>

#include <iostream>
#include <sstream>

EnrichableI2cAnalyzer::EnrichableI2cAnalyzer()
:	Analyzer2(),  
	mSettings( new EnrichableI2cAnalyzerSettings() ),
	mSimulationInitilized( false ),
	mSubprocess( new EnrichableAnalyzerSubprocess() )
{
	SetAnalyzerSettings( mSettings.get() );
}

EnrichableI2cAnalyzer::~EnrichableI2cAnalyzer()
{
	KillThread();
}

void EnrichableI2cAnalyzer::SetupResults()
{
	mResults.reset( new EnrichableI2cAnalyzerResults( this, mSettings.get(), mSubprocess.get() ) );
	SetAnalyzerResults( mResults.get() );
	mResults->AddChannelBubblesWillAppearOn( mSettings->mSdaChannel );
}

void EnrichableI2cAnalyzer::WorkerThread()
{
	mSampleRateHz = GetSampleRate();
	mNeedAddress = true;

	mSubprocess->SetParserCommand(mSettings->mParserCommand);
	mSubprocess->Start();

	mSda = GetAnalyzerChannelData( mSettings->mSdaChannel );
	mScl = GetAnalyzerChannelData( mSettings->mSclChannel );

	AdvanceToStartBit(); 
	mScl->AdvanceToNextEdge(); //now scl is low.

	for( ; ; )
	{
		GetByte();
		CheckIfThreadShouldExit();
	}

	mSubprocess->Stop();
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
	U64 frameIndex = mResults->AddFrame( frame );

	U32 count = mArrowLocataions.size();
	for( U32 i=0; i<count; i++ )
		mResults->AddMarker( mArrowLocataions[i], AnalyzerResults::UpArrow, mSettings->mSclChannel );

	if(mSubprocess->MarkerEnabled()) {
		std::vector<EnrichableAnalyzerSubprocess::Marker> markers = mSubprocess->EmitMarker(
			mResults->GetNumPackets(),
			frameIndex,
			frame,
			count
		);

		Channel* channel = NULL;
		for(const EnrichableAnalyzerSubprocess::Marker& marker : markers) {
			if(marker.channelName == "sda") {
				channel = &mSettings->mSdaChannel;
			}
			if(channel != NULL) {
				mResults->AddMarker(
					mArrowLocataions[marker.sampleNumber],
					marker.markerType,
					*channel
				);
			} else {
				std::cerr << "Received marker request for invalid marker: ";
				std::cerr << marker.channelName;
				std::cerr << " ignoring.\n";
			}
		}
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
