/****************************************************************************
 * main.c
 * openacousticdevices.info
 * June 2017
 *****************************************************************************/

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "audiomoth.h"
#include "audioconfig.h"
#include "configparser.h"
#include "digitalfilter.h"

/* Useful time constants */

#define SECONDS_IN_MINUTE                               60
#define SECONDS_IN_HOUR                                 (60 * SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY                                  (24 * SECONDS_IN_HOUR)

#define MINUTES_IN_HOUR                                 60
#define MINUTES_IN_DAY                                  1440
#define YEAR_OFFSET                                     1900
#define MONTH_OFFSET                                    1   

/* Useful type constants */

#define BITS_PER_BYTE                                   8
#define UINT32_SIZE_IN_BITS                             32
#define UINT32_SIZE_IN_BYTES                            4
#define UINT16_SIZE_IN_BYTES                            2

/* Sleep and LED constants */

#define DEFAULT_WAIT_INTERVAL                           1

#define WAITING_LED_FLASH_INTERVAL                      2
#define WAITING_LED_FLASH_DURATION                      10

#define LOW_BATTERY_LED_FLASHES                         10

#define SHORT_LED_FLASH_DURATION                        100
#define LONG_LED_FLASH_DURATION                         500

/* SRAM buffer constants */

#define NUMBER_OF_BUFFERS                               8
#define NUMBER_OF_BYTES_IN_SAMPLE                       2
#define EXTERNAL_SRAM_SIZE_IN_SAMPLES                   (AM_EXTERNAL_SRAM_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE)
#define NUMBER_OF_SAMPLES_IN_BUFFER                     (EXTERNAL_SRAM_SIZE_IN_SAMPLES / NUMBER_OF_BUFFERS)

/* DMA transfer constant */

#define MAXIMUM_SAMPLES_IN_DMA_TRANSFER                 1024

/* Microphone warm-up constant */

#define FRACTION_OF_SECOND_FOR_WARMUP                   2

/* Compression constants */

#define COMPRESSION_BUFFER_SIZE_IN_BYTES                512

/* File size constants */

#define MAXIMUM_WAV_FILE_SIZE                          (UINT32_MAX - 1)

/* WAV header constant */

#define PCM_FORMAT                                      1
#define RIFF_ID_LENGTH                                  4
#define LENGTH_OF_ARTIST                                32
#define LENGTH_OF_COMMENT                               384

/* USB configuration constant */

#define MAX_START_STOP_PERIODS                          5

/* Digital filter constant */

#define FILTER_FREQ_MULTIPLIER                          100

/* DC filter constant */

#define DC_BLOCKING_FREQ                                48

/* Supply monitor constant */

#define MINIMUM_SUPPLY_VOLTAGE                          2800

/* Acoustic location constant */

#define ACOUSTIC_LOCATION_SIZE_IN_BYTES                 7

/* Audio configuration constant */

#define AUDIO_CONFIG_PULSE_INTERVAL                     8
#define AUDIO_CONFIG_TIME_CORRECTION                    2

/* Configuration file read constant */

#define MAX_FILE_READ_CHARACTERS                        8192

#define FILE_READ_BUFFER_LENGTH                         128

/* Device file write constant */

#define FILE_WRITE_BUFFER_LENGTH                        512

/* Maximum total file size constants */

#define TOTAL_FILE_SIZE_UNITS_IN_BYTES                  512

#define NUMBER_OF_BYTES_IN_ONE_MB                       (1024 * 1024)

/* Opportunistic recording constant */

#define OPPORTUNISTIC_GAP_IN_SECONDS                    5

/* Initial sleep / record constants */

#define SLEEP_RECORD_CYCLES_DISABLED                    0

#define INITIAL_AND_STANDARD_SLEEP_RECORD_CYCLES        2

/* Location constants */

#define ACOUSTIC_LONGITUDE_MULTIPLIER                   2

#define ACOUSTIC_LOCATION_PRECISION                     1000000

/* Useful macros */

#define FLASH_LED(led, duration) { \
    AudioMoth_set ## led ## LED(true); \
    AudioMoth_delay(duration); \
    AudioMoth_set ## led ## LED(false); \
}

#define FLASH_LED_AND_RETURN_ON_ERROR(fn) { \
    bool success = (fn); \
    if (success != true) { \
        FLASH_LED(Both, LONG_LED_FLASH_DURATION) \
        return SDCARD_WRITE_ERROR; \
    } \
}

#define RETURN_BOOL_ON_ERROR(fn) { \
    bool success = (fn); \
    if (success != true) { \
        return success; \
    } \
}

#define SAVE_SWITCH_POSITION_AND_POWER_DOWN(duration) { \
    *previousSwitchPosition = switchPosition; \
    AudioMoth_powerDownAndWake(duration, true); \
}

#define SERIAL_NUMBER                           "%08X%08X"

#define FORMAT_SERIAL_NUMBER(src)               (unsigned int)*((uint32_t*)src + 1),  (unsigned int)*((uint32_t*)src)

#define ABS(a)                                  ((a) < (0) ? (-a) : (a))

#define MIN(a, b)                               ((a) < (b) ? (a) : (b))

#define MAX(a, b)                               ((a) > (b) ? (a) : (b))

#define ROUNDED_DIV(a, b)                       (((a) + (b/2)) / (b))

/* Recording state enumeration */

typedef enum {RECORDING_OKAY, TOTAL_FILE_SIZE_LIMITED, FILE_SIZE_LIMITED, SUPPLY_VOLTAGE_LOW, SWITCH_CHANGED, SDCARD_WRITE_ERROR} AM_recordingState_t;

/* Filter type enumeration */

typedef enum {NO_FILTER, LOW_PASS_FILTER, BAND_PASS_FILTER, HIGH_PASS_FILTER} AM_filterType_t;

/* Configuration index enumeration */

typedef enum {INITIAL_SLEEP_RECORD_CYCLE, STANDARD_SLEEP_RECORD_CYCLE} AM_sleepRecordIndex_t;

typedef enum {STANDARD_RECORDING, OPPORTUNISTIC_RECORDING} AM_configurationIndex_t;

/* Acoustic location data structure */

#pragma pack(push, 1)

typedef struct {
    int32_t latitude: 28;
    int32_t longitude: 28;
} acousticLocation_t;

#pragma pack(pop)

/* WAV header */

#pragma pack(push, 1)

typedef struct {
    char id[RIFF_ID_LENGTH];
    uint32_t size;
} chunk_t;

typedef struct {
    chunk_t icmt;
    char comment[LENGTH_OF_COMMENT];
} icmt_t;

typedef struct {
    chunk_t iart;
    char artist[LENGTH_OF_ARTIST];
} iart_t;

typedef struct {
    uint16_t format;
    uint16_t numberOfChannels;
    uint32_t samplesPerSecond;
    uint32_t bytesPerSecond;
    uint16_t bytesPerCapture;
    uint16_t bitsPerSample;
} wavFormat_t;

typedef struct {
    chunk_t riff;
    char format[RIFF_ID_LENGTH];
    chunk_t fmt;
    wavFormat_t wavFormat;
    chunk_t list;
    char info[RIFF_ID_LENGTH];
    icmt_t icmt;
    iart_t iart;
    chunk_t data;
} wavHeader_t;

