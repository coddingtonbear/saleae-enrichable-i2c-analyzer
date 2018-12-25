#ifndef I2C_ANALYZER_SETTINGS
#define I2C_ANALYZER_SETTINGS

#include <AnalyzerSettings.h>
#include <AnalyzerTypes.h>

enum I2cDirection { I2C_READ, I2C_WRITE };
enum I2cResponse { I2C_ACK, I2C_NAK };

enum AddressDisplay { NO_DIRECTION_7, NO_DIRECTION_8, YES_DIRECTION_8 };

class EnrichableI2cAnalyzerSettings : public AnalyzerSettings
{
public:
	EnrichableI2cAnalyzerSettings();
	virtual ~EnrichableI2cAnalyzerSettings();
	
	virtual bool SetSettingsFromInterfaces();
	virtual void LoadSettings( const char* settings );
	virtual const char* SaveSettings();

	void UpdateInterfacesFromSettings();

	Channel mSdaChannel;
	Channel mSclChannel;
	enum AddressDisplay mAddressDisplay;
	const char* mParserCommand;

protected:
	std::auto_ptr< AnalyzerSettingInterfaceChannel > mSdaChannelInterface;
	std::auto_ptr< AnalyzerSettingInterfaceChannel > mSclChannelInterface;
	std::auto_ptr< AnalyzerSettingInterfaceNumberList > mAddressDisplayInterface;
	std::auto_ptr< AnalyzerSettingInterfaceText >		mParserCommandInterface;
};

#endif //I2C_ANALYZER_SETTINGS
