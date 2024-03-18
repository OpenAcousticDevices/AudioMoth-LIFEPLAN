/****************************************************************************
 * configParser.c
 * June 2019
 *****************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "configParser.h"

#define MAX_BUFFER_LENGTH 32

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/* Structure to maintain state */

typedef struct {
    uint8_t state;
    uint8_t index;
    uint8_t count;
    uint8_t returnState;
    char buffer[MAX_BUFFER_LENGTH];
    CP_parserStatus_t status;
} CP_parserState_t;

/* Macro definitions for updating state */

#define INC_STATE state->state += 1;
#define ZERO_STATE state->state = 0;
#define SET_STATE(X) state->state = X;

#define INDEX state->index
#define ZERO_INDEX state->index = 0;
#define INC_INDEX state->index += 1;
#define SET_INDEX(X) state->index = X;

#define COUNT state->count
#define INC_COUNT state->count += 1;
#define ZERO_COUNT state->count = 0;

#define RETURN state->returnState
#define SET_RETURN(X) state->returnState = X;

#define BUFFER state->buffer
#define ADD_TO_BUFFER if (state->count < MAX_BUFFER_LENGTH - 1) {state->buffer[state->count] = c; state->count += 1;}
#define CLEAR_BUFFER memset(state->buffer, 0, MAX_BUFFER_LENGTH); state->count = 0;

#define CLEAR_STATE memset(state, 0, sizeof(CP_parserState_t));

#define SET_STATUS_SUCCESS state->status = CP_SUCCESS;

/* Private macro definition for defining jump table functions */

#define _FUNCTION_START(NAME, NUMBER) \
void NAME ## NUMBER (char c, CP_parserState_t *state, CP_configSettings_t *configSettings) {if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c > 127) {} else

#define _FUNCTION_END(STATE, STATUS) \
{state->state = STATE; state->status = STATUS;} }

/* Macro definitions for defining jump table functions */

#define DEFINE_FUNCTION_INIT(NAME, NUMBER, CONDITION) \
_FUNCTION_START(NAME, NUMBER) if (CONDITION) {CLEAR_STATE state->state = 1; state->status = CP_PARSING;} else _FUNCTION_END(0, CP_WAITING)

#define DEFINE_FUNCTION_STEP(NAME, NUMBER, CONDITION, ACTION) \
_FUNCTION_START(NAME, NUMBER) if (CONDITION) {ACTION;} else _FUNCTION_END(0, CP_CHARACTER_ERROR)

#define DEFINE_FUNCTION_ELSE(NAME, NUMBER, CONDITION1, ACTION1, CONDITION2, ACTION2) \
_FUNCTION_START(NAME, NUMBER) if (CONDITION1) {ACTION1;} else if (CONDITION2) {ACTION2;} else _FUNCTION_END(0, CP_CHARACTER_ERROR)

#define DEFINE_FUNCTION_CND3(NAME, NUMBER, CONDITION1, ACTION1, CONDITION2, ACTION2, CONDITION3, ACTION3) \
_FUNCTION_START(NAME, NUMBER) if (CONDITION1) {ACTION1;} else if (CONDITION2) {ACTION2;} else if (CONDITION3) {ACTION3;} else _FUNCTION_END(0, CP_CHARACTER_ERROR)

#define DEFINE_FUNCTION_CND4(NAME, NUMBER, CONDITION1, ACTION1, CONDITION2, ACTION2, CONDITION3, ACTION3, CONDITION4, ACTION4) \
_FUNCTION_START(NAME, NUMBER) if (CONDITION1) {ACTION1;} else if (CONDITION2) {ACTION2;} else if (CONDITION3) {ACTION3;} else if (CONDITION4) {ACTION4;} else _FUNCTION_END(0, CP_CHARACTER_ERROR)

#define DEFINE_FUNCTION_STRG(NAME, NUMBER, STRING, ACTION) \
_FUNCTION_START(NAME, NUMBER) {char* pattern = STRING; uint32_t length = strlen(pattern); if (c == pattern[COUNT]) {INC_COUNT; if (COUNT == length) {ACTION;}} else _FUNCTION_END(0, CP_CHARACTER_ERROR)}

/* Macro definitions for error cases */

#define VALUE_ERROR \
state->state = 0; state->status = CP_VALUE_ERROR;