#pragma pack(pop)

static wavHeader_t wavHeader = {
    .riff = {.id = "RIFF", .size = 0},
    .format = "WAVE",
    .fmt = {.id = "fmt ", .size = sizeof(wavFormat_t)},
    .wavFormat = {.format = PCM_FORMAT, .numberOfChannels = 1, .samplesPerSecond = 0, .bytesPerSecond = 0, .bytesPerCapture = 2, .bitsPerSample = 16},
    .list = {.id = "LIST", .size = RIFF_ID_LENGTH + sizeof(icmt_t) + sizeof(iart_t)},
    .info = "INFO",
    .icmt = {.icmt.id = "ICMT", .icmt.size = LENGTH_OF_COMMENT, .comment = ""},
    .iart = {.iart.id = "IART", .iart.size = LENGTH_OF_ARTIST, .artist = ""},
    .data = {.id = "data", .size = 0}
};

/* Functions to set WAV header details and comment */

static void setHeaderDetails(wavHeader_t *wavHeader, uint32_t sampleRate, uint32_t numberOfSamples, uint32_t guanoHeaderSize) {

    wavHeader->wavFormat.samplesPerSecond = sampleRate;
    wavHeader->wavFormat.bytesPerSecond = NUMBER_OF_BYTES_IN_SAMPLE * sampleRate;
    wavHeader->data.size = NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamples;
    wavHeader->riff.size = NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamples + sizeof(wavHeader_t) + guanoHeaderSize - sizeof(chunk_t);

}

static void setHeaderComment(wavHeader_t *wavHeader, uint32_t currentTime, int8_t timezoneHours, int8_t timezoneMinutes, uint8_t *serialNumber, uint32_t gain, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature, bool switchPositionChanged, bool supplyVoltageLow, bool fileSizeLimited, bool totalFileSizeLimited, uint32_t amplitudeThreshold, AM_filterType_t filterType, uint32_t lowerFilterFreq, uint32_t higherFilterFreq) {

    time_t rawtime = currentTime + timezoneHours * SECONDS_IN_HOUR + timezoneMinutes * SECONDS_IN_MINUTE;

    struct tm *time = gmtime(&rawtime);

    /* Format artist field */

    char *artist = wavHeader->iart.artist;

    sprintf(artist, "AudioMoth %08X%08X", (unsigned int)*((uint32_t*)serialNumber + 1), (unsigned int)*((uint32_t*)serialNumber));

    /* Format comment field */

    char *comment = wavHeader->icmt.comment;

    comment += sprintf(comment, "Recorded at %02d:%02d:%02d %02d/%02d/%04d (UTC", time->tm_hour, time->tm_min, time->tm_sec, time->tm_mday, 1 + time->tm_mon, 1900 + time->tm_year);

    if (timezoneHours < 0) {

        comment += sprintf(comment, "%d", timezoneHours);

    } else if (timezoneHours > 0) {

        comment += sprintf(comment, "+%d", timezoneHours);

    } else {

        if (timezoneMinutes < 0) comment += sprintf(comment, "-%d", timezoneHours);

        if (timezoneMinutes > 0) comment += sprintf(comment, "+%d", timezoneHours);

    }

    if (timezoneMinutes < 0) comment += sprintf(comment, ":%02d", -timezoneMinutes);

    if (timezoneMinutes > 0) comment += sprintf(comment, ":%02d", timezoneMinutes);

    static char *gainSettings[5] = {"low", "low-medium", "medium", "medium-high", "high"};

    comment +=  sprintf(comment, ") by %s at %s gain setting while battery state was ", artist, gainSettings[gain]);

    if (extendedBatteryState == AM_EXT_BAT_LOW) {

        comment += sprintf(comment, "less than 2.5V");

    } else if (extendedBatteryState >= AM_EXT_BAT_FULL) {

        comment += sprintf(comment, "greater than 4.9V");

    } else {

        uint32_t batteryVoltage =  extendedBatteryState + AM_EXT_BAT_STATE_OFFSET / AM_BATTERY_STATE_INCREMENT;

        comment += sprintf(comment, "%01d.%01dV", (unsigned int)batteryVoltage / 10, (unsigned int)batteryVoltage % 10);

    }

    char *sign = temperature < 0 ? "-" : "";

    uint32_t temperatureInDecidegrees = ROUNDED_DIV(ABS(temperature), 100);

    comment += sprintf(comment, " and temperature was %s%d.%dC.", sign, (unsigned int)temperatureInDecidegrees / 10, (unsigned int)temperatureInDecidegrees % 10);

    if (amplitudeThreshold > 0) {

        comment += sprintf(comment, " Amplitude threshold was %d.", (unsigned int)amplitudeThreshold);

    }

    if (filterType == LOW_PASS_FILTER) {

        comment += sprintf(comment, " Low-pass filter applied with cut-off frequency of %01d.%01dkHz.", (unsigned int)higherFilterFreq / 10, (unsigned int)higherFilterFreq % 10);

    } else if (filterType == BAND_PASS_FILTER) {

        comment += sprintf(comment, " Band-pass filter applied with cut-off frequencies of %01d.%01dkHz and %01d.%01dkHz.", (unsigned int)lowerFilterFreq / 10, (unsigned int)lowerFilterFreq % 10, (unsigned int)higherFilterFreq / 10, (unsigned int)higherFilterFreq % 10);

    } else if (filterType == HIGH_PASS_FILTER) {

        comment += sprintf(comment, " High-pass filter applied with cut-off frequency of %01d.%01dkHz.", (unsigned int)lowerFilterFreq / 10, (unsigned int)lowerFilterFreq % 10);

    }

    if (supplyVoltageLow || switchPositionChanged || fileSizeLimited || totalFileSizeLimited) {

        comment += sprintf(comment, " Recording cancelled before completion due to ");

        if (switchPositionChanged) {

            comment += sprintf(comment, "change of switch position.");

        } else if (supplyVoltageLow) {

            comment += sprintf(comment, "low voltage.");

        } else if (fileSizeLimited) {

            comment += sprintf(comment, "file size limit.");

        } else if (totalFileSizeLimited) {

            comment += sprintf(comment, "total file size limit.");

        }

    }

}

/* Configuration data structure */

static const CP_configSettings_t defaultConfigSettings = {
    .timezoneHours = 0,
    .timezoneMinutes = 0,
    .enableLED = 1,
    .enableLowVoltageCutoff = 1,
    .enableBatteryLevelDisplay = 1,
    .enableProprietaryFileFormat = 0,
    .initialSleepRecordCycles = 0,
    .numberOfSleepRecordCycles = 0,
    .enableOpportunisticRecording = 0,
    .maximumOpportunisticDuration = 0,
    .maximumTotalOpportunisticFileSize = 0,
    .sleepDuration = {5, 5},
    .recordDuration = {55, 55},
    .clockDivider = {4, 4},
    .acquisitionCycles = 16,
    .oversampleRate = 1,
    .gain = {2, 2},
    .sampleRate = {384000, 384000},
    .enableEnergySaverMode = {0, 0},
    .sampleRateDivider = {8, 8},
    .lowerFilterFreq = {0, 0},
    .higherFilterFreq = {0, 0},
    .amplitudeThreshold = {0, 0},
    .activeStartStopPeriods = 0,
    .startStopPeriods = {
        {.startMinutes = 000, .stopMinutes = 060},
        {.startMinutes = 120, .stopMinutes = 180},
        {.startMinutes = 240, .stopMinutes = 300},
        {.startMinutes = 360, .stopMinutes = 420},
        {.startMinutes = 480, .stopMinutes = 540}
    },
    .earliestRecordingTime = 0,
    .latestRecordingTime = 0
};

