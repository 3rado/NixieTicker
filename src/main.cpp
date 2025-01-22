#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <EEPROM.h>
#include <YoutubeApi.h>
//#include <api_key.h>

#define EEPROM_SIZE 58 // 0=roll, 1=nastavenaprodleva, 2=sourceState, 3=sat, 4=resetwifi, 5=mode(heal), 6-55 channel id, 56 LED, 57 radar

// ADD YOUR YOUTUBE API KEY HERE
const char *youtubeApiKey = "YOUR API KEY";

const char *yt_root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG\n"
    "A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n"
    "b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw\n"
    "MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n"
    "YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT\n"
    "aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ\n"
    "jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp\n"
    "xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp\n"
    "1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG\n"
    "snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ\n"
    "U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8\n"
    "9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E\n"
    "BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B\n"
    "AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz\n"
    "yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE\n"
    "38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP\n"
    "AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad\n"
    "DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME\n"
    "HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==\n"
    "-----END CERTIFICATE-----\n";

WiFiServer server(80);

bool radarInstalled = 1;
bool LEDinstalled = 1;

int latchPin = 26;
int clockPin = 25;
int dataPin = 27;
int hvPin = 33;
int LEDpin = 32;
int radarPin = 14;
int oldValue;
int errorCount = 0; // if connection is lost, shows 000000-999999

int timeoutTime = 2000;
bool roll, sat, radarEnabled = 1;
bool detection = 0;
byte LEDSetting;
int LED = -1, mode = 0; // 0=normal, 1=heal;
String sourceState;
String channel_ID;

String header;
String body;

uint32_t millisTime = 0, lastHTTP = 0, lastRotate = 0, lastDetected = 0, lastClient = 0;
int waitTimeRotate = 120000, waitTimeHTTP, setWaitTimeHTTP, detectionTimeout = 30000, sleepRotations = 30, healState = 0;
double value;

int digits[10] = {0b0000000001,  // 0
                  0b1000000000,  // 1
                  0b0100000000,  // 2
                  0b0010000000,  // 3
                  0b0001000000,  // 4
                  0b0000100000,  // 5
                  0b0000010000,  // 6
                  0b0000001000,  // 7
                  0b0000000100,  // 8
                  0b0000000010}; // 9

void nixie(int, bool); // number, rotate
void rotate(int, int); // count, delay in ms
void rotate2(int rotateCount, int rotateDelay);
void rotate3(int rotateCount, int rotateDelay);
void settingsPage(void);

void LEDControl(byte LEDSetState);

bool getBinanceBTC();
bool getBinanceETH();
bool getCoindeskBTC();
bool getBlockHeight();
bool getYoutubeSubs();

