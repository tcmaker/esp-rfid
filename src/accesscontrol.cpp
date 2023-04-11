#include "accesscontrol.h"

TCMWiegandClass TCMWiegand;
AccessControlClass AccessControl;

bool TCMWiegandClass::idle;
unsigned long TCMWiegandClass::lastEdge_u;
unsigned long TCMWiegandClass::lastLoop_m;
ProxReaderInfo* TCMWiegandClass::reader;

/**
 * @brief Handles falling-edge interrupts on the D0 pin and filters out
 * false interrupts based on WIEGAND_MIN_TIME in microseconds.
 * 
 * False edges are detected when the Weigand signal passes through 
 * an optocoupler with slow edges.
 * 
 * Idle detection is handled to avoid missing the first bit due to
 * overflow of the micros() counter.
 * 
 * idle is set by the loop() function.
 */
ICACHE_RAM_ATTR void TCMWiegandClass::handleD0() {
	unsigned long nowMicros = micros();
	if (idle || nowMicros - lastEdge_u > WIEGAND_MIN_TIME) {
		if (reader != NULL) {
			reader->ISR_Data0();
		}
		lastEdge_u =  nowMicros;
		idle = false;
	}
}

/**
 * @brief Handles falling-edge interrupts on the D0 pin and filters out
 * false interrupts based on WIEGAND_MIN_TIME in microseconds. See handleD0()
 * for more information.
 */
ICACHE_RAM_ATTR void TCMWiegandClass::handleD1() {
	unsigned long nowMicros = micros();
	if (idle || nowMicros - lastEdge_u > WIEGAND_MIN_TIME) {
		if (reader != NULL) {
			reader->ISR_Data1();
		}
		lastEdge_u = nowMicros;
		idle = false;
	}
}


void TCMWiegandClass::begin(int pinD0, int pinD1) {
	lastEdge_u = micros();
	lastLoop_m = millis();
	// coolDownTimer = 0;
	reader = HidProxWiegand.addReader(pinD0, pinD1, readHandler);
	HidProxWiegand_AttachReaderInterrupts(pinD0, pinD1, this->handleD0, this->handleD1);
}

void TCMWiegandClass::loop() {
	/* lastEdge_u is reset in the D0/D1 interrupt handlers -- if we sit
		idle for a long time, then we might overflow and miss the first bit
		of a read.
	*/
	if ((micros() - lastEdge_u > WIEGAND_WAIT_TIME) || (millis() - lastLoop_m > 60000)) {
		idle = true;
	}
	HidProxWiegand.loop();
}

// #define WIEGAND_ENT 0xD
// #define WIEGAND_ESC 0x1B

// StaticJsonDocument<512> AccessControlClass::jsonRecord;

AccessControlClass::AccessControlClass() {
	coolDownStart = 0;
	lastMilli = millis();
	state = wait_read;
}

void AccessControlClass::loop()  {
	switch (state)
	{
	case ControlState::lookup_local:
		if (uid.isEmpty()) {
			#ifdef DEBUG
				Serial.println("");
				Serial.println(F("[ WARN ] state(lookup_local): uid was empty"));
			#endif
			state = ControlState::wait_read;
			return;
		} else if (lookupUID_local()) {
			state = ControlState::process_record;
		} else {
			state = ControlState::lookup_remote;
			lastMilli = millis();
			this->remoteLookup(uid);
		}
		break;
	case ControlState::lookup_remote:
		if (!jsonRecord.isNull()) {
			state = ControlState::process_record;
		} else if (millis() - lastMilli > LOOKUP_DELAY) {
			this->accessDenied("unrecognized");
			state = ControlState::cool_down;
		}
		break;
	case ControlState::process_record:
		this->checkUserRecord();
		// state is updated by this function
		break;
	case ControlState::grant_access:

		break;
	case ControlState::wait_read:
	default:
		break;
	}
}

bool AccessControlClass::lookupUID_local() {
	File f = SPIFFS.open("/P/" + uid, "r");

	if (f) {		// user exists
		size_t size = f.size();
		std::unique_ptr<char[]> buf(new char[size]);
		f.readBytes(buf.get(), size);
		f.close();
		auto error = deserializeJson(jsonRecord, buf.get());
		if (error) {
			state = ControlState::wait_read;
			jsonRecord.clear();
#ifdef DEBUG
			Serial.println("");
			Serial.println(F("[ WARN ] Failed to parse User Data"));
#endif
			return false;
		}

// 		if (config.pinCodeRequested) {
// 		}

		return true;
	} else {
		return false;
	}
}

