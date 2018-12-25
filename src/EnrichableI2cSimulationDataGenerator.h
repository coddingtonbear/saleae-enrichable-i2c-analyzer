#ifndef SERIAL_SIMULATION_DATA_GENERATOR
#define SERIAL_SIMULATION_DATA_GENERATOR

#include <AnalyzerHelpers.h>
#include "EnrichableI2cAnalyzerSettings.h"
#include <stdlib.h>

class EnrichableI2cSimulationDataGenerator
{
public:
	EnrichableI2cSimulationDataGenerator();
	~EnrichableI2cSimulationDataGenerator();

	void Initialize( U32 simulation_sample_rate, EnrichableI2cAnalyzerSettings* settings );
	U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );

protected:
	EnrichableI2cAnalyzerSettings* mSettings;
	U32 mSimulationSampleRateHz;
	U8 mValue;

protected:	//I2c specific
			//functions
	void CreateI2cTransaction( U8 address, I2cDirection direction, U8 data );
	void CreateI2cByte( U8 data, I2cResponse reply );
	void CreateBit( BitState bit_state );
	void CreateStart();
	void CreateStop();
	void CreateRestart();
	void SafeChangeSda( BitState bit_state );

protected: //vars
	ClockGenerator mClockGenerator;

	SimulationChannelDescriptorGroup mI2cSimulationChannels;
	SimulationChannelDescriptor* mSda;
	SimulationChannelDescriptor* mScl;
};
#endif //UNIO_SIMULATION_DATA_GENERATOR