/* Macro definitions for various character combinations */

#define IS(X) (c == X)

#define VALUE (c - '0')

#define ISDIGIT ('0' <= c && c <= '9')

#define ISNUMBER (ISDIGIT || IS('-'))

/* Custom macros to handle configuration settings */

#define CHECK_BUFFER_MIN_MAX_AND_SET(destination, min, max, success) { \
    int value = atoi(BUFFER); \
    if (value < min || value > max) { \
        VALUE_ERROR; \
    } else { \
        destination = value; \
        success; \
    } \
}

/* Custom definitions to handle configuration settings */

static uint32_t lowerFrequency, higherFrequency;

static inline bool handleSampleRate(char *buffer, uint32_t *sampleRate, uint8_t *sampleRateDivider) {

    uint32_t value = atoi(buffer);

    if (value != 8000 && value != 16000 && value != 24000 && value != 32000 && value != 48000 && value != 96000 && value != 192000 && value != 250000 && value != 384000) return false;
    
    *sampleRate = value == 250000 ? value : 384000;

    *sampleRateDivider = *sampleRate / value;

    return true;

}

static inline bool handleEnableEnergySaverMode(uint8_t *energySaverMode, uint32_t *sampleRate, uint8_t *sampleRateDivider, uint8_t *clockDivider) {

    if (*energySaverMode == false) return true;

    if (*sampleRateDivider >= 8) {

        *sampleRateDivider /= 2;

        *clockDivider /= 2;

        *sampleRate /= 2;

        return true;

    }
    
    return false;

}


static inline bool handleFilter(uint32_t sampleRate, uint16_t *lowerFilterFreq, uint16_t *higherFilterFreq) {

    if (lowerFrequency > sampleRate / 2) return false;

    if (higherFrequency > sampleRate / 2)  return false;

    if (lowerFrequency >= higherFrequency) return false;

    if (lowerFrequency == 0 && higherFrequency == sampleRate / 2) return true;

    *lowerFilterFreq = lowerFrequency == 0 ? UINT16_MAX : lowerFrequency / 100;

    *higherFilterFreq = higherFrequency == sampleRate / 2 ? UINT16_MAX : higherFrequency / 100;

    return true;

}

static inline bool checkStartStopPeriods(CP_startStopPeriod_t* startStopPeriods, uint32_t activeStartStopPeriods) {

    if (activeStartStopPeriods == 0) return false;

    if (startStopPeriods[0].stopMinutes <= startStopPeriods[0].startMinutes) return false;

    for (uint32_t i = 1; i < activeStartStopPeriods; i += 1) {

        if (startStopPeriods[i].stopMinutes <= startStopPeriods[i].startMinutes) return false;

        if (startStopPeriods[i].startMinutes <= startStopPeriods[i - 1].stopMinutes) return false;

    }

    return true;

}

/* Define jump table functions for configuration settings */