void AccessControlClass::checkUserRecord() {
	if (jsonRecord.containsKey("is_banned") && (jsonRecord["is_banned"] > 0)) {
		state = ControlState::cool_down;
		this->accessDenied("banned");
	} else if (jsonRecord["validuntil"] < now()) { // missing value -> expired
		state = ControlState::cool_down;
		this->accessDenied("expired");
	} else if (jsonRecord["validsince"] > now()) {
		state = ControlState::cool_down;
		this->accessDenied("not_yet_valid");
	} else {
		state = ControlState::wait_read;
		this->accessGranted("granted");
	}
}

/**
 * @brief Call-back used by HidProxWiegand in its loop().
 * This is used to update the state of AccessControl to handle
 * lookups and responses.
 * 
 * @param reader 
 */
void readHandler(ProxReaderInfo* reader) {
	Serial.print(F("Fob read: "));
	Serial.print(reader->facilityCode);
	Serial.print(F(":"));
	Serial.print(reader->cardCode);
	Serial.print(F(" ("));
	Serial.print(reader->bitCount);
	Serial.println(F(")"));
	if (AccessControl.state == ControlState::wait_read) {
		if (reader->facilityCode == 0) {
			#ifdef DEBUG
				Serial.println(F("[ INFO ] Bad read: facility code was zero"));
			#endif
			return;
		}
		unsigned long code = reader->facilityCode << 16 | reader->cardCode;
		
		String uid = String(code, DEC);
		// String type = String(reader->bitCount, DEC);

		#ifdef DEBUG
			Serial.print(F("[ INFO ] PICC's UID: "));
			Serial.println(uid);
		#endif
		// Setup AccessControl to handle authorization
		AccessControl.uid = uid;
		AccessControl.state = ControlState::lookup_local;
	} else {
		#ifdef DEBUG
			Serial.println(F("[ WARN ] AccessControl received read while still handling last read"));
		#endif
	return;
	}
}

void retrieveRecord(String uid) {
	return;
}

int weekdayFromMonday(int weekdayFromSunday) {
	// we expect weeks starting from Sunday equals to 1
	// we return week day starting from Monday equals to 0
	return ( weekdayFromSunday + 5 ) % 7;
}

#if 0
void deny_access(String reason) {
	if (processingState == valid)
	{
		ws.textAll("{\"command\":\"giveAccess\"}");
#ifdef DEBUG
		Serial.printf(" has access relay");
#endif
		mqttPublishAccess(now(), "true", "Always", username, uid);
		beeperValidAccess();
	}
	if (processingState == validAdmin)
	{
		ws.textAll("{\"command\":\"giveAccess\"}");
#ifdef DEBUG
		Serial.println(" has admin access, enable wifi");
#endif
		mqttPublishAccess(now(), "true", "Admin", username, uid);
		beeperAdminAccess();
	}
	if (processingState == expired)
	{
#ifdef DEBUG
		Serial.println(" expired");
#endif
		mqttPublishAccess(now(), "true", "Expired", username, uid);
		// ledAccessDeniedOn();
		// beeperAccessDenied();
	}
	if (processingState == wrongPincode)
	{
		mqttPublishAccess(now(), "true", "Wrong pin code", username, uid);
		ledAccessDeniedOn();
		beeperAccessDenied();
	}
	if (processingState == notValid)
	{
#ifdef DEBUG
		Serial.println(" does not have access");
#endif
		mqttPublishAccess(now(), "true", "Disabled", username, uid);
		ledAccessDeniedOn();
		beeperAccessDenied();
	}
	if (processingState == unknown)
	{
		String data = String(uid) += " " + String(type);
		writeEvent("WARN", "rfid", "Unknown rfid tag is scanned", data);
		writeLatest(uid, "Unknown", 98);
		DynamicJsonDocument root(512);
		root["command"] = "piccscan";
		root["uid"] = uid;
		root["type"] = type;
		root["known"] = 0;
		size_t len = measureJson(root);
		AsyncWebSocketMessageBuffer *buffer = ws.makeBuffer(len);
		if (buffer)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			ws.textAll(buffer);
		}
		mqttPublishAccess(now(), "false", "Denied", "Unknown", uid);
		ledAccessDeniedOn();
		beeperAccessDenied();
	}
	if (uid != "" && processingState != waitingProcessing)
	{
		writeLatest(uid, username, accountType);
		DynamicJsonDocument root(512);
		root["command"] = "piccscan";
		root["uid"] = uid;
		root["type"] = type;
		root["known"] = 1;
		root["acctype"] = accountType;
		root["user"] = username;
		size_t len = measureJson(root);
		AsyncWebSocketMessageBuffer *buffer = ws.makeBuffer(len);
		if (buffer)
		{
			serializeJson(root, (char *)buffer->get(), len + 1);
			ws.textAll(buffer);
		}
	}
}

#endif