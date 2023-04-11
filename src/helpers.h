#ifndef helpers_h
#define helpers_h

#include "Arduino.h"
#include <TimeLib.h>
#include <Strings.h>
#include <IPAddress.h>

String printIP(IPAddress adress);
void parseBytes(const char *str, char sep, byte *bytes, int maxBytes, int base);
String generateUid(int type = 0, int length = 12);

#endif