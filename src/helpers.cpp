#include "helpers.h"

boot_info_t bootInfo;

// String ICACHE_FLASH_ATTR printIP(IPAddress adress)
// {
// 	return (String)adress[0] + "." + (String)adress[1] + "." + (String)adress[2] + "." + (String)adress[3];
// }

void ICACHE_FLASH_ATTR parseBytes(const char *str, char sep, byte *bytes, int maxBytes, int base)
{
	for (int i = 0; i < maxBytes; i++)
	{
		bytes[i] = strtoul(str, NULL, base); // Convert byte
		str = strchr(str, sep);				 // Find next separator
		if (str == NULL || *str == '\0')
		{
			break; // No more separators, exit
		}
		str++; // Point to next character after separator
	}
}

// String ICACHE_FLASH_ATTR generateUid(int type, int length)
// {

// 	// nardev: this could be implemented in config, to choose default type of UID;

// 	String uid;
// 	if (type)
// 	{
// 		uid = now();
// 	}
// 	else
// 	{
// 		// char *characters = "abcdefghijklmnopqrstuvwxyz0123456789"; small letters, ordered
// 		// char *characters = "ABSDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; uppercase, ordered
// 		const char *characters = "SN07YXRGP9DBOUVLJK6IEH1FWMT8Q4SA3Z52"; // randomized, uppercase

// 		for (int i = 0; i < length; i++)
// 		{
// 			uid = uid + characters[random(0, 36)];
// 		}
// 	}

// 	return uid;
// }