DEFINE_FUNCTION_INIT(CP, 00, IS('{'))
DEFINE_FUNCTION_STRG(CP, 01, "enableLED:", INC_STATE)
DEFINE_FUNCTION_STEP(CP, 02, IS('0') || IS('1'), configSettings->enableLED = VALUE; INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 03, ",enableBatteryLevelDisplay:", INC_STATE)
DEFINE_FUNCTION_STEP(CP, 04, IS('0') || IS('1'), configSettings->enableBatteryLevelDisplay = VALUE; INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 05, ",enableProprietaryFileFormat:", INC_STATE)
DEFINE_FUNCTION_STEP(CP, 06, IS('0') || IS('1'), configSettings->enableProprietaryFileFormat = VALUE; INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STEP(CP, 07, IS(','), INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 08, IS('i'), INC_STATE; CLEAR_BUFFER, IS('s'), SET_STATE(19); CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 09, "nitialSleepRecordCycle", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 10, IS('s'), INC_STATE; CLEAR_BUFFER, IS(':'), SET_STATE(14); CLEAR_BUFFER)
DEFINE_FUNCTION_STEP(CP, 11, IS(':'), INC_STATE)
DEFINE_FUNCTION_ELSE(CP, 12, ISNUMBER, ADD_TO_BUFFER, IS(','), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->initialSleepRecordCycles, 0, 255, INC_STATE; CLEAR_BUFFER))
DEFINE_FUNCTION_STRG(CP, 13, "initialSleepRecordCycle:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 14, "{sleepDuration:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 15, ISNUMBER, ADD_TO_BUFFER, IS(','), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->sleepDuration[0], 5, 43200, INC_STATE; CLEAR_BUFFER))
DEFINE_FUNCTION_STRG(CP, 16, "recordDuration:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 17, ISNUMBER, ADD_TO_BUFFER, IS('}'), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->recordDuration[0], 1, 43200, configSettings->numberOfSleepRecordCycles += 1; INC_STATE; CLEAR_BUFFER))
DEFINE_FUNCTION_STRG(CP, 18, ",sl", SET_STATE(20); CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 19, IS('l'), INC_STATE; CLEAR_BUFFER, IS('t'), SET_STATE(25); CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 20, "eepRecordCycle:{sleepDuration:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 21, ISNUMBER, ADD_TO_BUFFER, IS(','), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->sleepDuration[1], 5, 43200, INC_STATE; CLEAR_BUFFER))
DEFINE_FUNCTION_STRG(CP, 22, "recordDuration:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 23, ISNUMBER, ADD_TO_BUFFER, IS('}'), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->recordDuration[1], 1, 43200, configSettings->numberOfSleepRecordCycles += 1; INC_STATE; CLEAR_BUFFER))
DEFINE_FUNCTION_STRG(CP, 24, ",st", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 25, "andardSettings:", SET_INDEX(0); SET_RETURN(26); SET_STATE(39); CLEAR_BUFFER)
DEFINE_FUNCTION_STEP(CP, 26, IS(','), INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 27, IS('o'), INC_STATE; CLEAR_BUFFER, IS('r'), SET_STATE(31); CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 28, "pportunisticSettings:", configSettings->enableOpportunisticRecording = 1; SET_INDEX(1); SET_RETURN(29); SET_STATE(39); CLEAR_BUFFER)
DEFINE_FUNCTION_STEP(CP, 29, IS(','), INC_STATE)
DEFINE_FUNCTION_STEP(CP, 30, IS('r'), INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 31, "ecordingPeriods:[", ZERO_INDEX; INC_STATE)
DEFINE_FUNCTION_STEP(CP, 32, IS('{'), INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 33, "startMinutes:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 34, ISDIGIT, ADD_TO_BUFFER, IS(','), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->startStopPeriods[INDEX].startMinutes, 0, 1440, INC_STATE; CLEAR_BUFFER))
DEFINE_FUNCTION_STRG(CP, 35, "stopMinutes:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 36, ISDIGIT, ADD_TO_BUFFER, IS('}'), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->startStopPeriods[INDEX].stopMinutes, 0, 1440, INC_STATE))
DEFINE_FUNCTION_ELSE(CP, 37, IS(',') && INDEX < (MAXIMUM_NUMBER_OF_START_STOP_PERIODS - 1), INC_INDEX; SET_STATE(32), IS(']'), configSettings->activeStartStopPeriods = INDEX + 1; if (checkStartStopPeriods(configSettings->startStopPeriods, configSettings->activeStartStopPeriods)) {INC_STATE} else {VALUE_ERROR})
DEFINE_FUNCTION_STEP(CP, 38, IS('}'), SET_STATUS_SUCCESS)

DEFINE_FUNCTION_STRG(CP, 39, "{gain:", INC_STATE)
DEFINE_FUNCTION_STEP(CP, 40, IS('0') || IS('1') || IS('2') || IS('3') || IS('4'), configSettings->gain[INDEX] = VALUE; INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 41, ",sampleRate:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 42, ISNUMBER, ADD_TO_BUFFER, IS(',') || (INDEX == 0 && IS('}')), bool success = handleSampleRate(BUFFER, &configSettings->sampleRate[INDEX], &configSettings->sampleRateDivider[INDEX]); if (!success) {VALUE_ERROR} else if (IS(',')) {INC_STATE} else {SET_STATE(RETURN)})
DEFINE_FUNCTION_CND4(CP, 43, IS('e'), INC_STATE; CLEAR_BUFFER, IS('f'), SET_STATE(48); CLEAR_BUFFER, IS('a'), SET_STATE(54); CLEAR_BUFFER, INDEX == 1 && IS('m'), SET_STATE(57); CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 44, "nableEnergySaverMode:", INC_STATE)
DEFINE_FUNCTION_STEP(CP, 45, IS('0') || IS('1'), configSettings->enableEnergySaverMode[INDEX] = VALUE; bool success = handleEnableEnergySaverMode(&configSettings->enableEnergySaverMode[INDEX], &configSettings->sampleRate[INDEX], &configSettings->sampleRateDivider[INDEX], &configSettings->clockDivider[INDEX]); if (!success) {VALUE_ERROR} else {INC_STATE})
DEFINE_FUNCTION_ELSE(CP, 46, IS(','), INC_STATE, INDEX == 0 && IS('}'), SET_STATE(RETURN))
DEFINE_FUNCTION_CND3(CP, 47, IS('f'), INC_STATE; CLEAR_BUFFER, IS('a'), SET_STATE(54); CLEAR_BUFFER, INDEX == 1 && IS('m'), SET_STATE(57); CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 48, "ilter:{lowerFrequency:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 49, ISNUMBER, ADD_TO_BUFFER, IS(','), lowerFrequency = atoi(BUFFER); INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 50, "higherFrequency:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 51, ISNUMBER, ADD_TO_BUFFER, IS('}'), higherFrequency = atoi(BUFFER); bool success = handleFilter(configSettings->sampleRate[INDEX] / configSettings->sampleRateDivider[INDEX], &configSettings->lowerFilterFreq[INDEX], &configSettings->higherFilterFreq[INDEX]); if (!success) {VALUE_ERROR} else {INC_STATE})
DEFINE_FUNCTION_ELSE(CP, 52, IS(','), INC_STATE, INDEX == 0 && IS('}'), SET_STATE(RETURN))
DEFINE_FUNCTION_ELSE(CP, 53, IS('a'), INC_STATE; CLEAR_BUFFER, INDEX == 1 && IS('m'), SET_STATE(57); CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 54, "mplitudeThreshold:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 55, ISNUMBER, ADD_TO_BUFFER, (INDEX == 0 && IS('}')) || (INDEX == 1 && IS(',')), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->amplitudeThreshold[INDEX], 0, 32768, if (INDEX == 1) {INC_STATE} else {SET_STATE(RETURN)}))
DEFINE_FUNCTION_STEP(CP, 56, INDEX == 1 && IS('m'), INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 57, "aximum", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 58, IS('D'), INC_STATE, IS('T'), SET_STATE(62))
DEFINE_FUNCTION_STRG(CP, 59, "uration:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 60, ISNUMBER, ADD_TO_BUFFER, IS(','), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->maximumOpportunisticDuration, 1, 43200, INC_STATE; CLEAR_BUFFER))
DEFINE_FUNCTION_STRG(CP, 61, "maximumT", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_STRG(CP, 62, "otalFileSize:", INC_STATE; CLEAR_BUFFER)
DEFINE_FUNCTION_ELSE(CP, 63, ISNUMBER, ADD_TO_BUFFER, IS('}'), CHECK_BUFFER_MIN_MAX_AND_SET(configSettings->maximumTotalOpportunisticFileSize, 0, 32768, SET_STATE(RETURN)))

static void (*CPfunctions[])(char, CP_parserState_t*, CP_configSettings_t*) = {CP00, CP01, CP02, CP03, CP04, CP05, CP06, CP07, \
                                                                               CP08, CP09, CP10, CP11, CP12, CP13, CP14, CP15, \
                                                                               CP16, CP17, CP18, CP19, CP20, CP21, CP22, CP23, \
                                                                               CP24, CP25, CP26, CP27, CP28, CP29, CP30, CP31, \
                                                                               CP32, CP33, CP34, CP35, CP36, CP37, CP38, CP39, \
                                                                               CP40, CP41, CP42, CP43, CP44, CP45, CP46, CP47, \
                                                                               CP48, CP49, CP50, CP51, CP52, CP53, CP54, CP55, \
                                                                               CP56, CP57, CP58, CP59, CP60, CP61, CP62, CP63 };

/* Define parser */

static CP_parserState_t parserState;

void ConfigParser_reset() {

    memset(&parserState, 0, sizeof(CP_parserState_t));

}

CP_parserStatus_t ConfigParser_parse(char c, CP_configSettings_t *configSettings) {

    CPfunctions[parserState.state](c, &parserState, configSettings);

    return parserState.status;

}
