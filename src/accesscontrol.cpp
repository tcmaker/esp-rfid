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
			// ISR_Data1 does not do bounds checking
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


/**
 * @brief Call-back used by HidProxWiegand in its loop().
 * This is used to update the state of AccessControl to handle
 * lookups and responses.
 * 
 * @param reader 
 */
void readHandler(ProxReaderInfo* reader) {
	DEBUG_SERIAL.printf("[ INFO ] %lu - ", micros());
	DEBUG_SERIAL.print(F("Fob read: "));
	DEBUG_SERIAL.print(reader->facilityCode);
	DEBUG_SERIAL.print(F(":"));
	DEBUG_SERIAL.print(reader->cardCode);
	DEBUG_SERIAL.print(F(" ("));
	DEBUG_SERIAL.print(reader->bitCount);
	DEBUG_SERIAL.println(F(")"));

	// AccessControl state machine must be in wait_read state to avoid race conditions
	if (AccessControl.state == ControlState::wait_read) {
		if (reader->facilityCode == 0) {
			DEBUG_SERIAL.println(F("[ INFO ] Bad read: facility code was zero"));
			return;
		}
		unsigned long code = reader->facilityCode << 16 | reader->cardCode;
		
		String uid = String(code, DEC);
		// String type = String(reader->bitCount, DEC);

		DEBUG_SERIAL.print(F("[ INFO ] UID: "));
		DEBUG_SERIAL.println(uid);

		// Setup AccessControl to handle authorization.
		// The actual authorization is done in AccessControlClass::loop()
		// via the main loop() thread.
		AccessControl.uid = uid;
		AccessControl.state = ControlState::lookup_local;
	} else {
		// any other state results in the read being thrown away
		DEBUG_SERIAL.println(F("[ WARN ] AccessControl received read while still handling last read"));
	return;
	}
}

void TCMWiegandClass::begin(int pinD0, int pinD1) {
	lastEdge_u = micros();
	lastLoop_m = millis();
	idle = true;
	// coolDownTimer = 0;
	reader = HidProxWiegand.addReader(pinD0, pinD1, readHandler);
	reader->bitCount = 0;
	reader->facilityCode = 0;
	reader->cardCode = 0;
	for (auto i = 0; i < MAX_READ_BITS; i++) {
		reader->databits[i] = 0;
	}

	// memset(reader->databits, 0, MAX_READ_BITS);
	HidProxWiegand_AttachReaderInterrupts(pinD0, pinD1, this->handleD0, this->handleD1);
}

// ProxReaderInfo* TCMWiegandClass::addReader(short pinD0, short pinD1) {
// 	lastEdge_u = micros();
// 	lastLoop_m = millis();

// 	HidProxWiegand_AttachReaderInterrupts(pinD0, pinD1, this->handleD0, this->handleD1);

// 	reader = HidProxWiegand.addReader(pinD0, pinD1, readHandler);
// 	return reader;
// }

void TCMWiegandClass::loop() {
	/* lastEdge_u is reset in the D0/D1 interrupt handlers -- if we sit
		idle for a long time, then we might overflow and miss the first bit
		of a read.
	*/
	if (!idle && ((micros() - lastEdge_u > WIEGAND_MIN_TIME * 10) || (millis() - lastLoop_m > WIEGAND_WAIT_TIME))) {
		idle = true;
		// wiegandCount counts loops
		// HidProxWiegand->getCurrentReader()->wiegandCounter = 1;
	}
	HidProxWiegand.loop();
}

// #define WIEGAND_ENT 0xD
// #define WIEGAND_ESC 0x1B

// StaticJsonDocument<512> AccessControlClass::jsonRecord;

AccessControlClass::AccessControlClass()
: lookupLocal(nullptr)
, lookupRemote(nullptr)
, accessDenied(nullptr)
, accessGranted(nullptr)
{
	coolDownStart = 0;
	lastMilli = millis();
	state = wait_read;
}

/* loop() should be called by the main thread loop
 * This breaks up the process of taking a Wiegand ID, looking up the ID
 * in the local file system and making choice whether to fire the granted
 * or denied call back.
 * If the record does not exist or if the record results in a denial other than
 * banned, then the state machine will delay for a short period (LOOKUP_DELAY) 
 * to see if a MQTT message arrives that changes the decision.
 * The process is broken up across multiple states in case any of these
 * steps (flash access, JSON deserialization, remote lookup, etc.) take a long time and
 * would cause other threads to block.
 * */
