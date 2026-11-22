/*
      Copyright 2015-2018 Dirk-Willem van Gulik <dirkx@webweaving.org>
                          Stichting Makerspace Leiden, the Netherlands.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, softwareM
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF
   ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Compile settings:  ESP32-WROOM-DA Module (or ESP32 Dev)

  QR code shown:

   https://wiki.makerspaceleiden.nl/mediawiki/index.php/QR_dustbin
*/
#include <BlueNodev114.h>
#include <HTTPClient.h>

#ifndef ARDUINO_ESP32_WROOM_DA
#error "Black/Blue/White Hardware is expected to be an ESP32 WROOM-DA"
#endif

#ifndef ARDUINO_PARTITION_min_spiffs
#error "Unexpected partition table; may break OTA"
#endif

#ifndef MACHINE
#define MACHINE "dustbin"  // Student test/demo node
#endif

// BINFULL is the name of the hardware input of the board to which the IR sensor is connected 
// IOA is defined in BlackNodev111.h as: IOA = PIN_HPIO_AW9523 | (0+5);
// It is used todetect HIGH=5V when bin is not full, or LOW=0 when bin is full from the IR sensor
#define BINFULL (node.IOA)  
#define SUCCON (node.OPTO0)    // Detects 12V coming from Succubus on OPTO1 input if it's running
#define RELAY_GPIO (node.OUT0) // Relay which opens this 12V line, disabling woodworking machines, when bin is full

// Generate with 'echo -n Password | openssl md5 or
// use https://www.md5hashgenerator.com/. No \0,
// cariage return or linefeed  at the end of the
// password; just the characters of the passwordz
// itself.
//
#ifndef OTA_PASSWD_HASH256
#error "An OTA password hash(md5) MUST be set. Sorry."
#endif
const char ota_password_hash[] = OTA_PASSWD_HASH256;

auto node = BlueNodev114(MACHINE,WIFI_NETWORK,WIFI_PASSWD);
bool bin_full = false;
bool succubus_on = false;
/
/ BINFULL measurement logic: if in two consecutive 5-measurement windows at least one of the
// measurements shows low=bin_full - then the bin is full
bool previous_window = false;               // remembers result of prev. window measurement
bool succubus_known = false;

bool initial_bin_check_complete = false;
bool reboot_pending = false;

unsigned long last_bin_check = 0;
unsigned long reset_message_start = 0;

const unsigned long BIN_CHECK_INTERVAL = 10*1000; // duration between BINFULL  measurement windows, in mSeconds
const unsigned long RESET_MESSAGE_TIME = 5*1000;  // duration to show "emptied" message on display, in mSeconds

#ifdef SUCCON             // IODebounce is a helper class which cleans up hardware inputs.
IODebounce *succonCurrent;// using a *pointer here so that if this macro is not defined /
#endif                    // HW is not installed, the compiler does not allocate memory
                          //  for this code (memory saving trick)

MachineState::machinestate_t BINFULL_STATE;   // Define machine state when IR sensor drops to 0 and relay opens
MachineState::machinestate_t RUNNING;// Normal operational state

class MachineDeck : public Deck {
public:
  MachineDeck(auto *node)
    : Deck(node){};

  void render_pane(bool refresh) {
    if (!_display) return;

//    _display->clearDisplay();
    _display->print_centred("DUSTBIN I/O");

    _display->setCursor(0, 18);
    _display->printf("IOA:   %d\n", ::node.xdigitalRead(BINFULL));
    _display->printf("OPTO1: %d\n", ::node.xdigitalRead(SUCCON) == LOW);
    _display->printf("OUT0:  %d\n", ::node.xdigitalRead(RELAY_GPIO));
  }
};

void publishEvent(const char *event) {
  JsonDocument doc;

  doc["event"] = event;
  doc["bin_full"] = bin_full;
  doc["succubus_on"] = succubus_on;

  char topic[96];
  snprintf(topic, sizeof(topic), "ac/event/%s", MACHINE);

  node.mqttJsonPost(topic, doc);
}

bool sampleBinWindow() {    // measure 5 times each 10ms, return TRUE if at least one is LOW
  bool low_seen = false;

  for (int i = 0; i < 5; i++) {
    int reading = node.xdigitalRead(BINFULL);
    if (reading != HIGH) low_seen = true;
    delay(10);
  }
  return low_seen;
}

