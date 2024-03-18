#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <stdint.h>
#include <stdbool.h>

#define NUMBER_OF_SETTINGS                      2
#define NUMBER_OF_SLEEP_RECORD_CYCLES           2
#define MAXIMUM_NUMBER_OF_START_STOP_PERIODS    5

typedef enum {CP_WAITING, CP_PARSING, CP_CHARACTER_ERROR, CP_VALUE_ERROR, CP_SUCCESS} CP_parserStatus_t;

typedef struct {
    uint16_t startMinutes;
    uint16_t stopMinutes;
} CP_startStopPeriod_t;

typedef struct {  
    int8_t timezoneHours;
    int8_t timezoneMinutes;
    uint8_t enableLED;
    uint8_t enableLowVoltageCutoff;
    uint8_t enableBatteryLevelDisplay;
    uint8_t enableProprietaryFileFormat;
    uint8_t initialSleepRecordCycles;
    uint8_t numberOfSleepRecordCycles;
    uint8_t enableOpportunisticRecording;
    uint16_t maximumOpportunisticDuration;
    uint32_t maximumTotalOpportunisticFileSize;
    uint16_t sleepDuration[NUMBER_OF_SLEEP_RECORD_CYCLES];
    uint16_t recordDuration[NUMBER_OF_SLEEP_RECORD_CYCLES];
    uint8_t clockDivider[NUMBER_OF_SETTINGS];
    uint8_t acquisitionCycles;
    uint8_t oversampleRate;
    uint8_t gain[NUMBER_OF_SETTINGS];
    uint32_t sampleRate[NUMBER_OF_SETTINGS];
    uint8_t enableEnergySaverMode[NUMBER_OF_SETTINGS];
    uint8_t sampleRateDivider[NUMBER_OF_SETTINGS];
    uint16_t lowerFilterFreq[NUMBER_OF_SETTINGS];
    uint16_t higherFilterFreq[NUMBER_OF_SETTINGS];
    uint16_t amplitudeThreshold[NUMBER_OF_SETTINGS];
    uint8_t activeStartStopPeriods;
    CP_startStopPeriod_t startStopPeriods[MAXIMUM_NUMBER_OF_START_STOP_PERIODS];
    uint32_t earliestRecordingTime;
    uint32_t latestRecordingTime;
} CP_configSettings_t;

void ConfigParser_reset();

CP_parserStatus_t ConfigParser_parse(char c, CP_configSettings_t *result);

#endif /* CONFIGPARSER_H */