void AccessControlClass::loop()  {
	ControlState nextState = wait_read;
	static AccessResult result = AccessResult::unrecognized;
	// Default state is wait_read.
	// A Wiegand scan will result in the `state` being updated to
	// lookup_local by readHandler()

	switch (state) {
	case ControlState::lookup_local:
		if (uid.isEmpty()) {
			DEBUG_SERIAL.println(F("[ WARN ] state(lookup_local): uid was empty"));
			state = ControlState::wait_read;
			return;
		} 
		jsonRecord.clear();

		if (lookupUID_local()) {
			// local record does not exist
			lastMilli = millis();
			result = AccessResult::unrecognized;

			if (this->lookupRemote) {
				// setup remote lookup, if available
				state = ControlState::wait_remote;
				this->lookupRemote(uid);
				// handleResult(result);
			} else {
				// if remote lookup is not setup, then handle the denial now
				state = ControlState::cool_down;
				// handleResult(result);
			}
		} else {
			state = ControlState::process_record_local;
			// do not call handleResult() in this case
			return;
		}
		// state = nextState;
		handleResult(result);
		break;
	case ControlState::wait_remote:
		// remote lookup should set state to process_record_remote
		if (millis() - lastMilli > LOOKUP_DELAY) {
			// if record is received asynchronously at this point then
			// timeout will still occur
			state = ControlState::timeout_remote;
		}
		break;
	case ControlState::timeout_remote:
		handleResult(result);
		state = ControlState::cool_down;
		break;
	case ControlState::process_record_local:
		result = this->checkUserRecord();
		lastMilli = millis();

		if (result != granted && result != banned) {
			// local look up did not result in granted or banned
			if (this->lookupRemote) {
				nextState = ControlState::wait_remote;
				this->lookupRemote(uid);
			} else {
				nextState = ControlState::cool_down;
			}
		} else {
			nextState = ControlState::cool_down;
		}
		state = nextState;
		handleResult(result);
		break;
	case ControlState::process_record_remote:
		// we get here if the remote lookup successfully retrieved a JSON record
		lastMilli = millis();
		// result is static
		result = this->checkUserRecord();
		handleResult(result);
		state = ControlState::cool_down;
		DEBUG_SERIAL.printf("json size before cool down: %u\n", jsonRecord.memoryUsage());
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

int AccessControlClass::lookupUID_local() {
	File f = SPIFFS.open("/P/" + uid, "r");

	if (f) {		// user exists
		// size_t size = f.size();
		// std::unique_ptr<char[]> buf(new char[size]);
		f.readBytes(buf, 384);
		f.close();
		auto error = deserializeJson(jsonRecord, buf);
		DEBUG_SERIAL.printf("json size: %u\n", jsonRecord.memoryUsage());
		if (error) {
			// state = ControlState::wait_read;
			jsonRecord.clear();
			DEBUG_SERIAL.println(F("[ WARN ] Failed to parse User Data"));
			// file error, then try remote lookup
			return 2;
		}

		// Original code has pincode support--may want to re-add that here...
		// if (config.pinCodeRequested) {
		// 	if(this->setupReadPinCode) {
		// 		this->setupReadPinCode();
		// 		return ControlState::check_pin;
		// 	}
		// }

		// record found and deserialized
		return 0;
	} else {
		// No file found, then try remote lookup.
		return 1;
	}
}

/* checkUserRecord() looks at the current jsonRecord and implements
*  the decision logic.
*/
AccessResult AccessControlClass::checkUserRecord() {
	if (jsonRecord.containsKey("is_banned") && (jsonRecord["is_banned"].as<int>() > 0)) {
		return AccessResult::banned;
	} 
	
	if (jsonRecord["validuntil"] < now()) { // missing value => 0 => expired
		return AccessResult::expired;
	} else if (jsonRecord["validsince"] > now()) { // missing value => 0 => granted
		// this would only be used for "future effectivity" -- not sure if useful
		// if NTP has not set time, then fail granted
		if (now() < 1600000000) { // Sep 13 2020
			return AccessResult::granted;
		}
		return AccessResult::not_yet_valid;
	} else {
		return AccessResult::granted;
	}
}

/**
 * @brief Uses the result determined from checkUserRecord() and current state
 * to prepare and call the appropriate callback function.
 * 
 * @param result AccessResult
 */
void AccessControlClass::handleResult(const AccessResult result) {
	String detail("N/A");
	String name;

	name = jsonRecord["username"] | "N/A";

	// looks at result and state to indicate why the result occured.
	switch (result)
	{
	case unrecognized:
		detail = "unrecognized";
		break;
	case banned:
		detail = jsonRecord["is_banned"] | "(zero)";
		break;
	case expired:
		// DEBUG_SERIAL.println(jsonRecord["validuntil"].as<String>());
		detail = "validuntil=";
		if (jsonRecord.containsKey("validuntil")) {
			detail += jsonRecord["validuntil"].as<String>();
		} else {
			detail += "unset";
		}
		break;
	case not_yet_valid:
		/* FALL THROUGH */
	case granted:
		detail = "validsince=";
		if (jsonRecord.containsKey("validsince")) {
			detail += jsonRecord["validsince"].as<String>();
		} else {
			detail += "unset";
		}
		break;
	default:
		detail = "unhandled lookup result";
		break;
	}

	if (state == cool_down) {
		detail += " (local DB)";
	} else if (state == wait_remote) {
		detail += " (waiting remote)";
	} else if (state == timeout_remote) {
		detail += " (remote DB timeout)";
	} else {
		detail += " (remote DB)";
	}

	if (result == granted) {
		this->accessGranted(result,
		                    detail,
						    this->uid,
						    name);
		// return ControlState::wait_read;
	} else {
		this->accessDenied(result,
						   detail,
						   this->uid,
						   name);
		// return ControlState::cool_down;
	}

}



#if 0
int weekdayFromMonday(int weekdayFromSunday) {
	// we expect weeks starting from Sunday equals to 1
	// we return week day starting from Monday equals to 0
	return ( weekdayFromSunday + 5 ) % 7;
}

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