void checkBin() {                             // Runs two measurement windows
  bool current_window = sampleBinWindow();
  if (current_window   && previous_window && !bin_full) {    // Two consecutive windows containing LOW mean the bin is full.
    bin_full = true;                          // this flag stays set until RFID-triggered reboot
    node.setMonitoredOutput(RELAY_GPIO, LOW); // opens the relay, prevents woodworking machines from running
    node.machinestate = BINFULL_STATE;        // change machine state
    Log.println("Dust bin FULL - relay opened.");
    publishEvent("bin_full");
  }
  previous_window = current_window;     // Remember this window for the next check.
  static bool previous_window_clean = false;    // At startup, require two consecutive clean windows.
  if (!initial_bin_check_complete && !bin_full) { // initial check: if bin not full, close the relay
    if (!current_window && previous_window_clean) {
      initial_bin_check_complete = true;
      node.setMonitoredOutput(RELAY_GPIO, HIGH);
      node.machinestate = RUNNING;
      Log.println("Bin confirmed NOT full - relay closed.");
    }
  }
  previous_window_clean = !current_window;    // window is clean when current_window stayed low 
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Log.printf("\nBooting(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
  // Initialize the relay output to LOW (not allowing wood machines run until the bin is verified not full)
  digitalWrite(RELAY_GPIO, LOW);            // safety: set the pin to LOW even before enabling output
  pinMode(RELAY_GPIO, OUTPUT);              // configures hardware pin RELAY_GPIO as output
  node.setMonitoredOutput(RELAY_GPIO, LOW);// sets RELAY_GPIO and remembers its expected state for monitoring
  
  // Define machine states
  RUNNING = node.machinestate.addState("Bin not full - Normal", LED::LED_ON,
                                       MachineState::NEVER, MachineState::WAITINGFORCARD, false);
  BINFULL_STATE = node.machinestate.addState("Bin FULL - Interlocked",
                                            LED::LED_ON, MachineState::NEVER, MachineState::WAITINGFORCARD, false);

  #ifdef SUCCON
    succonCurrent = new IODebounce("suction_status", SUCCON);
    node.addHandler(succonCurrent);

    succonCurrent->setCallback([](const int newState) { // only runs on change of 12V succo line state
      bool new_on = (newState == LOW);                  // OPTO is low when there's 12V on input

      if (!succubus_known || new_on != succubus_on) {
        succubus_on = new_on;                             // current succo state
        succubus_known = true;                            // have we established this state?
        if (succubus_on) {
          Debug.println("Succubus running (12V detected on OPTO1).");
          publishEvent("succubus_running");
        } else {
          Debug.println("Succubus off (no 12V on OPTO1).");
          publishEvent("succubus_off");
        }
      }
    });
  #endif

  node.setOTAPasswordHash(ota_password_hash);   // Sets the Over-The-Air pwd so we can securely
  node.set_mqtt_prefix("ac");                   // upload firmware over WiFi without USB
  node.set_master("master");                    // "ac" is a prefix, "master" is the MQTT server/broker ID

  node.setNodeDeck(new MachineDeck(&node)); // Use the "MachineDeck" display layout
  node.setMenuCallback([](const int newState) -> bool { // Pressing MORE gets node into INFODISPLAY state, opening diagnostic screens
    if (newState == LOW && (node.machinestate == RUNNING ||node.machinestate == BINFULL_STATE)) {
      node.machinestate = INFODISPLAY;
      return true;
    }
    return false;
  }, ONLOW);    // when it returns from INFODISPLAY, it automatically goes to WAITINGFORCARD, not RUNNING or BINFULL_STATE. See node.setOnChangeCallback for fix

  node.onReport([](JsonObject report) { // Callback which runs when node sends periodic status report to MQTT
    char *p = (char *)__FILE__;
    char *q = rindex(p, '/');
    if (q) p = q + 1;
    char buff[128];
    snprintf(buff, sizeof(buff), "%s", p);
    if ((q = strstr(buff, ".ino"))) *q = '\0';
    JsonObject fw = report["fw"].to<JsonObject>();
    fw["file"] = buff;
    fw["compiled"] = __DATE__ " " __TIME__;
    report["bin_full"] = bin_full;
    report["succubus_on"] = succubus_on;
    report["relay_closed"] = initial_bin_check_complete && !bin_full;
  });

  node.begin(); //Boots WiFi, starts web server, connects to MQTT broker, init display, and starts bckgnd event handlers
  // This resets AW9523 pins, setting IOA to output. Since we want it as input, we move expandedPinModes after node.begin:
    #ifdef SUCCON
      expandedPinMode(SUCCON, INPUT);   // configures hardware pin assigned to SUCCON as input. Expanded for AW9523 IO chip
    #endif
    #ifdef BINFULL
      expandedPinMode(BINFULL, INPUT); // configures hardware pin assigned to BINFULL as input. Again, expanded = AW9523 IO expansion chip
    #endif

  succubus_on = (node.xdigitalRead(SUCCON) == LOW); // Read the initial Succubus state, LOW means 12V is present
  succubus_known = true;
  Debug.printf("Initial Succubus state: %s\n", succubus_on ? "running" : "off");

  // Update LCD on any transition between operational states
 node.setOnChangeCallback(MachineState::ALL_STATES,
  [](MachineState::machinestate_t last, MachineState::machinestate_t current) -> void {
    // this IF ensure it shows "starting up" on boot, not "waiting for card"
    if (current == MachineState::WAITINGFORCARD && !initial_bin_check_complete &&
    !bin_full && !reboot_pending) {
       node.updateDisplay("", "MORE", true);            // erases "waiting for card"
       node.updateDisplayStateMsg("starting up...", 1);
      }
    //
    if (last == INFODISPLAY &&          // Restore bin status after leaving the MORE screens.
        current == MachineState::WAITINGFORCARD &&
        !reboot_pending &&
        (initial_bin_check_complete || bin_full)) {
      node.machinestate = bin_full ? BINFULL_STATE : RUNNING;
      return;
    }
    if (current == RUNNING) {
      node.updateDisplay("", "MORE", true);             // erases "waiting for card"
      node.updateDisplayStateMsg("bin not full", 1);
    }
    if (current == BINFULL_STATE) {
      node.updateDisplay("", "MORE", true);
      node.updateDisplayStateMsg("bin FULL", 1);
    }
  });
  // Next line handles the case where node.begin() has already entered WAITINGFORCARD before our callback was registered.
  if (!initial_bin_check_complete) {
    node.updateDisplay("", "MORE", true);
    node.updateDisplayStateMsg("starting up...", 1);
  }

  // Runs when RFID is swiped and approved
  node.onApproval([](const char *machine) {
    if (reboot_pending) return;                   // Ignore RFIDs if reboot is already pending
    ApprovalEntry *user = node.lastApproved();    // Identify the approved user.
    const char *username = user ? user->displayName() : "Unknown user";
    // Report who emptied the bin to MQTT.
    JsonDocument doc;                             // Prepare the message (doc)
    doc["event"] = "bin_emptied";
    doc["user"] = username;
    doc["message"] = String("user ") + username + " just emptied the bin";
    doc["bin_full"] = bin_full;
    doc["succubus_on"] = succubus_on;
    char topic[96];
    snprintf(topic, sizeof(topic), "ac/event/%s", MACHINE);
    node.mqttJsonPost(topic, doc);                  // Post to MQTT
    node.updateDisplay("", "", true);               // Display the message who emptied the bin
    node.updateDisplayStateMsg("Bin emptied by:", 1);
    node.updateDisplayStateMsg(username, 2);
    reboot_pending = true;                         // Schedule the ESP32 restart.
    reset_message_start = millis();               
  });

  Log.printf("Starting loop(): %s " __DATE__ " " __TIME__ "\n", FILE2FIRMWARE(__FILE__));
}

void loop() {
  node.loop();  // Must be run continously. Services bgnd network tasks, updates sensors, 
                // processes debouncers, manages timeouts. Everything relies on this
  if (reboot_pending) {    // If reboot was triggered by RFID scan / bin emptying - wait and restart
    if (millis() - reset_message_start >= RESET_MESSAGE_TIME) ESP.restart();
    return;
  }
  // Announce initial state when MQTT becomes available. (also after reconnect)
  static bool mqtt_was_up = false;
  bool mqtt_is_up = node.isUp();
  if (mqtt_is_up && !mqtt_was_up) { 
    publishEvent("dustbin_online");
    publishEvent(succubus_on ? "succubus_running" : "succubus_off");
  }
  mqtt_was_up = mqtt_is_up;
  
  // Check sensor every 10 sec
  if (millis() - last_bin_check >= BIN_CHECK_INTERVAL) { 
    last_bin_check = millis();
    checkBin();
  }
};