/* Function to write the GUANO data */

static uint32_t writeGuanoData(char *buffer, CP_configSettings_t *configSettings, uint32_t currentTime, uint32_t *acousticLocationReceived, int32_t *acousticLatitude, int32_t *acousticLongitude, uint8_t *firmwareDescription, uint8_t *firmwareVersion, uint8_t *serialNumber, char *filename, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature) {

    uint32_t length = sprintf(buffer, "guan");
    
    length += UINT32_SIZE_IN_BYTES;
    
    length += sprintf(buffer + length, "GUANO|Version:1.0\nMake:Open Acoustic Devices\nModel:AudioMoth\nSerial:" SERIAL_NUMBER "\n", FORMAT_SERIAL_NUMBER(serialNumber));

    length += sprintf(buffer + length, "Firmware Version:%s (%u.%u.%u)\n", firmwareDescription, firmwareVersion[0], firmwareVersion[1], firmwareVersion[2]);

    int32_t timezoneOffset = configSettings->timezoneHours * SECONDS_IN_HOUR + configSettings->timezoneMinutes * SECONDS_IN_MINUTE;

    time_t rawTime = currentTime + timezoneOffset;

    struct tm time;

    gmtime_r(&rawTime, &time);

    length += sprintf(buffer + length, "Timestamp:%04d-%02d-%02dT%02d:%02d:%02d", YEAR_OFFSET + time.tm_year, MONTH_OFFSET + time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);

    if (timezoneOffset == 0) {

        length += sprintf(buffer + length, "Z\n");
        
    } else if (timezoneOffset < 0) {

        length += sprintf(buffer + length, "-%02d:%02d\n", ABS(configSettings->timezoneHours), ABS(configSettings->timezoneMinutes));

    } else {

        length += sprintf(buffer + length, "+%02d:%02d\n", configSettings->timezoneHours, configSettings->timezoneMinutes);

    }

    if (*acousticLocationReceived) {

        char *latitudeSign = *acousticLatitude < 0 ? "-" : "";

        char *longitudeSign = *acousticLongitude < 0 ? "-" : "";

        length += sprintf(buffer + length, "Loc Position:%s%ld.%06ld %s%ld.%06ld\nOAD|Loc Source:Acoustic chime\n", latitudeSign, ABS(*acousticLatitude) / ACOUSTIC_LOCATION_PRECISION, ABS(*acousticLatitude) % ACOUSTIC_LOCATION_PRECISION, longitudeSign, ABS(*acousticLongitude) / ACOUSTIC_LOCATION_PRECISION, ABS(*acousticLongitude) % ACOUSTIC_LOCATION_PRECISION);

    }

    length += sprintf(buffer + length, "Original Filename:%s\n", filename);

    uint32_t batteryVoltage = extendedBatteryState == AM_EXT_BAT_LOW ? 24 : extendedBatteryState >= AM_EXT_BAT_FULL ? 50 : extendedBatteryState + AM_EXT_BAT_STATE_OFFSET / AM_BATTERY_STATE_INCREMENT;

    length += sprintf(buffer + length, "OAD|Battery Voltage:%01lu.%01lu\n", batteryVoltage / 10, batteryVoltage % 10);
    
    char *temperatureSign = temperature < 0 ? "-" : "";

    uint32_t temperatureInDecidegrees = ROUNDED_DIV(ABS(temperature), 100);

    length += sprintf(buffer + length, "Temperature Int:%s%lu.%lu", temperatureSign, temperatureInDecidegrees / 10, temperatureInDecidegrees % 10);
    
    *(uint32_t*)(buffer + RIFF_ID_LENGTH) = length - sizeof(chunk_t);;

    return length;

}

/* Function to write configuration to file */

static bool writeConfigurationToFile(CP_configSettings_t *configSettings, uint8_t *firmwareDescription, uint8_t *firmwareVersion, uint8_t *serialNumber) {

    uint16_t length;

    static char configBuffer[FILE_WRITE_BUFFER_LENGTH];

    RETURN_BOOL_ON_ERROR(AudioMoth_openFile("DEVICE.TXT"));

    length = sprintf(configBuffer, "Device ID                       : %08X%08X\n", (unsigned int)*((uint32_t*)serialNumber + 1), (unsigned int)*((uint32_t*)serialNumber));

    length += sprintf(configBuffer + length, "Firmware                        : %s (%d.%d.%d)\n", firmwareDescription, firmwareVersion[0], firmwareVersion[1], firmwareVersion[2]);

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    RETURN_BOOL_ON_ERROR(AudioMoth_closeFile());

    return true;

}

/* Backup domain variables */

static uint32_t *previousSwitchPosition = (uint32_t*)AM_BACKUP_DOMAIN_START_ADDRESS;

static uint32_t *timeOfNextRecording = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 4);

static AM_configurationIndex_t *configurationIndexOfNextRecording = (AM_configurationIndex_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 8);

static uint32_t *numberOfCompleteInitialRecordings = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 12);

static uint32_t *numberOfRecordings = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 16);

static uint32_t *durationOfNextRecording = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 20);

static uint32_t *readyToMakeRecordings = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 24);

static uint32_t *previousDayOfYear = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 28);

static uint32_t *totalFileSizeWritten = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 32);

static int32_t *acousticLatitude = (int32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 36);

static int32_t *acousticLongitude = (int32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 40);

static uint32_t *acousticLocationReceived = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 44);

static CP_configSettings_t *configSettings = (CP_configSettings_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 48);

/* Filter variables */

static AM_filterType_t requestedFilterType;

/* DMA transfer variable */

static uint32_t numberOfSamplesInDMATransfer;

/* SRAM buffer variables */

static volatile uint32_t writeBuffer;

static volatile uint32_t writeBufferIndex;

static int16_t* buffers[NUMBER_OF_BUFFERS];

/* Initial microphone warm-up period settings */

static uint32_t dmaTransfersToSkip;

static volatile uint32_t dmaTransfersProcessed;

/* Compression buffers */

static bool writeIndicator[NUMBER_OF_BUFFERS];

static int16_t compressionBuffer[COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE];

/* Audio configuration variables */

static bool audioConfigStateLED;

static bool audioConfigToggleLED;

static uint32_t audioConfigPulseCounter;

/* File read variables */

static char fileReadBuffer[FILE_READ_BUFFER_LENGTH];

/* Recording state */

static volatile bool switchPositionChanged;

/* DMA buffers */

static int16_t primaryBuffer[MAXIMUM_SAMPLES_IN_DMA_TRANSFER];

static int16_t secondaryBuffer[MAXIMUM_SAMPLES_IN_DMA_TRANSFER];

/* Current recording file name */

