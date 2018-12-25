#include "EnrichableI2cAnalyzerSettings.h"

#include <AnalyzerHelpers.h>
#include <cstring>

EnrichableI2cAnalyzerSettings::EnrichableI2cAnalyzerSettings()
:	mSdaChannel( UNDEFINED_CHANNEL ),
	mSclChannel( UNDEFINED_CHANNEL ),
	mAddressDisplay( YES_DIRECTION_8 )
{
	mSdaChannelInterface.reset( new AnalyzerSettingInterfaceChannel() );
	mSdaChannelInterface->SetTitleAndTooltip( "SDA", "Serial Data Line" );
	mSdaChannelInterface->SetChannel( mSdaChannel );

	mSclChannelInterface.reset( new AnalyzerSettingInterfaceChannel() );
	mSclChannelInterface->SetTitleAndTooltip( "SCL", "Serial Clock Line" );
	mSclChannelInterface->SetChannel( mSclChannel );

	mAddressDisplayInterface.reset( new AnalyzerSettingInterfaceNumberList() );
	mAddressDisplayInterface->SetTitleAndTooltip( "Address Display", "Specify how you would like the I2C address to be displayed." );
	mAddressDisplayInterface->AddNumber( YES_DIRECTION_8, "8-bit, read/write bit included [default]", "Displays the address as it would be seen in a microcontroller register (with the read/write bit included)" );
	mAddressDisplayInterface->AddNumber( NO_DIRECTION_8, "8-bit, read/write bit set as 0", "Displays the I2C address as an 8 bit number, but uses 0 in place of the read/write bit" );
	mAddressDisplayInterface->AddNumber( NO_DIRECTION_7, "7-bit, address bits only", "Displays the I2C address as a 7-bit number" );
	mAddressDisplayInterface->SetNumber( mAddressDisplay );

	mParserCommandInterface.reset(new AnalyzerSettingInterfaceText());
	mParserCommandInterface->SetTitleAndTooltip("Enrichment Script", "Command to run for enriching displayed SPI data.");
	mParserCommandInterface->SetTextType(AnalyzerSettingInterfaceText::NormalText);
	mParserCommandInterface->SetText(mParserCommand);

	AddInterface( mSdaChannelInterface.get() );
	AddInterface( mSclChannelInterface.get() );
	AddInterface( mAddressDisplayInterface.get() );
	AddInterface( mParserCommandInterface.get() );

	//AddExportOption( 0, "Export as text/csv file", "text (*.txt);;csv (*.csv)" );
	AddExportOption( 0, "Export as text/csv file" );
	AddExportExtension( 0, "text", "txt" );
	AddExportExtension( 0, "csv", "csv" );

	ClearChannels();
	AddChannel( mSdaChannel, "SDA", false );
	AddChannel( mSclChannel, "SCL", false );
}

EnrichableI2cAnalyzerSettings::~EnrichableI2cAnalyzerSettings()
{
}

bool EnrichableI2cAnalyzerSettings::SetSettingsFromInterfaces()
{
	if( mSdaChannelInterface->GetChannel() == mSclChannelInterface->GetChannel() )
	{
		SetErrorText( "SDA and SCL can't be assigned to the same input." );
		return false;
	}

	mSdaChannel = mSdaChannelInterface->GetChannel();
	mSclChannel = mSclChannelInterface->GetChannel();
	mAddressDisplay = AddressDisplay( U32( mAddressDisplayInterface->GetNumber() ) );

	ClearChannels();
	AddChannel( mSdaChannel, "SDA", true );
	AddChannel( mSclChannel, "SCL", true );

	return true;
}

void EnrichableI2cAnalyzerSettings::LoadSettings( const char* settings )
{
	SimpleArchive text_archive;
	text_archive.SetString( settings );

	const char* name_string;	//the first thing in the archive is the name of the protocol analyzer that the data belongs to.
	text_archive >> &name_string;
	if( strcmp( name_string, "SaleaeEnrichableI2CAnalyzer" ) != 0 )
		AnalyzerHelpers::Assert( "SaleaeEnrichableI2CAnalyzer: Provided with a settings string that doesn't belong to us;" );

	text_archive >> mSdaChannel;
	text_archive >> mSclChannel;
	text_archive >> *(U32*)&mAddressDisplay;
	text_archive >>  &mParserCommand;

	ClearChannels();
	AddChannel( mSdaChannel, "SDA", true );
	AddChannel( mSclChannel, "SCL", true );

	UpdateInterfacesFromSettings();
}

const char* EnrichableI2cAnalyzerSettings::SaveSettings()
{
	SimpleArchive text_archive;

	text_archive << "SaleaeI2CAnalyzer";
	text_archive << mSdaChannel;
	text_archive << mSclChannel;
	text_archive << mAddressDisplay;
	text_archive <<  mParserCommand;

	return SetReturnString( text_archive.GetString() );
}

void EnrichableI2cAnalyzerSettings::UpdateInterfacesFromSettings()
{
	mSdaChannelInterface->SetChannel( mSdaChannel );
	mSclChannelInterface->SetChannel( mSclChannel );
	mAddressDisplayInterface->SetNumber( mAddressDisplay );
	mParserCommandInterface->SetText( mParserCommand );
}