void setup()
{
  int intip;

  pinMode(dataPin, OUTPUT);  // ser data
  pinMode(latchPin, OUTPUT); // latch
  pinMode(clockPin, OUTPUT); // clk
  pinMode(hvPin, OUTPUT);    // HV enable, active LOW
  pinMode(radarPin, INPUT);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(LEDpin, 0);

  EEPROM.begin(EEPROM_SIZE);
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  digitalWrite(hvPin, LOW);
  value = 0;
  Serial.begin(115200);
  rotate3(5, 33);

  roll = EEPROM.read(0);

  switch (EEPROM.read(1))
  {
  case 0:
    setWaitTimeHTTP = 2000;
    break;
  case 1:
    setWaitTimeHTTP = 5000;
    break;
  case 2:
    setWaitTimeHTTP = 10000;
    break;
  case 3:
    setWaitTimeHTTP = 30000;
    break;
  case 4:
    setWaitTimeHTTP = 60000;
    break;
  default:
    setWaitTimeHTTP = 5000;
  }

  switch (EEPROM.read(2))
  {
  case 0:
    sourceState = "Binance - BTC/USDT";
    break;
  case 1:
    sourceState = "Coindesk - BTC/USD";
    break;
  case 2:
    sourceState = "Binance - ETH/USDT";
    break;
  case 3:
    sourceState = "Block Height";
    break;
  case 4:
    sourceState = "Youtube Subs";
    break;
  default:
    sourceState = "Binance - BTC/USDT";
  }

  sat = EEPROM.read(3);

  mode = EEPROM.read(5);
  if (mode < 0 || mode > 2)
    mode = 0;

  int EEPROMaddress = 6;
  char readChar = 'A';
  channel_ID = "";

  while (readChar != '\0')
  {
    readChar = EEPROM.read(EEPROMaddress);
    if (readChar != '\0')
    {
      channel_ID += readChar;
    }
    EEPROMaddress++;
  }

  Serial.println(channel_ID);

  LEDSetting = EEPROM.read(56);
  LEDControl(LEDSetting);
  radarEnabled = EEPROM.read(57);

  WiFiManager wm;

  if (EEPROM.read(4) == 1)
  {
    wm.resetSettings(); // wipe credentials
    EEPROM.write(4, 0);
    EEPROM.commit();
    nixie(654321, 0);
  }

  wm.setClass("invert");
  wm.setHostname("Nixie");
  wm.setConfigPortalTimeout(180);
  digitalWrite(hvPin, HIGH);
  bool res;
  res = wm.autoConnect("NX Ticker");

  if (!res)
  {
    Serial.println("Failed to connect, restarting...");
    ESP.restart();
  }
  else
  {
    Serial.println("connected to WiFi");
    digitalWrite(hvPin, LOW);
    server.begin();

    Serial.print("IP : ");
    String adress = WiFi.localIP().toString().c_str();
    char *adressc = new char[adress.length() + 1];
    strcpy(adressc, adress.c_str());
    Serial.println(adressc);
    char *ptr = strtok(adressc, ".");

    while (ptr != NULL)
    {
      intip = atoi(ptr);
      nixie(intip, 0);
      ptr = strtok(NULL, ".");
      delay(1500);
    }
  }
}

void loop()
{
  settingsPage();

  if (radarInstalled && radarEnabled)
  {
    if (digitalRead(radarPin) == 1)
    {
      lastDetected = millisTime;
    }
    if (millisTime - lastDetected > detectionTimeout)
    {
      LEDControl(0);
      while (digitalRead(radarPin) == 0)
      {
        if (sleepRotations > 0)
        {
          rotate(1, 100);
          sleepRotations--;
        }
        else
        {
          digitalWrite(hvPin, HIGH);
          delay(200);
        }
        settingsPage();
      }
      sleepRotations = 20;
      LEDControl(LEDSetting);
      digitalWrite(hvPin, LOW);
      nixie(value, roll);
      millisTime = millis();
      lastRotate = millisTime;
    }
  }

  if (mode == 2) // test
  {
    rotate(1, 3000);
    mode = 0;
  }
  else if (mode == 1) // heal
  {
    if (healState > 999999)
    {
      healState = 0;
    }
    nixie(healState, 0);
    healState = healState + 111111;
    delay(3000);
  }
  else // normal
  {
    if (millisTime - lastRotate > waitTimeRotate)
    {
      rotate3(10, 25);
      nixie(value, roll);
      lastRotate = millisTime;
    }

    if (millisTime - lastHTTP > waitTimeHTTP)
    {
      lastHTTP = millisTime;
      Serial.print("Wifi signal: ");
      Serial.println(WiFi.RSSI());

      bool returned = 1;
      if (sourceState == "Binance - BTC/USDT")
        returned = getBinanceBTC();
      else if (sourceState == "Coindesk - BTC/USD")
        returned = getCoindeskBTC();
      else if (sourceState == "Binance - ETH/USDT")
        returned = getBinanceETH();
      else if (sourceState == "Block Height")
        returned = getBlockHeight();
      else if (sourceState == "Youtube Subs")
        returned = getYoutubeSubs();

      if (returned == 0)
      {
        Serial.print("Value: ");
        Serial.println(value, 0);
        nixie(value, roll);
        waitTimeHTTP = setWaitTimeHTTP;
        errorCount = 0;
      }
      else
      {
        Serial.println("Error on HTTP request");
        if (errorCount > 999999)
          ESP.restart();
        nixie(errorCount, 0);
        errorCount = errorCount + 111111;
        waitTimeHTTP = 10000;
      }
    }
    millisTime = millis();
  }
}