static char filename[32];

/* Firmware version and description */

static uint8_t firmwareVersion[AM_FIRMWARE_VERSION_LENGTH] = {0, 1, 6};

static uint8_t firmwareDescription[AM_FIRMWARE_DESCRIPTION_LENGTH] = "AudioMoth-LIFEPLAN";

/* Function prototypes */

static void flashLedToIndicateBatteryLife(void);

static void scheduleRecording(uint32_t currentTime,  uint32_t *timeOfNextRecording, AM_configurationIndex_t *indexOfNextRecording, uint32_t *durationOfNextRecording);

static AM_recordingState_t makeRecording(uint32_t currentTime, uint32_t recordDuration, bool enableLED, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature);

/* Functions of copy to the backup domain */

static void copyToBackupDomain(uint32_t *dst, uint8_t *src, uint32_t length) {

    uint32_t value = 0;

    for (uint32_t i = 0; i < length / UINT32_SIZE_IN_BYTES; i += 1) {
        *(dst + i) = *((uint32_t*)src + i);
    }

    for (uint32_t i = 0; i < length % UINT32_SIZE_IN_BYTES; i += 1) {
        value = (value << BITS_PER_BYTE) + *(src + length - 1 - i);
    }

    *(dst + length / UINT32_SIZE_IN_BYTES) = value;

}

/* Main function */

int main(void) {

    /* Initialise device */

    AudioMoth_initialise();

    AM_switchPosition_t switchPosition = AudioMoth_getSwitchPosition();

    if (AudioMoth_isInitialPowerUp()) {

        *timeOfNextRecording = 0;

        *configurationIndexOfNextRecording = OPPORTUNISTIC_RECORDING;

        *numberOfCompleteInitialRecordings = 0;

        *numberOfRecordings = 0;

        *durationOfNextRecording = 0;

        *readyToMakeRecordings = false;

        *previousSwitchPosition = AM_SWITCH_NONE;

        *totalFileSizeWritten = 0;

        *previousDayOfYear = UINT32_MAX;

        *acousticLocationReceived = false;

        copyToBackupDomain((uint32_t*)configSettings, (uint8_t*)&defaultConfigSettings, sizeof(CP_configSettings_t));

    }

    /* Handle the case that the switch is in USB position  */

    if (switchPosition == AM_SWITCH_USB) {

        if (configSettings->enableBatteryLevelDisplay && (*previousSwitchPosition == AM_SWITCH_DEFAULT || *previousSwitchPosition == AM_SWITCH_CUSTOM)) {

            flashLedToIndicateBatteryLife();

        }

        AudioMoth_handleUSB();

        SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

    }

    /* Handle the case that the switch is in the DEFAULT position */

    if (switchPosition == AM_SWITCH_DEFAULT) {

        audioConfigPulseCounter = 0;

        audioConfigStateLED = false;

        audioConfigToggleLED = false;

        if (AudioMoth_hasTimeBeenSet()) AudioMoth_setGreenLED(true);

        AudioConfig_handleAudioConfiguration();

        AudioMoth_setGreenLED(false);

        SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

    }

    /* Determine the current time */

    uint32_t currentTime;

    AudioMoth_getTime(&currentTime, NULL);

    /* Make necessary preparation on change to CUSTOM position */
   
    if (switchPosition != *previousSwitchPosition) {

        /* Check time has been set */

        *readyToMakeRecordings = AudioMoth_hasTimeBeenSet();

        /* Check can access SD card */

        bool fileSystemEnabled = false;

        if (*readyToMakeRecordings) {

            fileSystemEnabled = AudioMoth_enableFileSystem(AM_SD_CARD_NORMAL_SPEED);

            *readyToMakeRecordings = fileSystemEnabled;

        }

        /* Check can read configuration file from the SD card */

        if (*readyToMakeRecordings) *readyToMakeRecordings = AudioMoth_openFileToRead("CONFIG.TXT");

        /* Check can parse configuration file from the SD card */

        if (*readyToMakeRecordings) {

            uint32_t count = 0;

            CP_parserStatus_t status = CP_WAITING;

            static CP_configSettings_t tempConfigSettings;

            memcpy(&tempConfigSettings, &defaultConfigSettings, sizeof(CP_configSettings_t));

            while (count < MAX_FILE_READ_CHARACTERS) {

                if (count % FILE_READ_BUFFER_LENGTH == 0) {

                    AudioMoth_readFile(fileReadBuffer, FILE_READ_BUFFER_LENGTH);

                }

                status = ConfigParser_parse(fileReadBuffer[count % FILE_READ_BUFFER_LENGTH], &tempConfigSettings);

                if (status == CP_CHARACTER_ERROR || status == CP_VALUE_ERROR || status == CP_SUCCESS) break;

                count += 1;

            }

            if (status == CP_SUCCESS) {

                tempConfigSettings.timezoneHours = 0;

                tempConfigSettings.timezoneMinutes = 0;

                copyToBackupDomain((uint32_t*)configSettings, (uint8_t*)&tempConfigSettings, sizeof(CP_configSettings_t));

            } else {

                *readyToMakeRecordings = false;

            }

            AudioMoth_closeFile();

        }

        /* Write configuration file to SD card */

        if (*readyToMakeRecordings) *readyToMakeRecordings = writeConfigurationToFile(configSettings, firmwareDescription, firmwareVersion, (uint8_t*)AM_UNIQUE_ID_START_ADDRESS);

        /* Schedule recording */

        if (*readyToMakeRecordings) {

            *configurationIndexOfNextRecording = OPPORTUNISTIC_RECORDING;

            *numberOfCompleteInitialRecordings = 0;

            *numberOfRecordings = 0;

            *previousDayOfYear = UINT32_MAX;

            AudioMoth_getTime(&currentTime, NULL);

            scheduleRecording(currentTime, timeOfNextRecording, configurationIndexOfNextRecording, durationOfNextRecording);

            SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

        }

    }

    /* Flash warning if not ready to make recording */

    if (*readyToMakeRecordings == false) {

        FLASH_LED(Both, SHORT_LED_FLASH_DURATION)

        SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

    }

    /* Make recording if ready to do so */

    if (currentTime >= *timeOfNextRecording) {

        /* Reduce the recording duration if necessary */

        uint32_t missedSeconds = MIN(currentTime - *timeOfNextRecording, *durationOfNextRecording);

        *durationOfNextRecording -= missedSeconds;

        /* Make the recording */

        AM_recordingState_t recordingState = RECORDING_OKAY;

        if (*durationOfNextRecording > 0) {

            /* Measure battery voltage */

            uint32_t supplyVoltage = AudioMoth_getSupplyVoltage();

            AM_extendedBatteryState_t extendedBatteryState = AudioMoth_getExtendedBatteryState(supplyVoltage);

            /* Check if low voltage check is enabled and that the voltage is okay */

            bool okayToMakeRecording = true;

            if (configSettings->enableLowVoltageCutoff) {

                AudioMoth_enableSupplyMonitor();

                AudioMoth_setSupplyMonitorThreshold(MINIMUM_SUPPLY_VOLTAGE);

                okayToMakeRecording = AudioMoth_isSupplyAboveThreshold();

            }

            /* Make recording if okay */

            if (okayToMakeRecording) {

                AudioMoth_enableTemperature();

                int32_t temperature = AudioMoth_getTemperature();

                AudioMoth_disableTemperature();

                if (configSettings->enableEnergySaverMode[*configurationIndexOfNextRecording]) AudioMoth_setClockDivider(AM_HF_CLK_DIV2);

                bool fileSystemEnabled = AudioMoth_enableFileSystem(configSettings->sampleRateDivider[*configurationIndexOfNextRecording] == 1 ? AM_SD_CARD_HIGH_SPEED : AM_SD_CARD_NORMAL_SPEED);

                if (fileSystemEnabled) {

                    recordingState = makeRecording(currentTime, *durationOfNextRecording, configSettings->enableLED, extendedBatteryState, temperature);

                } else {

                    FLASH_LED(Both, LONG_LED_FLASH_DURATION);

                    recordingState = SDCARD_WRITE_ERROR;

                }

            } else {

                if (configSettings->enableLED) FLASH_LED(Both, LONG_LED_FLASH_DURATION);

                recordingState = SUPPLY_VOLTAGE_LOW;

            }

            /* Disable low voltage monitor if it was used */

            if (configSettings->enableLowVoltageCutoff) AudioMoth_disableSupplyMonitor();

        }

        /* Schedule next recording */

        if (recordingState != FILE_SIZE_LIMITED) {

            scheduleRecording(currentTime + *durationOfNextRecording, timeOfNextRecording, configurationIndexOfNextRecording, durationOfNextRecording);

        }

        /* Count the recording if it finished okay and was a full recording */

        if (recordingState == RECORDING_OKAY && configSettings->numberOfSleepRecordCycles == INITIAL_AND_STANDARD_SLEEP_RECORD_CYCLES && *durationOfNextRecording == configSettings->recordDuration[INITIAL_SLEEP_RECORD_CYCLE]) *numberOfCompleteInitialRecordings += 1;

        *numberOfRecordings += 1;

    } else if (configSettings->enableLED) {

        /* Flash LED to indicate waiting */

        FLASH_LED(Green, WAITING_LED_FLASH_DURATION);

    }

    /* Determine how long to power down */

    uint32_t secondsToSleep = 0;

    if (*timeOfNextRecording > currentTime) {

        secondsToSleep = MIN(*timeOfNextRecording - currentTime, WAITING_LED_FLASH_INTERVAL);

    }

    /* Power down */

    SAVE_SWITCH_POSITION_AND_POWER_DOWN(secondsToSleep);

}

