#ifndef accesscontrol_h
#define accesscontrol_h

#include <TimeLib.h>
#include <strings.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "magicnumbers.h"
#include <HidProxWiegand.h>
#include "config.h"

#define WIEGAND_MIN_TIME 2100   // minimum time (us) between D0/D1 edges 
#define LOOKUP_DELAY 500        // maximum time (ms) to wait for UID lookup response from server

enum AccessResult {
    unrecognized = 1,
    banned,
    expired,
    not_yet_valid,
    granted
};

enum ControlState {
    wait_read,
    lookup_local,
    lookup_remote,
    process_record,
    check_pin,
    // grant_access,
    // deny_access,
    cool_down
};

void readHandler(ProxReaderInfo* reader);
// void retrieveRecord(String uid);

class TCMWiegandClass {
    public:
    // TCMWiegandClass();

    void begin(int pinD0, int pinD1);

    void loop();

    private:
    static ProxReaderInfo* reader;

    static bool idle;
    static unsigned long lastEdge_u;
    static unsigned long lastLoop_m;
    static void handleD0();
    static void handleD1();
};

class AccessControlClass {
    public:
    /**
     * @brief Construct new RFID object
     * 
     */
    AccessControlClass();


    void (*lookupCredential)(JsonDocument* user);
    void (*remoteLookup)(String scanned_id);
    void (*accessDenied)(AccessResult result, String detail, String credential, String name);
    void (*accessGranted)(AccessResult result, String detail, String credential, String name);


    // void begin();

    void loop();

    // void reset();

    ControlState lookupUID_local();
    ControlState checkUserRecord();

    ControlState state;

    String uid;

    struct UserRecord {
        String credential;
        String person;
        unsigned long validsince;
        unsigned long last_updated;
        bool is_banned;
        unsigned long validuntil;
    };

	StaticJsonDocument<512> jsonRecord;
    UserRecord currentUser;

    protected:
    unsigned long lastMilli;
    unsigned long coolDownStart;
};

// void cardRead1Handler(ProxReaderInfo* reader);

extern TCMWiegandClass TCMWiegand;
extern AccessControlClass AccessControl;

#endif