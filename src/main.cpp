#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <EEPROM.h>

#define EEPROM_SIZE 6 //0=roll, 1=nastavenaprodleva, 2=sourceState, 3=sat, 4=resetwifi, 5=mode(heal)

WiFiServer server(80);

int latchPin = 26;
int clockPin = 25;
int dataPin = 27;
int hvPin = 33;
int oldValue;
int errorCount = 0; //if connection is lost, shows 000000-999999

int timeoutTime = 2000;
bool roll, sat = 0;
byte mode = 0; //0=normal, 1=heal;
String sourceState;

String header;

uint32_t millisTime = 40000, lastHTTP = 0, lastRotate = 0, lastClient = 0;
int waitTimeRotate = 180000, waitTimeHTTP, setWaitTimeHTTP;
double value;

int digits[10] = {0b0000000001,  //0
                  0b1000000000,  //1
                  0b0100000000,  //2
                  0b0010000000,  //3
                  0b0001000000,  //4
                  0b0000100000,  //5
                  0b0000010000,  //6
                  0b0000001000,  //7
                  0b0000000100,  //8
                  0b0000000010}; //9

void nixie(int, bool); //number, rotate
void rotate(int, int); //count, delay in ms
void settingsPage(void);

void setup()
{
  int intip;

  pinMode(27, OUTPUT); //ser data
  pinMode(26, OUTPUT); //latch
  pinMode(25, OUTPUT); //clk
  pinMode(33, OUTPUT); //HV enable, active LOW
  EEPROM.begin(EEPROM_SIZE);
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  digitalWrite(hvPin, LOW);

  Serial.begin(115200);
  rotate(10, 10);

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
  default:
    sourceState = "Binance - BTC/USDT";
  }

  sat = EEPROM.read(3);
  
  mode = EEPROM.read(5);
  if (mode<0 || mode >2)
  mode = 0;

  WiFiManager wm;

  if (EEPROM.read(4) == 1)
  {
    wm.resetSettings(); //wipe credentials
    EEPROM.write(4, 0);
    EEPROM.commit();
    nixie(654321, 0);
  }

  wm.setClass("invert");
  wm.setHostname("Nixie");
  wm.setConfigPortalTimeout(120);
  digitalWrite(hvPin, HIGH);
  bool res;
  res = wm.autoConnect("NixieTicker");

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

  if (mode == 2) //test
  {
    rotate(1, 3000);
    mode = 0;
  }
  else if (mode == 1) //heal
  {
    rotate(1, 1000);
  }
  else //normal
  {
    if (millisTime - lastRotate > waitTimeRotate)
    {
      rotate(10, 40);
      nixie(value, roll);
      lastRotate = millisTime;
    }

    if (millisTime - lastHTTP > waitTimeHTTP)
    {
      lastHTTP = millisTime;
      Serial.println("HTTP begin");
      HTTPClient http;
      if (sourceState == "Binance - BTC/USDT")
        http.begin("https://api.binance.com/api/v3/ticker/price?symbol=BTCUSDT");
      else if (sourceState == "Coindesk - BTC/USD")
        http.begin("https://api.coindesk.com/v1/bpi/currentprice.json");
      else if (sourceState == "Binance - ETH/USDT")
        http.begin("https://api.binance.com/api/v3/ticker/price?symbol=ETHUSDT");

      int httpCode = http.GET();

      if (httpCode == 200)
      {
        Serial.println(httpCode);

        const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(3) + JSON_OBJECT_SIZE(1) + 2 * JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 2 * JSON_OBJECT_SIZE(6) + 2 * JSON_OBJECT_SIZE(7) + 590;
        DynamicJsonDocument doc(capacity);
        String payload = http.getString();
        const char *json = payload.c_str();

        deserializeJson(doc, json);
        if (sourceState == "Coindesk - BTC/USD")
        {
          JsonObject bpi = doc["bpi"];
          JsonObject bpi_USD = bpi["USD"];
          value = bpi_USD["rate_float"];
        }
        else if (sourceState == "Binance - BTC/USDT")
        {
          value = doc["price"];
        }
        else if (sourceState == "Binance - ETH/USDT")
        {
          value = doc["price"];
        }

        if (sat == 1 && sourceState != "Binance - ETH/USDT")
          value = 100000000 / value;

        Serial.println(value, 0);
        nixie(value, roll);
        waitTimeHTTP = setWaitTimeHTTP;
        errorCount = 0;
      }
      else
      {
        Serial.println("Error on HTTP request");
        Serial.print("Code:");
        Serial.println(httpCode);
        if (errorCount > 999999)
          ESP.restart();
        nixie(errorCount, 0);
        errorCount = errorCount + 111111;
        waitTimeHTTP = 10000;
      }

      http.end();
    }
    millisTime = millis();
  }
}

