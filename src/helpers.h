#ifndef helpers_h
#define helpers_h

#define VERSION "2.0.0-dev"

#include "Arduino.h"
#include <FS.h>
// #include <TimeLib.h>
// #include <Strings.h>
// #include <IPAddress.h>

// String printIP(IPAddress adress);
void parseBytes(const char *str, char sep, byte *bytes, int maxBytes, int base);
// String generateUid(int type = 0, int length = 12);

/**
 * @brief holds boot_info for boot message
 * 
 */
struct boot_info_t
{
	const char *version = VERSION;
	const bool debug = DEBUG;
    uint32_t chipid;
	bool formatted;
	bool configured;
    FSInfo fsinfo;
};

#endif