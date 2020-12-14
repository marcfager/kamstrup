/*
Kamstrup Reader for ESP8266. Reads values with optical eye and sends values over MQTT.
Tested on a nodeMCU 0.9 board with a Kastrup 684-34a series electric meter.

Optical eye: https://wiki.hal9k.dk/projects/kamstrup
Based on example "powermeter" by hal9k-dk (https://github.com/Hal9k-dk/kamstrup/tree/master/Software%20eksempler/kamstrup_powermeter)
Using PubSubClient library for MQTT: https://github.com/knolleary/pubsubclient
Using ESP8266 for Arduino: https://github.com/esp8266/Arduino

The license for the original code is not defined. If later defined, the same license will be valid for this code.
Feel free to use and modify this code, but please include credits to the original authors mentioned above.

Marcfager December 12 2020

*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>

// WiFi & MQTT setup
const char* ssid = "AP Name";                 // WiFi AP SSID
const char* password = "AP Password";        // WiFi AP Password
const char* mqtt_server = "10.10.10.10";   // MQTT Server host
const String mqtt_clientId = "KamstrupEl";  // MQTT Client ID (and mDNS name)
const char* mqtt_prefix = "/openhab/kamstrupEl"; // MQTT topic prefix

// Optical Eye pins
#define PIN_KAMSER_RX  4  // Kamstrup IR interface RX
#define PIN_KAMSER_TX  5  // Kamstrup IR interface TX

//Kamstrup setup
// Parameters for Energy in, Energy in High-res, Momentary power, Maximum power, Voltage phases 1-3, Current phases 1-3, Power phases 1-3
// The kregstrings are used as MQTT topics for the values sent
word const kregnums[] = { 0x0001,0x000d,0x03ff,0x0027,0x041e,0x041f,0x0420,0x0434,0x0435,0x0436,0x438,0x439,0x43a };
char* kregstrings[]   = { "EIN","EINHR", "P","Pmax","U1","U2","U3", "I1", "I2", "I3", "P1", "P2", "P3"};
#define NUMREGS 13     // Number of registers above
#define KAMBAUD 1200
#define KAMTIMEOUT 1000  // Kamstrup timeout after transmit
const boolean allowZero = true; // If value is 0 (potential fault), send it still on MQTT.

// Perferences
// How often should values be read & sent to MQTT
const unsigned long pollFreq = 60000;


WiFiClient espClient;
PubSubClient client(espClient);
SoftwareSerial kamSer(PIN_KAMSER_RX, PIN_KAMSER_TX, false);  // Initialize serial

unsigned long lastMessage = 0; // Millis when last publish to MQTT was done
byte connAtt = 0; // Connection attempts made (wifi_setup ran)

// Setup WiFi
boolean setup_wifi() {
  if (connAtt > 10) {
    Serial.println();
    Serial.println("Resetting mcu due to 10 failed connection attempts");
    reboot();
  }
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi SSID ");
  Serial.print(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  byte att = 0;
  while ((WiFi.status() != WL_CONNECTED) && (att < 30)) {
    delay(1000);
    Serial.print(".");
    att++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    randomSeed(micros());
    Serial.println();
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  else {
    Serial.println();
    Serial.println("WiFi connection failed!");
    connAtt++;
    return false;
  }
}

// Reboot function
void reboot() {
  wdt_disable();
  wdt_enable(WDTO_15MS);
  while (1) {}
}

// Setup MQTT
boolean setup_mqtt() {
  byte att = 0;
  while (!client.connected() && (att < 10)) {
    Serial.print("Connecting to MQTT Server ");
    Serial.print(mqtt_server);
    char wtopic[100];
    strcpy(wtopic, mqtt_prefix);
    strcpy(wtopic, "/status");
    if (client.connect(mqtt_clientId.c_str(), wtopic, 0, true, "OFFLINE")) {
      Serial.println();
      Serial.print("MQTT Connected! Client id: ");
      Serial.println(mqtt_clientId);
      char topic[100];
      strcpy(topic, mqtt_prefix);
      strcat(topic, "/status");
      Serial.println(topic);
      client.publish(topic, "ONLINE");
      return true;
    } else {
      Serial.print(".");
      delay(3000);
    }
    att++;
    delay(3000);
  }
  Serial.println();
  Serial.print("MQTT connection failed. RC: ");
  Serial.println(client.state());
  return false;
}

// Publish to MQTT
void publish(char* subtopic, char* msg) {
  char topic[100];
  strcpy(topic, mqtt_prefix);
  strcat(topic, "/");
  strcat(topic, subtopic);
  client.publish(topic, msg);
}

// Read kamstrup registers
void readKamstrup() {
  // Read all registers
  for (int kreg = 0; kreg < NUMREGS; kreg++) {
    float val = 0;
    byte att = 0;
    // 15 attempts to read value (value != 0)
    while ((val == 0) && (att < 15)) {
      val = kamReadReg(kreg);
      delay(200);
      att++;
    }
    // If value is OK or allowZero is set
    if ((val != 0) || allowZero) {
      char dval[20];
      dtostrf(val, 10, 5, dval);
      publish(kregstrings[kreg], dval);
    }
  }
}

// Setup function
void setup() {
  Serial.begin(57200);
  delay(1000);
  // Setup kamstrup serial
  pinMode(PIN_KAMSER_RX,INPUT);
  pinMode(PIN_KAMSER_TX,OUTPUT);
  kamSer.begin(KAMBAUD);
  delay(200);
  Serial.println();
  Serial.println("Kamstrup MQTT Boot");
  setup_wifi();
  MDNS.begin(mqtt_clientId);
  client.setServer(mqtt_server, 1883);
  if (WiFi.status() == WL_CONNECTED) {
    setup_mqtt();
  }
}

// Loop function
void loop() {
  // If WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    // If MQTT is connected
    if (client.connected()) {
      // If pollFreq time since last message
      if (millis() - lastMessage > pollFreq) {
        publish("status", "ONLINE");
        // Read Kamstrup values & send to MQTT
        readKamstrup();
        lastMessage = millis();
      }
    } else {
      // MQTT not connected, reconnect
      setup_mqtt();
    }
  } else {
    // WiFi not connected, reconnect
    setup_wifi();
  }
  if (!client.connected()) {
    setup_wifi();
  }
  client.loop();

}

// kamReadReg - read a Kamstrup register
float kamReadReg(unsigned short kreg) {

  byte recvmsg[40];  // buffer of bytes to hold the received data
  float rval;        // this will hold the final value

  // prepare message to send and send it
  byte sendmsg[] = { 0x3f, 0x10, 0x01, (kregnums[kreg] >> 8), (kregnums[kreg] & 0xff) };
  kamSend(sendmsg, 5);

  // listen if we get an answer
  unsigned short rxnum = kamReceive(recvmsg);

  // check if number of received bytes > 0 
  if(rxnum != 0){
    
    // decode the received message
    rval = kamDecode(kreg,recvmsg);
    
    // print out received value to terminal (debug)
    Serial.print(kregstrings[kreg]);
    Serial.print(": ");
    Serial.print(rval);
    Serial.print(" ");
    Serial.println();
    
    return rval;
  }
}

// kamSend - send data to Kamstrup meter
void kamSend(byte const *msg, int msgsize) {

  // append checksum bytes to message
  byte newmsg[msgsize+2];
  for (int i = 0; i < msgsize; i++) { newmsg[i] = msg[i]; }
  newmsg[msgsize++] = 0x00;
  newmsg[msgsize++] = 0x00;
  int c = crc_1021(newmsg, msgsize);
  newmsg[msgsize-2] = (c >> 8);
  newmsg[msgsize-1] = c & 0xff;

  // build final transmit message - escape various bytes
  byte txmsg[20] = { 0x80 };   // prefix
  int txsize = 1;
  for (int i = 0; i < msgsize; i++) {
    if (newmsg[i] == 0x06 or newmsg[i] == 0x0d or newmsg[i] == 0x1b or newmsg[i] == 0x40 or newmsg[i] == 0x80) {
      txmsg[txsize++] = 0x1b;
      txmsg[txsize++] = newmsg[i] ^ 0xff;
    } else {
      txmsg[txsize++] = newmsg[i];
    }
  }
  txmsg[txsize++] = 0x0d;  // EOF

  // send to serial interface
  for (int x = 0; x < txsize; x++) {
    kamSer.write(txmsg[x]);
  }

}

// kamReceive - receive bytes from Kamstrup meter
unsigned short kamReceive(byte recvmsg[]) {

  byte rxdata[50];  // buffer to hold received data
  unsigned long rxindex = 0;
  unsigned long starttime = millis();
  
  kamSer.flush();  // flush serial buffer - might contain noise

  byte r;
  
  // loop until EOL received or timeout
  while(r != 0x0d){
    
    // handle rx timeout
    if(millis()-starttime > KAMTIMEOUT) {
      Serial.println("Timed out listening for data");
      return 0;
    }

    // handle incoming data
    if (kamSer.available()) {

      // receive byte
      r = kamSer.read();
      if(r != 0x40) {  // don't append if we see the start marker
        // append data
        rxdata[rxindex] = r;
        rxindex++; 
      }

    }
  }

  // remove escape markers from received data
  unsigned short j = 0;
  for (unsigned short i = 0; i < rxindex -1; i++) {
    if (rxdata[i] == 0x1b) {
      byte v = rxdata[i+1] ^ 0xff;
      if (v != 0x06 and v != 0x0d and v != 0x1b and v != 0x40 and v != 0x80){
        Serial.print("Missing escape ");
        Serial.println(v,HEX);
      }
      recvmsg[j] = v;
      i++; // skip
    } else {
      recvmsg[j] = rxdata[i];
    }
    j++;
  }
  
  // check CRC
  if (crc_1021(recvmsg,j)) {
    Serial.println("CRC error: ");
    return 0;
  }
  
  return j;
  
}

// kamDecode - decodes received data
float kamDecode(unsigned short const kreg, byte const *msg) {

  // skip if message is not valid
  if (msg[0] != 0x3f or msg[1] != 0x10) {
    return false;
  }
  if (msg[2] != (kregnums[kreg] >> 8) or msg[3] != (kregnums[kreg] & 0xff)) {
    return false;
  }
    
  // decode the mantissa
  long x = 0;
  for (int i = 0; i < msg[5]; i++) {
    x <<= 8;
    x |= msg[i + 7];
  }
  
  // decode the exponent
  int i = msg[6] & 0x3f;
  if (msg[6] & 0x40) {
    i = -i;
  };
  float ifl = pow(10,i);
  if (msg[6] & 0x80) {
    ifl = -ifl;
  }

  // return final value
  return (float )(x * ifl);

}

// crc_1021 - calculate crc16
long crc_1021(byte const *inmsg, unsigned int len){
  long creg = 0x0000;
  for(unsigned int i = 0; i < len; i++) {
    int mask = 0x80;
    while(mask > 0) {
      creg <<= 1;
      if (inmsg[i] & mask){
        creg |= 1;
      }
      mask>>=1;
      if (creg & 0x10000) {
        creg &= 0xffff;
        creg ^= 0x1021;
      }
    }
  }
  return creg;
}