void nixie(int number, bool rolling)
{
  int stot, dest, tis, sto, des, jed;
  byte r[8];             //shift registers
  if (number >= 1000000) //if number is bigger than 1 000 000, shows only last 6 digits. Better solution would be shift number from left to right
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

void settingsPage()
{
  WiFiClient client = server.available();

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
              lastHTTP = lastHTTP - 60000;
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

            waitTimeHTTP = setWaitTimeHTTP;

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");

            client.println("<style>html { font-family: 'Helvetica'; display: inline-block; margin: 0px auto; text-align: center; background-color:#000000; color:#FFFFFF}");
            client.println(".button { background-color: #202020; width: 320px; border: 2px solid #666666; border-radius: 20px; color: white; padding: 12px; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {border: 2px solid #FF9900;}");
            client.println(".button3 {width: 160px;}");
            client.println(".button4 {border: 2px solid #FF0000;}");

            client.println(".dropdown {position: relative; display: inline-block;}");
            client.println(".dropdown-content {display: none; font-size: 20px; background-color: #202020; border-radius: 20px; border: 2px solid #666666; z-index: 1;}");
            client.println(".dropdown-content a {color: #FFFFFF; padding: 12px 16px; text-decoration: none; display: block; }");
            client.println(".dropdown-content a:hover {background-color: #000000; border-radius: 20px;}");
            client.println(".dropdown:hover .dropdown-content {display: block;}");
            client.println("h1{font-family: 'Courier New', monospace; font-size: 45px}");

            client.println("</style></head>");

            client.println("<body><h1>NixieTicker</h1>");

            if (roll == 0)
              client.println("<p><a href=\"/rollovat\"><button class=\"button\">Roll : OFF</button></a></p>");
            else
              client.println("<p><a href=\"/nerollovat\"><button class=\"button button2\">Roll : ON</button></a></p>");

            client.println("<p><div class=\"dropdown\">  <button class=\"button button2\">Refresh Rate: ");
            client.print(setWaitTimeHTTP / 1000);
            client.println("s</button> <div class=\"dropdown-content\">   <a href=\"/prodleva/2s\">2 sec</a> <a href=\"/prodleva/5s\">5 sec</a>   <a href=\"/prodleva/10s\">10 sec</a>   <a href=\"/prodleva/30s\">30 sec</a>  <a href=\"/prodleva/1min\">1 min</a> </div></div></p>");

            client.println("<p><div class=\"dropdown\">  <button class=\"button button2\">Price Source</button> <div class=\"dropdown-content\">   <a href=\"/coindesk-btcusd\">Coindesk - BTC/USD</a> <a href=\"/binance-btcusdt\">Binance - BTC/USDT (fast)</a><a href=\"/binance-ethusdt\">Binance - ETH/USDT (fast)</a> </div></div></p>");
            client.println("<p>Current price soucre: " + sourceState + "</p>");
            if (sourceState == "Coindesk - BTC/USD")
              client.println("<p>Coindesk only updates once per minute</p>");

            if (sourceState != "Binance - ETH/USDT")
            {
              if (sat == 0) //btc
              {
                client.println("<button class=\"button button2 button3\">BTC/USD</button><a href=\"/sat\"><button class=\"button button3\">USD/sat</button></a></p>");
              }
              else //sat
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
            client.println("<p id=\"demo\"></p><script>function myFunction() { var str = \"CONFIRM RESET\"; var result = str.link(\"/resetwifi\"); document.getElementById(\"demo\").innerHTML = result;}</script>");
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