/**
 * @file door.cpp
 * @author Brad Ferguson (brad.ferguson@tcmaker.org)
 * @brief Handles garage door I/O integration
 * @version 0.2
 * @date 2023-05-23
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include "door.h"

#define DEBUG_SERIAL if(DEBUG)Serial

void debugPrint(const char *info)
{
  Serial.printf("milliseconds: %lu - %s\n", millis(), info);
}

bool BounceWithCB::update() 
{
  bool changed = Bounce::update();
  if (changed && onStateChangeCB) {
    onStateChangeCB(read());
  }
  return changed;
}

void BounceWithCB::begin()
{
  Bounce::begin();
  if (onStateChangeCB) {
    onStateChangeCB(read());
  }
}

Door::Door(Bounce *door_sensor, Relay *lock, Relay *indicator, Relay *alarm, Relay *open, Relay *close)
    : maxOpenTime(0), state(DoorState::unlocked), lastMilli(millis())
{
  this->statusPin = door_sensor;
  relays.unlock = lock;
  relays.indicator = indicator;
  relays.alarm = alarm;
  relays.open = open;
  relays.close = close;
}

void Door::begin()
{

  if (relays.unlock)
    relays.unlock->begin();
  if (relays.indicator)
    relays.indicator->begin();
  if (relays.alarm)
    relays.alarm->begin();
  if (relays.open)
    relays.open->begin();
  if (relays.close)
    relays.close->begin();
  if (statusPin)
    statusPin->update();

  if (!statusPin)
  {
    // no status input, so presume secure
    state = secure;
  }
  else if (statusPin->read() == 1)
  {
    // door open at boot - presuem this is OK
    DEBUG_SERIAL.printf("[ DEBUG ] Door open, assuming previously activated...\n");
    // activate will set state
    this->activate();
  }
  else if (statusPin->read() == 0)
  {
    state = locking;
  }
}

Door::DoorState Door::status()
{
  return state;
}

/**
 * @brief Initiate the chain of events necessary to unlock and/or open the door.
 *
 */
void Door::activate()
{
  DEBUG_SERIAL.printf("milliseconds: %lu - door activating\n", millis());
  state = unlocked;
  lastMilli = millis();

  relays.unlock->activate();

  if (relays.indicator)
    relays.indicator->activate();
}

void Door::deactivate()
{
  DEBUG_SERIAL.println("[ WARN ] Door::deactivate() not implemented.");
}