/* Time zone handler */

inline void AudioMoth_timezoneRequested(int8_t *timezoneHours, int8_t *timezoneMinutes) {

    *timezoneHours = configSettings->timezoneHours;

    *timezoneMinutes = configSettings->timezoneMinutes;

}


/* AudioMoth interrupt handlers */

inline void AudioMoth_handleSwitchInterrupt() {

    switchPositionChanged = true;

    AudioConfig_cancelAudioConfiguration();

}

inline void AudioMoth_handleMicrophoneChangeInterrupt() { }

inline void AudioMoth_handleDirectMemoryAccessInterrupt(bool isPrimaryBuffer, int16_t **nextBuffer) {

    int16_t *source = secondaryBuffer;

    if (isPrimaryBuffer) source = primaryBuffer;

    /* Update the current buffer index and write buffer */

    bool thresholdExceeded = DigitalFilter_filter(source, buffers[writeBuffer] + writeBufferIndex, configSettings->sampleRateDivider[*configurationIndexOfNextRecording], numberOfSamplesInDMATransfer, configSettings->amplitudeThreshold[*configurationIndexOfNextRecording]);

    if (dmaTransfersProcessed > dmaTransfersToSkip) {

        writeIndicator[writeBuffer] |= thresholdExceeded;

        writeBufferIndex += numberOfSamplesInDMATransfer / configSettings->sampleRateDivider[*configurationIndexOfNextRecording];

        if (writeBufferIndex == NUMBER_OF_SAMPLES_IN_BUFFER) {

            writeBufferIndex = 0;

            writeBuffer = (writeBuffer + 1) & (NUMBER_OF_BUFFERS - 1);

            writeIndicator[writeBuffer] = false;

        }

    }

    dmaTransfersProcessed += 1;

}

/* AudioMoth USB message handlers */

inline void AudioMoth_usbFirmwareVersionRequested(uint8_t **firmwareVersionPtr) {

    *firmwareVersionPtr = firmwareVersion;

}

inline void AudioMoth_usbFirmwareDescriptionRequested(uint8_t **firmwareDescriptionPtr) {

    *firmwareDescriptionPtr = firmwareDescription;

}

inline void AudioMoth_usbApplicationPacketRequested(uint32_t messageType, uint8_t *transmitBuffer, uint32_t size) { }

inline void AudioMoth_usbApplicationPacketReceived(uint32_t messageType, uint8_t* receiveBuffer, uint8_t *transmitBuffer, uint32_t size) { }

/* Audio configuration handlers */

void AudioConfig_handleAudioConfigurationEvent(AC_audioConfigurationEvent_t event) {

    if (!AudioMoth_hasTimeBeenSet()) {

        if (event == AC_EVENT_PULSE) {

            audioConfigPulseCounter = (audioConfigPulseCounter + 1) % AUDIO_CONFIG_PULSE_INTERVAL;

        } else if (event == AC_EVENT_START) {

            audioConfigStateLED = true;

            audioConfigToggleLED = true;

        } else if (event == AC_EVENT_BYTE) {

            audioConfigToggleLED = !audioConfigToggleLED;

        } else if (event == AC_EVENT_BIT_ERROR || event == AC_EVENT_CRC_ERROR) {

            audioConfigStateLED = false;

        }

        AudioMoth_setGreenLED((audioConfigStateLED && audioConfigToggleLED) || (!audioConfigStateLED && !audioConfigPulseCounter));

    }

}

void AudioConfig_handleAudioConfigurationPacket(uint8_t *receiveBuffer, uint32_t size) {

    uint32_t standardPacketSize = UINT32_SIZE_IN_BYTES + UINT16_SIZE_IN_BYTES;

    bool standardPacket = size == standardPacketSize;

    bool hasLocation = size == standardPacketSize + ACOUSTIC_LOCATION_SIZE_IN_BYTES;

    if (AudioMoth_hasTimeBeenSet() == false && (standardPacket || hasLocation)) {

        uint32_t time;

        memcpy(&time, receiveBuffer, UINT32_SIZE_IN_BYTES);

        AudioMoth_setTime(time + AUDIO_CONFIG_TIME_CORRECTION, 0);

        AudioMoth_setGreenLED(true);

        if (hasLocation) {

            acousticLocation_t location;

            memcpy(&location, receiveBuffer + standardPacketSize, ACOUSTIC_LOCATION_SIZE_IN_BYTES);

            *acousticLocationReceived = true;

            *acousticLatitude = location.latitude;
            
            *acousticLongitude = location.longitude * ACOUSTIC_LONGITUDE_MULTIPLIER;

        }

    }

    /* Reset receive state */

    audioConfigStateLED = false;

    audioConfigPulseCounter = 0;

}

