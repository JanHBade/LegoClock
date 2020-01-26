/*
    Name:       LegoClock.ino
    Created:	14.12.2019 15:49:00
    Author:     BASTET\jhb
*/


#include "HTTPUpdateServer.h"
#include <PageStream.h>
#include <PageBuilder.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#define NO_ADAFRUIT_SSD1306_COLOR_COMPATIBILITY
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <WS2812FX.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <AutoConnect.h>

#include "HTTPUpdateServer.h"
#include "ESP32_RMT_Driver.h"

#define NTPSERVERCNT 4
const char* ntpServers[] = { "ntp.beckhoff.com","speedport.ip","de.pool.ntp.org","fritz.box" };
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

#define LEDPIN 2
#define NUMPIXELS 7
#define START_MODE FX_MODE_BREATH
WS2812FX ws2812fx = WS2812FX(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig config;
#define ROOT_PAGE "/"
#define AUX_SETTING "/led"
#define AUX_SETTING_SAVE "/led_save"

ACText(start, "start");
AutoConnectAux RootPage(ROOT_PAGE, "Startseite", false, {
  start,
    });

AutoConnectSelect modus("Modus", { }, "LED Modus");
ACInput(farbe, "", "Farbe (Hex) RGB", "^[a-fA-F0-9]{6}$");
ACCheckbox(autofarbe, "AutoFarbe", "Auto. Farbwechsel");
ACInput(speed, "200", "Geschwindigkeit", "^[0-9]{1,}$");
ACInput(brightness, "100", "Helligkeit", "^[0-9]{1,}$");
ACSubmit(save, "&#220;bernehmen", AUX_SETTING_SAVE);
ACSubmit(discard, "Abbrechen", "/_ac");
AutoConnectAux LedSettings(AUX_SETTING, "LED Einstellungen", true, {
  modus,
  farbe,
  autofarbe,
  speed,
  brightness,
  save,
  discard
    });

ACText(caption2, "Gespeichert");
ACText(parameters);
AutoConnectAux LedSettingsSave(AUX_SETTING_SAVE, "LED gespeichert", false, {
  caption2,
  parameters
    });

HTTPUpdateServer httpUpdater;
AutoConnectAux  update("/update", "Update");

#define HOSTNAME "LegoClock"

Adafruit_BME280 bme; // I2C

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float Values[3];
const char* Template[3] = {"%.1f GrC"," %.1f %%","%.0f hPa"};
int actValue = 0;
int LastUpdate = 0;
bool Datum = false;
struct tm timeinfo;
int Hue = 0;

enum eValues
{
	Temperatur,
	Feuchtigkeit,
	Luftdruck
};

void scan()
{
    Serial.println("Scanning I2C Addresses Channel 1");
    uint8_t cnt = 0;
    for (uint8_t i = 0; i<128; i++) {
        Wire.beginTransmission(i);
        uint8_t ec = Wire.endTransmission(true);
        if (ec == 0) {
            if (i<16)Serial.print('0');
            Serial.print(i, HEX);
            cnt++;
        }
        else Serial.print("..");
        Serial.print(' ');
        if ((i & 0x0f) == 0x0f)Serial.println();
    }
    Serial.print("Scan Completed, ");
    Serial.print(cnt);
    Serial.println(" I2C Devices found.");
}

int x2i(String s)
{
    int x = 0;
    for (int i=0;i<s.length();i++)
    {
        char c = s.charAt(i);
        if (c >= '0' && c <= '9')
        {
            x *= 16;
            x += c - '0';
        }
        else if (c >= 'A' && c <= 'F')
        {
            x *= 16;
            x += (c - 'A') + 10;
        }
        else if (c >= 'a' && c <= 'f')
        {
            x *= 16;
            x += (c - 'a') + 10;
        }
    }
    return x;
}

uint8_t d2i(String s)
{
    uint8_t x = 0;
    for (int i = 0;i < s.length();i++)
    {
        char c = s.charAt(i);
        if (c >= '0' && c <= '9')
        {
            x *= 10;
            x += c - '0';
        }
    }
    return x;
}

// Custom show functions which will use the RMT hardware to drive the LEDs.
// Need a separate function for each ws2812fx instance.
void myCustomShow(void)
{
    uint8_t* pixels = ws2812fx.getPixels();
    // numBytes is one more then the size of the ws2812fx's *pixels array.
    // the extra byte is used by the driver to insert the LED reset pulse at the end.
    uint16_t numBytes = ws2812fx.getNumBytes() + 1;
    rmt_write_sample(RMT_CHANNEL_0, pixels, numBytes, false); // channel 0
}

uint32_t HSVtoRGB(int hue, int sat, int val)
{
    // hue: 0-359, sat: 0-255, val (lightness): 0-255
    int r, g, b, base;
    if (sat == 0)
    {                     // Achromatic color (gray).
        r = val;
        g = val;
        b = val;
    }
    else
    {
        base = ((255 - sat) * val) >> 8;
        switch (hue / 60)
        {
        case 0:
            r = val;
            g = (((val - base) * hue) / 60) + base;
            b = base;
            break;
        case 1:
            r = (((val - base) * (60 - (hue % 60))) / 60) + base;
            g = val;
            b = base;
            break;
        case 2:
            r = base;
            g = val;
            b = (((val - base) * (hue % 60)) / 60) + base;
            break;
        case 3:
            r = base;
            g = (((val - base) * (60 - (hue % 60))) / 60) + base;
            b = val;
            break;
        case 4:
            r = (((val - base) * (hue % 60)) / 60) + base;
            g = base;
            b = val;
            break;
        case 5:
            r = val;
            g = base;
            b = (((val - base) * (60 - (hue % 60))) / 60) + base;
            break;
        }
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

void setup()
{
    Serial.begin(115200);

    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
                                                // init done

    // Show image buffer on the display hardware.
    // Since the buffer is intialized with an Adafruit splashscreen
    // internally, this will display the splashscreen.
    display.display();
        
    config.hostName = HOSTNAME;
    config.retainPortal = true;
    config.ticker = true;
    Portal.config(config);

    uint8_t num_modes = ws2812fx.getModeCount();
    for (uint8_t i = 0; i < num_modes; i++) {
         modus.add(ws2812fx.getModeName(i));
    }

    httpUpdater.setup(&Server, "", "");
        
    Portal.join({ RootPage,LedSettings,LedSettingsSave,update });
    
    if (Portal.begin())
    {        
        /*// We start by connecting to a WiFi network
        Serial.print("Connecting to ");
        Serial.println(WLAN_NAME);
        WiFi.persistent(false);	//verhindert Flash Schreiben	
        WiFi.mode(WIFI_STA);	//Nur Client Mode
        WiFi.setHostname(HOSTNAME);	//damit die Fritz.Box und so weiter den namen anzeigen
        WiFi.begin(WLAN_NAME, WLAN_PASSWORT);	//Wlan Start

        while (WiFi.status() != WL_CONNECTED) {
            //while (1) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("");*/

        Serial.println("WiFi connected");
        Serial.println("MAC: ");
        Serial.println(WiFi.macAddress());
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());

        for (int i = 0;i < NTPSERVERCNT;i++)
        {
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServers[i]);

            if (!getLocalTime(&timeinfo))
            {
                Serial.println("Fehler:");
                Serial.println(ntpServers[i]);
            }
            else
            {
                Serial.println("Gefunden:");
                Serial.println(ntpServers[i]);
                i = NTPSERVERCNT;   //Schleife verlassen, gültigen NTP Server gefunden
            }
        }

        Wire.begin();
        scan();

        unsigned status;
        // default settings
        status = bme.begin();
        // You can also pass in a Wire library object like &Wire2
        // status = bme.begin(0x76, &Wire2)
        if (!status)
        {
            Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
            Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(), 16);
            Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
            Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
            Serial.print("        ID of 0x60 represents a BME 280.\n");
            Serial.print("        ID of 0x61 represents a BME 680.\n");
            while (1) delay(10);
        }

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println(HOSTNAME);
        display.println(WiFi.localIP());
        display.display();
        delay(500);

        ws2812fx.init();
        ws2812fx.setBrightness(d2i(brightness.value));
        ws2812fx.setSpeed(d2i(speed.value));

        rmt_tx_int(RMT_CHANNEL_0, ws2812fx.getPin()); // assign ws2812fx1 to RMT channel 0

        ws2812fx.setCustomShow(myCustomShow); // set the custom show function to forgo the NeoPixel
        //ws2812fx2.setCustomShow(myCustomShow2); // bit-bang method and instead use the RMT hardware

        ws2812fx.setMode(START_MODE);
        modus.select(ws2812fx.getModeName(START_MODE));
        farbe.value = String(ws2812fx.getColor(), 16);
        farbe.value.toUpperCase();
        ws2812fx.start();
    }
}

