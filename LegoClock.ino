

#include <Wire.h>
#include <WiFi.h>
#include <time.h>

#define NO_ADAFRUIT_SSD1306_COLOR_COMPATIBILITY

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include <WS2812FX.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define WLAN_NAME "Beckhoff_Guest"
#define WLAN_PASSWORT "Automation1980"
#define HOSTNAME "LegoClock"

#define NTPSERVERCNT 4
const char* ntpServers[] = { "ntp.beckhoff.com","speedport.ip","de.pool.ntp.org","fritz.box" };

#define LEDPIN 2
#define NUMPIXELS 7
#define START_MODE FX_MODE_BREATH
WS2812FX ws2812fx = WS2812FX(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

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
    
    // We start by connecting to a WiFi network
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
    Serial.println("");

    Serial.println("WiFi connected");
    Serial.println("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    for (int i = 0;i < NTPSERVERCNT;i++)
    {        
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", ntpServers[i]);

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
    ws2812fx.setMode(START_MODE);    
    ws2812fx.start();
}

void loop()
{
    Values[Temperatur] = bme.readTemperature();
    Values[Feuchtigkeit] = bme.readHumidity();
    Values[Luftdruck] = bme.readPressure()/100.0;    
    
	if (!getLocalTime(&timeinfo))
    {
		Serial.println("Failed to obtain time");
	}
	else
	{		
        char timeStringBuff[50];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %d %B %Y %H:%M:%S", &timeinfo);
        Serial.println(timeStringBuff);
	}

    display.clearDisplay();
	display.setTextSize(1);
	display.setCursor(0, 0);

    if (timeinfo.tm_hour > 23)
    {
        if (Datum)
            display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 1);
        else
            display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);        

        display.display();
        if (0 == timeinfo.tm_min % 5)
        {
            Serial.println("Anti Einbrenn OLED Modus");
            Datum = !Datum;
        }
    }
    else
    {
        display.drawRoundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4, 1);

        display.setFont(&FreeSans12pt7b);
        display.setCursor(14, 19);
        display.printf(Template[actValue], Values[actValue]);

        display.setFont(&FreeSansBold24pt7b);
        display.setCursor(4, 55);
        if (!Datum)
        {
            display.println(&timeinfo, "%H:%M");
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
            ws2812fx.setColor(HSVtoRGB(Hue, 255, 255));
            Hue++;
            if (360 == Hue)
                Hue = 0;

            Datum = !Datum;
            actValue++;
            if (3 == actValue)
                actValue = 0;
        }
    }

    for (int i = 0;i < 700;i++)
    {        
        ws2812fx.service();
        delay(1);
    }
}