void nixie(int number, bool rolling)
{
  int stot, dest, tis, sto, des, jed;
  byte r[8];             // shift registers
  if (number >= 1000000) // if number is bigger than 1 000 000, shows only last 6 digits. Better solution would be to shift number from left to right
    number = number % 1000000;
  do
  {
    if (rolling == 0)
      oldValue = number;
    else
    {
      if (oldValue / 100000 > number / 100000)
        oldValue = oldValue - 100000;
      if (oldValue / 100000 < number / 100000)
        oldValue = oldValue + 100000;

      if ((oldValue % 100000) / 10000 > (number % 100000) / 10000)
        oldValue = oldValue - 10000;
      if ((oldValue % 100000) / 10000 < (number % 100000) / 10000)
        oldValue = oldValue + 10000;

      if ((oldValue % 10000) / 1000 > (number % 10000) / 1000)
        oldValue = oldValue - 1000;
      if ((oldValue % 10000) / 1000 < (number % 10000) / 1000)
        oldValue = oldValue + 1000;

      if ((oldValue % 1000) / 100 > (number % 1000) / 100)
        oldValue = oldValue - 100;
      if ((oldValue % 1000) / 100 < (number % 1000) / 100)
        oldValue = oldValue + 100;

      if ((oldValue % 100) / 10 > (number % 100) / 10)
        oldValue = oldValue - 10;
      if ((oldValue % 100) / 10 < (number % 100) / 10)
        oldValue = oldValue + 10;

      if (oldValue % 10 > number % 10)
        oldValue = oldValue - 1;
      if (oldValue % 10 < number % 10)
        oldValue = oldValue + 1;

      delay(50);
    }
    stot = oldValue / 100000;
    dest = (oldValue % 100000) / 10000;
    tis = (oldValue % 10000) / 1000;
    sto = (oldValue % 1000) / 100;
    des = (oldValue % 100) / 10;
    jed = oldValue % 10;

    r[0] = digits[stot];
    r[1] = (digits[stot] >> 8) | (digits[dest] << 2);
    r[2] = (digits[dest] >> 6) | (digits[tis] << 4);
    r[3] = digits[tis] >> 4;
    r[4] = digits[sto];
    r[5] = (digits[sto] >> 8) | (digits[des] << 4);
    r[6] = (digits[des] >> 4) | (digits[jed] << 6);
    r[7] = (digits[jed] >> 2);

    digitalWrite(latchPin, LOW);

    for (int j = 7; j >= 0; j--)
      shiftOut(dataPin, clockPin, MSBFIRST, r[j]);

    digitalWrite(latchPin, HIGH);
  } while (oldValue != number && rolling == 1);
}

void rotate(int rotateCount, int rotateDelay)
{
  for (int j = 0; j < rotateCount; j++)
    for (int i = 999999; i >= 0; i = i - 111111)
    {
      nixie(i, 0);
      delay(rotateDelay);
    }
}

void rotate2(int rotateCount, int rotateDelay) // ke vsem cislum pricita
{
  int rotationValue = value;
  for (int j = 0; j < rotateCount; j++)

    for (int i = 0; i <= 9; i++)
    {
      if (rotationValue >= 900000)
        rotationValue = rotationValue - 900000;
      else
        rotationValue = rotationValue + 100000;

      if (rotationValue % 100000 >= 90000)
        rotationValue = rotationValue - 90000;
      else
        rotationValue = rotationValue + 10000;
      if (rotationValue % 10000 >= 9000)
        rotationValue = rotationValue - 9000;
      else
        rotationValue = rotationValue + 1000;

      if (rotationValue % 1000 >= 900)
        rotationValue = rotationValue - 900;
      else
        rotationValue = rotationValue + 100;

      if (rotationValue % 100 >= 90)
        rotationValue = rotationValue - 90;
      else
        rotationValue = rotationValue + 10;
      if (rotationValue % 10 >= 9)
        rotationValue = rotationValue - 9;
      else
        rotationValue = rotationValue + 1;

      nixie(rotationValue, 0);
      delay(rotateDelay);
    }
}