/* Encode the compression buffer */

static void encodeCompressionBuffer(uint32_t numberOfCompressedBuffers) {

    for (uint32_t i = 0; i < UINT32_SIZE_IN_BITS; i += 1) {

        compressionBuffer[i] = numberOfCompressedBuffers & 0x01 ? 1 : -1;

        numberOfCompressedBuffers >>= 1;

    }

    for (uint32_t i = UINT32_SIZE_IN_BITS; i < COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE; i += 1) {

        compressionBuffer[i] = 0;

    }

}

/* Save recording to SD card */

static AM_recordingState_t makeRecording(uint32_t currentTime, uint32_t recordDuration, bool enableLED, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature) {

    /* Initialise buffers */

    writeBuffer = 0;

    writeBufferIndex = 0;

    buffers[0] = (int16_t*)AM_EXTERNAL_SRAM_START_ADDRESS;

    for (uint32_t i = 1; i < NUMBER_OF_BUFFERS; i += 1) {
        buffers[i] = buffers[i - 1] + NUMBER_OF_SAMPLES_IN_BUFFER;
    }

    /* Calculate effective sample rate */

    uint32_t effectiveSampleRate = configSettings->sampleRate[*configurationIndexOfNextRecording] / configSettings->sampleRateDivider[*configurationIndexOfNextRecording];

    /* Set up the digital filter */

    if (configSettings->lowerFilterFreq[*configurationIndexOfNextRecording] == 0 && configSettings->higherFilterFreq[*configurationIndexOfNextRecording] == 0) {

        requestedFilterType = NO_FILTER;

        DigitalFilter_designHighPassFilter(effectiveSampleRate, DC_BLOCKING_FREQ);

    } else if (configSettings->lowerFilterFreq[*configurationIndexOfNextRecording] == UINT16_MAX) {

        requestedFilterType = LOW_PASS_FILTER;

        DigitalFilter_designBandPassFilter(effectiveSampleRate, DC_BLOCKING_FREQ, FILTER_FREQ_MULTIPLIER * configSettings->higherFilterFreq[*configurationIndexOfNextRecording]);

    } else if (configSettings->higherFilterFreq[*configurationIndexOfNextRecording] == UINT16_MAX) {

        requestedFilterType = HIGH_PASS_FILTER;

        DigitalFilter_designHighPassFilter(effectiveSampleRate, MAX(DC_BLOCKING_FREQ, FILTER_FREQ_MULTIPLIER * configSettings->lowerFilterFreq[*configurationIndexOfNextRecording]));

    } else {

        requestedFilterType = BAND_PASS_FILTER;

        DigitalFilter_designBandPassFilter(effectiveSampleRate, MAX(DC_BLOCKING_FREQ, FILTER_FREQ_MULTIPLIER * configSettings->lowerFilterFreq[*configurationIndexOfNextRecording]), FILTER_FREQ_MULTIPLIER * configSettings->higherFilterFreq[*configurationIndexOfNextRecording]);

    }

    /* Calculate the sample multiplier */

    float sampleMultiplier = 16.0f / (float)(configSettings->oversampleRate * configSettings->sampleRateDivider[*configurationIndexOfNextRecording]);

    DigitalFilter_applyAdditionalGain(sampleMultiplier);

    /* Calculate the number of samples in each DMA transfer */

    numberOfSamplesInDMATransfer = MAXIMUM_SAMPLES_IN_DMA_TRANSFER / configSettings->sampleRateDivider[*configurationIndexOfNextRecording];

    while (numberOfSamplesInDMATransfer & (numberOfSamplesInDMATransfer - 1)) {

        numberOfSamplesInDMATransfer = numberOfSamplesInDMATransfer & (numberOfSamplesInDMATransfer - 1);

    }

    numberOfSamplesInDMATransfer *= configSettings->sampleRateDivider[*configurationIndexOfNextRecording];

    /* Set up the DMA transfers to skip */

    dmaTransfersProcessed = 0;

    dmaTransfersToSkip = configSettings->sampleRate[*configurationIndexOfNextRecording] / FRACTION_OF_SECOND_FOR_WARMUP / numberOfSamplesInDMATransfer;

    /* Calculate recording parameters */

    uint32_t numberOfBytesInHeader = sizeof(wavHeader);

    uint32_t numberOfSamplesInHeader = numberOfBytesInHeader / NUMBER_OF_BYTES_IN_SAMPLE;

    uint32_t maximumNumberOfSeconds = (MAXIMUM_WAV_FILE_SIZE - numberOfBytesInHeader) / NUMBER_OF_BYTES_IN_SAMPLE / effectiveSampleRate;

    bool fileSizeLimited = (recordDuration > maximumNumberOfSeconds);

    uint32_t numberOfSamples = effectiveSampleRate * (fileSizeLimited ? maximumNumberOfSeconds : recordDuration);

    /* Reset total buffers written today */

    time_t rawtime = currentTime + configSettings->timezoneHours * SECONDS_IN_HOUR + configSettings->timezoneMinutes * SECONDS_IN_MINUTE;

    struct tm *time = gmtime(&rawtime);

    uint32_t dayOfYear = time->tm_yday;

    if (dayOfYear != *previousDayOfYear) {

        *totalFileSizeWritten = 0;

        *previousDayOfYear = dayOfYear;

    }

    uint32_t maximumFileSizeWritten = configSettings->maximumTotalOpportunisticFileSize * NUMBER_OF_BYTES_IN_ONE_MB / TOTAL_FILE_SIZE_UNITS_IN_BYTES;

    if (*configurationIndexOfNextRecording == OPPORTUNISTIC_RECORDING && *totalFileSizeWritten > maximumFileSizeWritten) return TOTAL_FILE_SIZE_LIMITED;

    /* Initialise microphone for recording */

    AudioMoth_enableExternalSRAM();

    AudioMoth_enableMicrophone(AM_NORMAL_GAIN_RANGE, configSettings->gain[*configurationIndexOfNextRecording], configSettings->clockDivider[*configurationIndexOfNextRecording], configSettings->acquisitionCycles, configSettings->oversampleRate);

    AudioMoth_initialiseDirectMemoryAccess(primaryBuffer, secondaryBuffer, numberOfSamplesInDMATransfer);

    AudioMoth_startMicrophoneSamples(configSettings->sampleRate[*configurationIndexOfNextRecording]);

    /* Show LED for SD card activity */
   
    if (enableLED) AudioMoth_setRedLED(true);

    /* Open a file with the current local time as the name */

    uint32_t length = sprintf(filename, "%04d%02d%02d_%02d%02d%02d", YEAR_OFFSET + time->tm_year, MONTH_OFFSET + time->tm_mon, time->tm_mday, time->tm_hour, time->tm_min, time->tm_sec);

    static char *extensions[4] = {".WAV", "T.WAV"};

    uint32_t extensionIndex = configSettings->amplitudeThreshold[*configurationIndexOfNextRecording] > 0 ? 1 : 0;

    strcpy(filename + length, extensions[extensionIndex]);

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_openFile(filename));

    AudioMoth_setRedLED(false);

    /* Termination conditions */

    switchPositionChanged = false;

    bool supplyVoltageLow = false;

    bool totalFileSizeLimited = false;

    /* Main record loop */

    uint32_t readBuffer = 0;

    uint32_t samplesWritten = 0;

    uint32_t buffersProcessed = 0;

    uint32_t numberOfCompressedBuffers = 0;

    uint32_t totalNumberOfCompressedSamples = 0;

    /* Ensure main loop doesn't start if the last buffer is currently being written to */

    while (writeBuffer == NUMBER_OF_BUFFERS - 1) { }

    /* Main recording loop */

    while (samplesWritten < numberOfSamples + numberOfSamplesInHeader && !switchPositionChanged && !supplyVoltageLow && !totalFileSizeLimited) {

        while (readBuffer != writeBuffer && samplesWritten < numberOfSamples + numberOfSamplesInHeader && !switchPositionChanged && !supplyVoltageLow && !totalFileSizeLimited) {

            /* Write the appropriate number of bytes to the SD card */

            uint32_t numberOfSamplesToWrite = MIN(numberOfSamples + numberOfSamplesInHeader - samplesWritten, NUMBER_OF_SAMPLES_IN_BUFFER);

            if (!writeIndicator[readBuffer] && buffersProcessed > 0 && numberOfSamplesToWrite == NUMBER_OF_SAMPLES_IN_BUFFER) {

                numberOfCompressedBuffers += NUMBER_OF_BYTES_IN_SAMPLE * NUMBER_OF_SAMPLES_IN_BUFFER / COMPRESSION_BUFFER_SIZE_IN_BYTES;

            } else {

                /* Light LED during SD card write if appropriate */

                if (enableLED) AudioMoth_setRedLED(true);

                /* Encode and write compression buffer */

                if (numberOfCompressedBuffers > 0) {

                    encodeCompressionBuffer(numberOfCompressedBuffers);

                    totalNumberOfCompressedSamples += (numberOfCompressedBuffers - 1) * COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE;

                    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(compressionBuffer, COMPRESSION_BUFFER_SIZE_IN_BYTES));

                    if (*configurationIndexOfNextRecording == OPPORTUNISTIC_RECORDING) *totalFileSizeWritten += COMPRESSION_BUFFER_SIZE_IN_BYTES / TOTAL_FILE_SIZE_UNITS_IN_BYTES;

                    numberOfCompressedBuffers = 0;

                }

                /* Write the buffer */

                FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(buffers[readBuffer], NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamplesToWrite));

                if (*configurationIndexOfNextRecording == OPPORTUNISTIC_RECORDING) *totalFileSizeWritten += NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamplesToWrite / TOTAL_FILE_SIZE_UNITS_IN_BYTES;

                /* Clear LED */

                AudioMoth_setRedLED(false);

            }

            /* Increment buffer counters */

            readBuffer = (readBuffer + 1) & (NUMBER_OF_BUFFERS - 1);

            samplesWritten += numberOfSamplesToWrite;

            buffersProcessed += 1;

            /* Check if the total file size limit has been exceeded */

            if (*configurationIndexOfNextRecording == OPPORTUNISTIC_RECORDING && *totalFileSizeWritten > maximumFileSizeWritten) totalFileSizeLimited = true;

        }

        /* Check the voltage level */

        if (configSettings->enableLowVoltageCutoff && !AudioMoth_isSupplyAboveThreshold()) {

            supplyVoltageLow = true;

        }

        /* Sleep until next DMA transfer is complete */

        AudioMoth_sleep();

    }

    /* Write the compression buffer files at the end */

    if (samplesWritten < numberOfSamples + numberOfSamplesInHeader && numberOfCompressedBuffers > 0) {

        /* Light LED during SD card write if appropriate */

        if (enableLED) AudioMoth_setRedLED(true);

        /* Encode and write compression buffer */

        encodeCompressionBuffer(numberOfCompressedBuffers);

        totalNumberOfCompressedSamples += (numberOfCompressedBuffers - 1) * COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE;

        FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(compressionBuffer, COMPRESSION_BUFFER_SIZE_IN_BYTES));

        if (*configurationIndexOfNextRecording == OPPORTUNISTIC_RECORDING) *totalFileSizeWritten += COMPRESSION_BUFFER_SIZE_IN_BYTES / TOTAL_FILE_SIZE_UNITS_IN_BYTES;

        /* Clear LED */

        AudioMoth_setRedLED(false);

    }

    /* Write the GUANO data */

    uint32_t guanoDataSize = writeGuanoData((char*)compressionBuffer, configSettings, currentTime, acousticLocationReceived, acousticLatitude, acousticLongitude, firmwareDescription, firmwareVersion, (uint8_t*)AM_UNIQUE_ID_START_ADDRESS, filename, extendedBatteryState, temperature);

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(compressionBuffer, guanoDataSize));

    /* Initialise the WAV header */

    samplesWritten = MAX(numberOfSamplesInHeader, samplesWritten);

    setHeaderDetails(&wavHeader, effectiveSampleRate, samplesWritten - numberOfSamplesInHeader - totalNumberOfCompressedSamples, guanoDataSize);

    setHeaderComment(&wavHeader, currentTime, configSettings->timezoneHours, configSettings->timezoneMinutes, (uint8_t*)AM_UNIQUE_ID_START_ADDRESS, configSettings->gain[*configurationIndexOfNextRecording], extendedBatteryState, temperature, switchPositionChanged, supplyVoltageLow, fileSizeLimited, totalFileSizeLimited, configSettings->amplitudeThreshold[*configurationIndexOfNextRecording], requestedFilterType, configSettings->lowerFilterFreq[*configurationIndexOfNextRecording], configSettings->higherFilterFreq[*configurationIndexOfNextRecording]);

    /* Write the header */

    if (enableLED) AudioMoth_setRedLED(true);

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_seekInFile(0));

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(&wavHeader, sizeof(wavHeader)));

    /* Close the file */

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_closeFile());

    AudioMoth_setRedLED(false);

    /* Return with state */

    if (switchPositionChanged) return SWITCH_CHANGED;

    if (supplyVoltageLow) return SUPPLY_VOLTAGE_LOW;

    if (fileSizeLimited) return FILE_SIZE_LIMITED;

    if (totalFileSizeLimited) return TOTAL_FILE_SIZE_LIMITED;

    return RECORDING_OKAY;

}