void loop()
{
    Values[Temperatur] = bme.readTemperature();
    Values[Feuchtigkeit] = bme.readHumidity();
    Values[Luftdruck] = bme.readPressure()/100.0;

    start.value = "Temperatur: " + String(Values[Temperatur],2);
    start.value += "GrC Feuchtigkeit: " + String(Values[Feuchtigkeit], 2);
    start.value += "% Luftdruck: " + String(Values[Luftdruck], 1) + "hPa";

    
    
    if (ws2812fx.getMode() != (modus.selected - 1))
    {
        Serial.println(ws2812fx.getModeName(modus.selected - 1));        
        ws2812fx.setMode(modus.selected-1);
    }
    if (ws2812fx.getBrightness() != d2i(brightness.value))
    {
        Serial.println(d2i(brightness.value));        
        ws2812fx.setBrightness(d2i(brightness.value));
    }
    if (ws2812fx.getSpeed() != d2i(speed.value))
    {
        Serial.println(d2i(speed.value));
        ws2812fx.setSpeed(d2i(speed.value));
    }
    if (!autofarbe.checked)
    {
        if (ws2812fx.getColor() != x2i(farbe.value))
        {
            farbe.value.toUpperCase();
            Serial.println(farbe.value);
            ws2812fx.setColor(x2i(farbe.value));
        }
    }
    
    
	if (!getLocalTime(&timeinfo))
    {
		Serial.println("Failed to obtain time");
        start.value += "Failed to obtain time";
	}
	else
	{		
        char timeStringBuff[50];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %d %B %Y %H:%M:%S", &timeinfo);
        Serial.println(timeStringBuff);
        start.value += " Zeit: " + String(timeStringBuff);
	}

    display.clearDisplay();
	display.setTextSize(1);
	display.setCursor(0, 0);

    display.drawRoundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4, 1);
	    
	/*int i = 0;
	for (i = 0x80; i < 0x7f; i++)
	{
		display.setTextWrap(true);
		display.print((char)i);		
	}*/

	display.setFont(&FreeSans12pt7b);
	display.setCursor(14 , 19);
	display.printf(Template[actValue],Values[actValue]);
	     
	display.setFont(&FreeSansBold24pt7b);
    display.setCursor(4, 55);
    if (!Datum)
    {		
        display.println(&timeinfo,"%H:%M");
    }
    else
    {
        display.println(&timeinfo, "%d.%m");
    }
    display.drawLine(3, 58, timeinfo.tm_sec, 58, 1);
    display.drawLine(3, 59, timeinfo.tm_sec, 59, 1);
    display.drawLine(3, 60, timeinfo.tm_sec, 60, 1);    

    display.display();

	if (0 == timeinfo.tm_sec % 5)
	{
        if (autofarbe.checked)
        {
            ws2812fx.setColor(HSVtoRGB(Hue, 255, 255));
            Hue++;
            if (360 == Hue)
                Hue = 0;
        }

		Datum = !Datum;
		actValue++;
		if (3 == actValue)
			actValue = 0;
	}

    for (int i = 0;i < 700;i++)
    {
        Portal.handleClient();
        ws2812fx.service();
        delay(1);
    }
}