void rotate3(int rotateCount, int rotateDelay) // opozdeny zacatek pricitani u kazdeho dalsiho cisla - had
{
  int rotationValue = value;
  int stepsCount = rotateCount * 10 + 40;
  for (int i = 0; i < stepsCount; i++)
  {

    if (i >= 40 && stepsCount - i > 0)
      if (rotationValue >= 900000)
        rotationValue = rotationValue - 900000;
      else
        rotationValue = rotationValue + 100000;

    if (i >= 32 && stepsCount - i > 8)
      if (rotationValue % 100000 >= 90000)
        rotationValue = rotationValue - 90000;
      else
        rotationValue = rotationValue + 10000;

    if (i >= 24 && stepsCount - i > 16)
      if (rotationValue % 10000 >= 9000)
        rotationValue = rotationValue - 9000;
      else
        rotationValue = rotationValue + 1000;

    if (i >= 16 && stepsCount - i > 24)
      if (rotationValue % 1000 >= 900)
        rotationValue = rotationValue - 900;
      else
        rotationValue = rotationValue + 100;

    if (i >= 8 && stepsCount - i > 32)
      if (rotationValue % 100 >= 90)
        rotationValue = rotationValue - 90;
      else
        rotationValue = rotationValue + 10;

    if (i >= 0 && stepsCount - i > 40)
      if (rotationValue % 10 >= 9)
        rotationValue = rotationValue - 9;
      else
        rotationValue = rotationValue + 1;

    nixie(rotationValue, 0);
    delay(rotateDelay);
  }
}