/* Schedule recordings */

static void scheduleRecording(uint32_t currentTime, uint32_t *timeOfNextRecording, AM_configurationIndex_t *configurationIndexOfNextRecording, uint32_t *durationOfNextRecording) {

    /* Remember previous configuration type */

    AM_configurationIndex_t configurationIndexOfPreviousRecording = *configurationIndexOfNextRecording;

    *configurationIndexOfNextRecording = STANDARD_RECORDING;

    /* Determine which sleep / record cycle to use */

    AM_sleepRecordIndex_t sleepRecordCycleIndexOfNextRecording = STANDARD_SLEEP_RECORD_CYCLE;

    if (configSettings->numberOfSleepRecordCycles == INITIAL_AND_STANDARD_SLEEP_RECORD_CYCLES) {

        if (*numberOfRecordings == 0 || *numberOfCompleteInitialRecordings < configSettings->initialSleepRecordCycles) {

            sleepRecordCycleIndexOfNextRecording = INITIAL_SLEEP_RECORD_CYCLE;

        }

    }

    /* Check number of active state stop periods */

    uint32_t activeStartStopPeriods = MIN(configSettings->activeStartStopPeriods, MAX_START_STOP_PERIODS);

    /* No active periods */

    if (activeStartStopPeriods == 0) {

        *timeOfNextRecording = UINT32_MAX;

        *durationOfNextRecording = 0;

        goto done;

    }

    /* Check if recording should be limited by earliest recording time */

    if (configSettings->earliestRecordingTime > 0) {

        currentTime = MAX(currentTime, configSettings->earliestRecordingTime);

    }

    /* Calculate the number of seconds of this day */

    time_t rawtime = currentTime;

    struct tm *time = gmtime(&rawtime);

    uint32_t currentSeconds = SECONDS_IN_HOUR * time->tm_hour + SECONDS_IN_MINUTE * time->tm_min + time->tm_sec;

    /* Check each active start stop period */

    for (uint32_t i = 0; i < activeStartStopPeriods; i += 1) {

        CP_startStopPeriod_t *period = configSettings->startStopPeriods + i;

        /* Calculate the start and stop time of the current period */

        uint32_t startSeconds = SECONDS_IN_MINUTE * period->startMinutes;

        uint32_t stopSeconds = SECONDS_IN_MINUTE * period->stopMinutes;

        uint32_t durationOfStartStopPeriod = stopSeconds - startSeconds;

        /* Check if the start stop period has not yet started */

        if (currentSeconds <= startSeconds) {

            *timeOfNextRecording = currentTime + startSeconds - currentSeconds;

            if (configSettings->numberOfSleepRecordCycles == SLEEP_RECORD_CYCLES_DISABLED) {

                *durationOfNextRecording = durationOfStartStopPeriod;

            } else {

                *durationOfNextRecording = MIN(configSettings->recordDuration[sleepRecordCycleIndexOfNextRecording], durationOfStartStopPeriod);

            }

            goto done;

        }

        /* Check if currently inside a start stop period */

        if (currentSeconds < stopSeconds) {

            /* Handle case with no sleep record cycle */

            uint32_t secondsFromStartOfPeriod = currentSeconds - startSeconds;

            if (configSettings->numberOfSleepRecordCycles == SLEEP_RECORD_CYCLES_DISABLED) {

                *timeOfNextRecording = currentTime;

                *durationOfNextRecording = durationOfStartStopPeriod - secondsFromStartOfPeriod;;

                goto done;

            }

            /* Check if recording should start immediately */

            uint32_t durationOfCycle = configSettings->recordDuration[sleepRecordCycleIndexOfNextRecording] + configSettings->sleepDuration[sleepRecordCycleIndexOfNextRecording];

            uint32_t partialCycle = secondsFromStartOfPeriod % durationOfCycle;

            if (partialCycle < configSettings->recordDuration[sleepRecordCycleIndexOfNextRecording]) {

                *timeOfNextRecording = currentTime;

                *durationOfNextRecording = MIN(configSettings->recordDuration[sleepRecordCycleIndexOfNextRecording] - partialCycle, durationOfStartStopPeriod - secondsFromStartOfPeriod);

                goto done;

            }

            /* Wait for next cycle to begin */

            secondsFromStartOfPeriod += durationOfCycle - partialCycle;

            if (secondsFromStartOfPeriod < durationOfStartStopPeriod) {

                *timeOfNextRecording = currentTime + durationOfCycle - partialCycle;

                *durationOfNextRecording = MIN(configSettings->recordDuration[sleepRecordCycleIndexOfNextRecording], durationOfStartStopPeriod - secondsFromStartOfPeriod);

                /* Make opportunistic recording if possible */

                if (configSettings->enableOpportunisticRecording && configurationIndexOfPreviousRecording == STANDARD_RECORDING && *timeOfNextRecording - currentTime > 2 * OPPORTUNISTIC_GAP_IN_SECONDS) {

                    *timeOfNextRecording = *timeOfNextRecording - configSettings->sleepDuration[sleepRecordCycleIndexOfNextRecording] + OPPORTUNISTIC_GAP_IN_SECONDS;

                    *durationOfNextRecording = configSettings->sleepDuration[sleepRecordCycleIndexOfNextRecording] - 2 * OPPORTUNISTIC_GAP_IN_SECONDS;

                    if (configSettings->maximumOpportunisticDuration > 0) *durationOfNextRecording = MIN(configSettings->maximumOpportunisticDuration, *durationOfNextRecording);

                    *configurationIndexOfNextRecording = OPPORTUNISTIC_RECORDING;

                }

                goto done;

            }

        }

    }

    /* Calculate time until first period tomorrow */

    CP_startStopPeriod_t *firstPeriod = configSettings->startStopPeriods;

    uint32_t startSeconds = SECONDS_IN_MINUTE * firstPeriod->startMinutes;

    uint32_t stopSeconds = SECONDS_IN_MINUTE * firstPeriod->stopMinutes;

    uint32_t durationOfStartStopPeriod = stopSeconds - startSeconds;

    *timeOfNextRecording = currentTime + (SECONDS_IN_DAY - currentSeconds) + startSeconds;

    if (configSettings->numberOfSleepRecordCycles == SLEEP_RECORD_CYCLES_DISABLED) {

        *durationOfNextRecording = durationOfStartStopPeriod;

    } else {

        *durationOfNextRecording = MIN(configSettings->recordDuration[sleepRecordCycleIndexOfNextRecording], durationOfStartStopPeriod);

    }

done:

    /* Check if recording should be limited by last recording time */

    if (configSettings->latestRecordingTime > 0) {

        if (*timeOfNextRecording >= configSettings->latestRecordingTime) {

            *timeOfNextRecording = UINT32_MAX;

            *durationOfNextRecording = 0;

        } else {

            int64_t excessTime = (int64_t)*timeOfNextRecording + (int64_t)*durationOfNextRecording - (int64_t)configSettings->latestRecordingTime;

            if (excessTime > 0) {

                *durationOfNextRecording -= excessTime;

            }

        }

    }

}

/* Flash LED according to battery life */

static void flashLedToIndicateBatteryLife() {

    uint32_t numberOfFlashes = LOW_BATTERY_LED_FLASHES;

    uint32_t supplyVoltage = AudioMoth_getSupplyVoltage();

    AM_extendedBatteryState_t batteryState = AudioMoth_getExtendedBatteryState(supplyVoltage);

    /* Set number of flashes according to battery state */

    if (batteryState > AM_EXT_BAT_4V3) {

        numberOfFlashes = 1;

    } else if (batteryState > AM_EXT_BAT_3V5) {

        numberOfFlashes = AM_EXT_BAT_4V4 - batteryState;

    }

    /* Flash LED */

    for (uint32_t i = 0; i < numberOfFlashes; i += 1) {

        FLASH_LED(Red, SHORT_LED_FLASH_DURATION)

        if (numberOfFlashes == LOW_BATTERY_LED_FLASHES) {

            AudioMoth_delay(SHORT_LED_FLASH_DURATION);

        } else {

            AudioMoth_delay(LONG_LED_FLASH_DURATION);

        }

    }

}
