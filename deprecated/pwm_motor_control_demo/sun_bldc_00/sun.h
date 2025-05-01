// sun.h //////////////////////////////////////////////////////////////////////

#pragma once

// Prototypes //////////////////////////////////////////////////////////////////

unsigned long getCurrentEpoch();
bool isSunriseTriggerTime(int secondsSinceMidnight);
bool isSunsetTriggerTime(int secondsSinceMidnight);
bool shouldResetDailyFlags(int secondsSinceMidnight);
bool sunIsUp(int secondsSinceMidnight);
void setupNetworking();
void updateSunData(int secondsSinceMidnight);

// End of file /////////////////////////////////////////////////////////////////
