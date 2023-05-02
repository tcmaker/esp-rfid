#include "accesscontrol.h"

#define DEBUG_SERIAL if(DEBUG)Serial

TCMWiegandClass TCMWiegand;
AccessControlClass AccessControl;

bool TCMWiegandClass::idle = true;
unsigned long TCMWiegandClass::lastEdge_u = 0;
unsigned long TCMWiegandClass::lastLoop_m = 0;
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
	unsigned long nowMillis = millis();
	if (idle || nowMicros - lastEdge_u > WIEGAND_MIN_TIME) {
		if (reader != NULL) {
			reader->ISR_Data0();
		}
		lastEdge_u =  nowMicros;
		lastLoop_m = nowMillis;
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
	unsigned long nowMillis = millis();
	if (idle || nowMicros - lastEdge_u > WIEGAND_MIN_TIME) {
		if (reader != NULL) {
			if (reader->bitCount >= MAX_READ_BITS) {
				DEBUG_SERIAL.println("[ WARN ] Read max bit count exceeded.");
				reader->bitCount = MAX_READ_BITS - 1;
			}
			reader->ISR_Data1();
		}
		lastEdge_u = nowMicros;
		lastLoop_m = nowMillis;
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
	ControlState nextState = wait_read;
	switch (state) {
	case ControlState::lookup_local:
		if (uid.isEmpty()) {
			DEBUG_SERIAL.println(F("[ WARN ] state(lookup_local): uid was empty"));
			state = ControlState::wait_read;
			return;
		} 

		nextState = lookupUID_local();
		if (nextState == lookup_remote) {
			lastMilli = millis();

			// kick off remote lookup, if available
			if (this->remoteLookup) {
				this->remoteLookup(uid);
			} else {
				nextState = ControlState::cool_down;
				this->accessDenied(AccessResult::unrecognized,
						   		   String("local DB only"),
						           this->uid,
						   		   String("N/A"));
			}
		}
		state = nextState;
		break;
	case ControlState::lookup_remote:
		// jsonRecord would get populated via MQTT, which can then be processed
		if (!jsonRecord.isNull()) {
			state = ControlState::process_record;
		} else if (millis() - lastMilli > LOOKUP_DELAY) {
			this->accessDenied(AccessResult::unrecognized,
								String("remote DB timeout"),
								this->uid,
								String("N/A"));
			state = ControlState::cool_down;
		}
		break;
	case ControlState::process_record:
		lastMilli = millis();
		nextState = this->checkUserRecord();
		state = nextState;
		break;
	case ControlState::cool_down:
		if (millis() - lastMilli > 1500) {
			state = ControlState::wait_read;
		}
		break;
	case ControlState::wait_read:
	default:
		break;
	}
}

ControlState AccessControlClass::lookupUID_local() {
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
			DEBUG_SERIAL.println(F("[ WARN ] Failed to parse User Data"));
			return ControlState::lookup_remote;
		}

		// Original code has pincode support--may want to re-add that here...
		// if (config.pinCodeRequested) {
		// 	if(this->setupReadPinCode) {
		// 		this->setupReadPinCode();
		// 		return ControlState::check_pin;
		// 	}
		// }

		// record found and deserialized
		return ControlState::process_record;
	} else {
		// No file found, then try remote lookup.
		return ControlState::lookup_remote;
	}
}

ControlState AccessControlClass::checkUserRecord() {
	if (jsonRecord.containsKey("is_banned") && (jsonRecord["is_banned"].as<int>() > 0)) {
		this->accessDenied(AccessResult::banned,
						   jsonRecord["is_banned"].as<String>(),
						   this->uid,
						   jsonRecord["person"] | String("N/A"));
		return ControlState::cool_down;
	} else if (jsonRecord["validuntil"] < now()) { // missing value => 0 => expired
		this->accessDenied(AccessResult::expired,
		                   jsonRecord["validuntil"] | String("validuntil is unset"),
						   this->uid,
						   jsonRecord["person"] | String("N/A"));
		return ControlState::cool_down;
	} else if (jsonRecord["validsince"] > now()) { // missing value => 0 => granted
		this->accessDenied(AccessResult::not_yet_valid,
		                   jsonRecord["validsince"],
						   this->uid,
						   jsonRecord["person"] | String("N/A"));
		return ControlState::cool_down;
	} else {
		String validsince = "validsince is unset";
		if (jsonRecord.containsKey("validsince")) {
			validsince = jsonRecord["validsince"].as<String>();
		}
		this->accessGranted(AccessResult::granted,
		                   validsince,
						   this->uid,
						   jsonRecord["person"] | String("N/A"));
		return ControlState::wait_read;
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
	DEBUG_SERIAL.print(F("Fob read: "));
	DEBUG_SERIAL.print(reader->facilityCode);
	DEBUG_SERIAL.print(F(":"));
	DEBUG_SERIAL.print(reader->cardCode);
	DEBUG_SERIAL.print(F(" ("));
	DEBUG_SERIAL.print(reader->bitCount);
	DEBUG_SERIAL.println(F(")"));
	if (AccessControl.state == ControlState::wait_read) {
		if (reader->facilityCode == 0) {
			DEBUG_SERIAL.println(F("[ INFO ] Bad read: facility code was zero"));
			return;
		}
		unsigned long code = reader->facilityCode << 16 | reader->cardCode;
		
		String uid = String(code, HEX);
		// String type = String(reader->bitCount, DEC);

		DEBUG_SERIAL.print(F("[ INFO ] PICC's UID: "));
		DEBUG_SERIAL.println(uid);

		// Setup AccessControl to handle authorization
		// this function is called from loop(), so state is only changed in main thread
		AccessControl.uid = uid;
		AccessControl.state = ControlState::lookup_local;
	} else {
		// any other state results in the read being thrown away
		DEBUG_SERIAL.println(F("[ WARN ] AccessControl received read while still handling last read"));
	return;
	}
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