void settingsPage()
{
  WiFiClient client = server.available();
  int index = 0;
  int addres = 0;

  if (client)
  {
    millisTime = millis();
    lastClient = millisTime;
    Serial.println("New Client."); // print a message out in the serial port
    String currentLine = "";       // make a String to hold incoming data from the client
    while (client.connected() && millisTime - lastClient <= timeoutTime)
    { // loop while the client's connected
      millisTime = millis();
      if (client.available())
      {                         // if there's bytes to read from the client,
        char c = client.read(); // read a byte, then
        Serial.write(c);        // print it out the serial monitor
        header += c;
        if (c == '\n')
        { // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0)
          {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            if (header.indexOf("GET /rollovat") >= 0)
            {
              roll = 1;
              EEPROM.write(0, roll);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /nerollovat") >= 0)
            {
              roll = 0;
              EEPROM.write(0, roll);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /led") >= 0)
            {
              LEDSetting = 255;
              LEDControl(LEDSetting);
              EEPROM.write(56, LEDSetting);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /noled") >= 0)
            {
              LEDSetting = 0;
              LEDControl(LEDSetting);
              EEPROM.write(56, LEDSetting);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /dimled") >= 0)
            {
              LEDSetting = 80;
              LEDControl(LEDSetting);
              EEPROM.write(56, LEDSetting);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /radar") >= 0)
            {
              radarEnabled = 1;
              EEPROM.write(57, radarEnabled);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /noradar") >= 0)
            {
              radarEnabled = 0;
              lastRotate = millis();
              EEPROM.write(57, radarEnabled);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /prodleva/2s") >= 0)
            {
              setWaitTimeHTTP = 2000;
              EEPROM.write(1, 0);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /prodleva/5s") >= 0)
            {
              setWaitTimeHTTP = 5000;
              EEPROM.write(1, 1);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /prodleva/10s") >= 0)
            {
              setWaitTimeHTTP = 10000;
              EEPROM.write(1, 2);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /prodleva/30s") >= 0)
            {
              setWaitTimeHTTP = 30000;
              EEPROM.write(1, 3);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /prodleva/1m") >= 0)
            {
              setWaitTimeHTTP = 60000;
              EEPROM.write(1, 4);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /coindesk-btcusd") >= 0)
            {
              sourceState = "Coindesk - BTC/USD";
              setWaitTimeHTTP = 60000;
              EEPROM.write(1, 4);
              EEPROM.write(2, 1);
              EEPROM.commit();
              Serial.println(sourceState);
              lastHTTP = lastHTTP - 60000;
            }
            else if (header.indexOf("GET /binance-btcusdt") >= 0)
            {
              sourceState = "Binance - BTC/USDT";
              EEPROM.write(2, 0);
              EEPROM.commit();
              Serial.println(sourceState);
              lastHTTP = lastHTTP - 60000;
            }
            else if (header.indexOf("GET /binance-ethusdt") >= 0)
            {
              sourceState = "Binance - ETH/USDT";
              EEPROM.write(2, 2);
              EEPROM.commit();
              Serial.println(sourceState);
              lastHTTP = lastHTTP - 60000;
            }
            else if (header.indexOf("GET /block-height") >= 0)
            {
              sourceState = "Block Height";
              setWaitTimeHTTP = 60000;
              EEPROM.write(2, 3);
              EEPROM.commit();
              Serial.println(sourceState);
              lastHTTP = lastHTTP - 60000;
            }
            else if (header.indexOf("GET /youtube") >= 0)
            {
              sourceState = "Youtube Subs";
              EEPROM.write(2, 4);
              EEPROM.commit();
              Serial.println(sourceState);
              lastHTTP = lastHTTP - 60000;
            }
            else if (header.indexOf("GET /sat") >= 0)
            {
              sat = 1;
              EEPROM.write(3, 1);
              EEPROM.commit();
              lastHTTP = lastHTTP - 60000;
            }
            else if (header.indexOf("GET /btc") >= 0)
            {
              sat = 0;
              EEPROM.write(3, 0);
              EEPROM.commit();
              lastHTTP = lastHTTP - 60000; // update displayed value now
            }
            else if (header.indexOf("GET /resetwifi") >= 0)
            {
              EEPROM.write(4, 1);
              EEPROM.commit();
              ESP.restart();
            }
            else if (header.indexOf("GET /healon") >= 0)
            {
              mode = 1;
              EEPROM.write(5, 1);
              EEPROM.commit();
            }
            else if (header.indexOf("GET /healoff") >= 0)
            {
              mode = 0;
              EEPROM.write(5, 0);
              EEPROM.commit();
              lastRotate = millisTime;
            }
            else if (header.indexOf("GET /test") >= 0)
            {
              mode = 2;
            }
            else if (header.indexOf("POST /submit") >= 0)
            {
              while (client.available())
              {
                c = client.read();
                body += c;
              }

              if (body.indexOf("channel_ID") >= 0)
              {
                index = body.indexOf("=");
                if (index != -1)
                {
                  String newChannel_ID = body.substring(index + 1);
                  if (newChannel_ID.length() < 50 && newChannel_ID.length() > 5)
                  {
                    channel_ID = newChannel_ID;
                    addres = 6;
                    for (int i = 0; i < channel_ID.length(); i++)
                    {
                      EEPROM.write(addres, channel_ID[i]);
                      addres++;
                    }
                    EEPROM.write(addres, '\0');
                    EEPROM.commit();
                  }
                  else
                  {
                    Serial.println("channel ID is too long or too short");
                  }
                }
                else
                  Serial.println("channel ID not found");
              }
              body = "";
            }
            else
            {
              Serial.println("wtf did you send me?");
            }

            waitTimeHTTP = setWaitTimeHTTP;

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");

            client.println("<style>html { font-family: 'Helvetica'; display: inline-block; margin: 0px auto; text-align: center; background-color:#000000; color:#FFFFFF}");
            client.println(".button { background-color: #202020; width: 320px; border: 2px solid #666666; border-radius: 20px; color: white; padding: 12px; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {border: 2px solid #FF9900;}");
            client.println(".button3 {width: 155px;}");
            client.println(".button4 {border: 2px solid #FF0000;}");

            client.println(".dropdown {position: relative; display: inline-block;}");
            client.println(".dropdown-content {display: none; font-size: 20px; background-color: #202020; border-radius: 20px; border: 2px solid #666666; z-index: 1;}");
            client.println(".dropdown-content a {color: #FFFFFF; padding: 12px 16px; text-decoration: none; display: block; }");
            client.println(".dropdown-content a:hover {background-color: #000000; border-radius: 20px;}");
            client.println(".dropdown:hover .dropdown-content {display: block;}");
            client.println(".input { background-color: #202020; border: 2px solid #666666; border-radius: 20px; color: white; padding: 12px; font-size: 30px; width: 295px; margin: 2px; outline: none;}");
            client.println("h1{font-family: 'Courier New', monospace; font-size: 45px}");

            client.println("</style></head>");

            client.println("<body><h1>NX Ticker</h1>");

            if (roll == 0)
              client.println("<p><a href=\"/rollovat\"><button class=\"button\">Roll : OFF</button></a></p>");
            else
              client.println("<p><a href=\"/nerollovat\"><button class=\"button button2\">Roll : ON</button></a></p>");

            if (LEDinstalled)
            {
              if (LED == 0)
                client.println("<p><a href=\"/led\"><button class=\"button\">LED : OFF</button></a></p>");
              else if (LED > 0 && LED < 255)
                client.println("<p><a href=\"/noled\"><button class=\"button button2\">LED : DIM</button></a></p>");
              else
                client.println("<p><a href=\"/dimled\"><button class=\"button button2\">LED : ON</button></a></p>");
            }

            if (radarInstalled)
            {
              if (radarEnabled == 0)
                client.println("<p><a href=\"/radar\"><button class=\"button\">Motion Detection : OFF</button></a></p>");
              else
                client.println("<p><a href=\"/noradar\"><button class=\"button button2\">Motion Detection : ON</button></a></p>");
            }

            client.println("<p><div class=\"dropdown\">  <button class=\"button button2\">Refresh Rate: ");
            client.print(setWaitTimeHTTP / 1000);
            client.println("s</button> <div class=\"dropdown-content\">   <a href=\"/prodleva/2s\">2 sec</a> <a href=\"/prodleva/5s\">5 sec</a>   <a href=\"/prodleva/10s\">10 sec</a>   <a href=\"/prodleva/30s\">30 sec</a>  <a href=\"/prodleva/1min\">1 min</a> </div></div></p>");

            client.println("<p><div class=\"dropdown\">  <button class=\"button button2\">Data Source</button> <div class=\"dropdown-content\">   <a href=\"/coindesk-btcusd\">Coindesk - BTC/USD</a> <a href=\"/binance-btcusdt\">Binance - BTC/USDT (fast)</a><a href=\"/binance-ethusdt\">Binance - ETH/USDT (fast)</a> <a href=\"/block-height\">Block Height</a> <a href=\"/youtube\">Youtube Subs</a> </div></div></p>");
            client.println("<p>Current data soucre: " + sourceState + "</p>");

            if (sourceState == "Youtube Subs")
            {
              client.println("<p>Current Channel id is:<br>" + channel_ID + "</p>");
              client.println("<form method='post' action='/submit'>");
              client.println("<input class='input' type='text' name='channel_ID' placeholder='New channel ID'><p></p>");
              client.println("<input class='button' type='submit' value='Send'>");
              client.println("</form>");
            }

            if (sourceState == "Coindesk - BTC/USD")
              client.println("<p>Coindesk only updates once per minute</p>");

            if (sourceState == "Binance - BTC/USDT" || sourceState == "Coindesk - BTC/USD")
            {
              if (sat == 0) // btc
              {
                client.println("<button class=\"button button2 button3\">BTC/USD</button><a href=\"/sat\"><button class=\"button button3\">USD/sat</button></a></p>");
              }
              else // sat
              {
                client.println("<a href=\"/btc\"><button class=\"button button3\">BTC/USD</button></a><button class=\"button button3 button2\">USD/sat</button></p>");
              }
            }
            if (mode == 1)
              client.println("<br><a href=\"/healoff\"><button class=\"button button2 button3\">Heal</button></a>");
            else
              client.println("<br><a href=\"/healon\"><button class=\"button button3\">Heal</button></a>");

            client.println("<a href=\"/test\"><button class=\"button button2 button3\">Test</button></a>");

            client.println("<br><br><br><br><br><br><p><a onclick=\"myFunction()\"><button class=\"button button4\">RESET WIFI</button></a></p>");
            client.println("<p id=\"reset\"></p><script>function myFunction() { var str = \"CONFIRM RESET\"; var result = str.link(\"/resetwifi\"); document.getElementById(\"reset\").innerHTML = result;}</script>");
            client.println("<p>(This will erase saved wifi credentials)</p>");

            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          }
          else
          { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        }
        else if (c != '\r')
        {                   // if you got anything else but a carriage return character,
          currentLine += c; // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
  }
}

bool getBinanceBTC()
{
  HTTPClient http;
  http.begin("https://api.binance.com/api/v3/ticker/price?symbol=BTCUSDT");
  int httpCode = http.GET();
  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 200)
  {
    String payload = http.getString();
    DynamicJsonDocument doc(256);
    const char *json = payload.c_str();
    deserializeJson(doc, json);
    value = doc["price"];

    if (sat == 1)
      value = 100000000 / value;
    http.end();
    return 0;
  }
  else
  {
    http.end();
    return 1;
  }
}

bool getBinanceETH()
{
  HTTPClient http;
  http.begin("https://api.binance.com/api/v3/ticker/price?symbol=ETHUSDT");
  int httpCode = http.GET();
  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 200)
  {
    String payload = http.getString();
    DynamicJsonDocument doc(256);
    const char *json = payload.c_str();
    deserializeJson(doc, json);
    value = doc["price"];
    http.end();
    return 0;
  }
  else
  {
    http.end();
    return 1;
  }
}

bool getCoindeskBTC()
{
  HTTPClient http;
  http.begin("https://api.coindesk.com/v1/bpi/currentprice.json");
  int httpCode = http.GET();

  if (httpCode == 200)
  {
    String payload = http.getString();
    const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(3) + JSON_OBJECT_SIZE(1) + 2 * JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 2 * JSON_OBJECT_SIZE(6) + 2 * JSON_OBJECT_SIZE(7) + 590;
    DynamicJsonDocument doc(capacity);
    const char *json = payload.c_str();
    deserializeJson(doc, json);

    JsonObject bpi = doc["bpi"];
    JsonObject bpi_USD = bpi["USD"];
    value = bpi_USD["rate_float"];

    if (sat == 1)
      value = 100000000 / value;

    http.end();
    return 0;
  }
  else
  {
    http.end();
    return 1;
  }
}

bool getBlockHeight()
{
  HTTPClient http;
  http.begin("https://mempool.space/api/blocks/tip/height");
  int httpCode = http.GET();
  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode == 200)
  {
    String payload = http.getString();
    value = payload.toInt();
    http.end();
    return 0;
  }
  else
  {
    http.end();
    return 1;
  }
}

bool getYoutubeSubs()
{
  WiFiClientSecure client;
  client.setCACert(yt_root_ca);
  YoutubeApi api(youtubeApiKey, client);

  if (api.getChannelStatistics(channel_ID))
  {
    value = api.channelStats.subscriberCount;
    client.stop();
    return 0;
  }
  else
  {
    client.stop();
    return 1;
  }
}

void LEDControl(byte LEDSetState)
{
  while (LEDSetState != LED)
  {
    if (LEDSetState > LED)
      LED++;
    else
      LED--;

    ledcWrite(0, LED);
    delay(5);
  }
}