bool Door::update()
{
  static bool trigger_open = false;

  bool acted = false;

  if (relays.unlock)
    relays.unlock->update();
  if (relays.indicator)
    relays.indicator->update();
  if (relays.alarm)
    relays.alarm->update();
  if (relays.open)
    relays.open->update();
  if (relays.close)
    relays.close->update();
  if (statusPin)
    statusPin->update();

  switch (state)
  {
  /**
   * @brief door is closed and locked
   *
   */
  case secure: // (closed and locked)
    if (relays.alarm && relays.alarm->state == Relay::OperationState::active)
    {
      relays.alarm->release();
      relays.alarm->deactivate();
    }
    if (relays.indicator && (relays.indicator->state == Relay::OperationState::active))
    {
      relays.indicator->release();
      relays.indicator->deactivate();
    }

    // exits from this state are fob scan and tamper
    // only the fob scan exit will result in triggering an open signal
    trigger_open = true;

    if (this->statusPin->read() == 1)
    {
      lastMilli = millis();
      trigger_open = false;
      state = tamper; // tamper
      // send alarm
      debugPrint("tamper detected");
      if (tamperCallBack) {
        tamperCallBack(true);
      }
    }
    break;

  /**
   * @brief door is unlocked, waiting to see if it is opened otherwise unlock will timeout
   * and relock the door
   *
   * exits from state:
   *  * to `open` if the status pin detects the door has been opened
   *  * to `locking` if the unlock relay has returned to `inactive`
   */
  case unlocked:
    if (statusPin && statusPin->read() == 1) // door has been opened
    {
      lastMilli = millis();

      // interlock is held unlocked while door is open
      relays.unlock->hold();

      if (relays.indicator) // && relays.indicator->override == Relay::OverrideState::normal)
        relays.indicator->hold();

      state = open; // Door open
      debugPrint("door opened");
    }
    else if (trigger_open && relays.unlock->state == Relay::OperationState::active)
    {
      // wait until unlock relay has fully moved from inactive -> activating -> active
      // trigger open one time if transitioned from secure state

      trigger_open = false;
      if (relays.open && relays.open->state == Relay::OperationState::inactive)
      {
        relays.open->activate();
      }
    }
    else if (relays.unlock->state == Relay::OperationState::inactive)
    {                       // lock relay has automatically deactivated
      if (relays.indicator) // && relays.indicator->override != Relay::OverrideState::normal)
      {
        relays.indicator->release();
        // relays.indicator->deactivate();
      }

      lastMilli = millis();
      state = locking; // to verify door still closed after lock
      debugPrint("door relocking");
    }
    // else maintain state
    break;

  /////////////////////////////////////////////
  // intermediate state: open and/or unlocked to locked
  case locking:
    if (this->statusPin->read() == 1) // door opened before lock succeeded
    {
      // unlock door, go back to open state
      // do not reset lastMillis
      relays.unlock->hold();
      if (relays.indicator)
        relays.indicator->hold();

      state = open;
      acted = true;
      debugPrint("door failed to relock in time -- door open");
    }
    else if (relays.unlock->state == Relay::OperationState::inactive)
    { // door succesfully locked
      state = secure;
      debugPrint("door closed and locked");
    }
    break;

  /////////////////////////////////////////////
  case open:                                 // Door open
    if (statusPin && statusPin->read() == 0) // door has been closed
    {
      relays.unlock->release(); // does not deactivate() in case actuationTime is still running.
      state = locking;
      acted = true;
      debugPrint("door closed -- door locking");
    }
    else if (maxOpenTime && (millis() - lastMilli > maxOpenTime * 1000))
    {
      // relays.alarm->trigger();
      if (relays.alarm)
      {
        relays.alarm->activate();
      }
      state = alarm;
      acted = true;
      debugPrint("door exceeded maxOpenTime -- alarm on");
    }
    // else maintain state
    break;

  /////////////////////////////////////////////
  // Alarm state set when door opened unexpectedly or door open too long
  // only available when statusPin is setup
  case alarm:
    if (statusPin->read() == 0) // door closed
    {
      relays.unlock->release();
      relays.unlock->deactivate();
      if (relays.indicator) // && relays.indicator->override != Relay::OverrideState::normal)
      {
        relays.indicator->release();
        relays.indicator->deactivate();
      }

      state = unalarming; // to verify door locked from alarm state
      acted = true;
      debugPrint("door closed -- door locking");
    }
    break;

  /////////////////////////////////////////////
  // transitiion from alarm to secure
  case unalarming:
    if (this->statusPin->read() == 1) // door re-opened before lock succeeded
    {
      relays.unlock->hold();
      if (relays.indicator && relays.indicator->override == Relay::OverrideState::normal)
        relays.indicator->hold();
      state = alarm; // back to alarm state
      acted = true;
      debugPrint("door failed to relock -- door open -- alarming");
    }
    else
    {
      state = secure;
      acted = true;
      debugPrint("door closed -- alarm off");
      if (alarmCallBack) {
        alarmCallBack(false);
      }
    }
    break;

  /////////////////////////////////////////////
  // tamper state is entered when unexpect door opening is detected
  // there is a delay to avoid intermittent contact opening (wind?)
  // causing nuisance alarms
  case tamper:
    if (this->statusPin->read() == 0)
    {
      // relays.unlock->release();
      // relays.unlock->deactivate();
      state = secure; // to verify door locked from alarm state
      acted = true;
      debugPrint("door closed -- door locked");
      if (tamperCallBack) {
        tamperCallBack(false);
      }
    }
    else if (millis() > lastMilli + 10000)
    {
      // relays.alarm->forceOn();
      relays.unlock->hold();
      if (relays.indicator && relays.indicator->override == Relay::OverrideState::normal)
        relays.indicator->hold();
      state = alarm;
      acted = true;
      debugPrint("tamper alarm triggered");
      if (alarmCallBack) {
        alarmCallBack(true);
      }
    }
    break;

  /////////////////////////////////////////////
  default:
    lastMilli = millis();
    DEBUG_SERIAL.printf("Door::update(): unknown state: %d", state);
    state = alarm;
    if (alarmCallBack) {
      alarmCallBack(true);
    }
    // log error
    break;
  }
  return acted;
}