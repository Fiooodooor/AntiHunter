#include "hardware.h"
#include "scanner.h"
#include "network.h"
#include <SPI.h>
#include <SD.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

extern Preferences prefs;
extern int cfgBeeps, cfgGapMs;
extern ScanMode currentScanMode;
extern std::vector<uint8_t> CHANNELS;

// GPS
TinyGPSPlus gps;
HardwareSerial GPS(2);
bool sdAvailable = false;
String lastGPSData = "No GPS data";
float gpsLat = 0.0, gpsLon = 0.0;
bool gpsValid = false;

// getDiagnostics vars
extern volatile bool scanning;
extern volatile int totalHits;
extern volatile uint32_t framesSeen;
extern volatile uint32_t bleFramesSeen;
extern volatile bool trackerMode;
extern std::set<String> uniqueMacs;
extern uint32_t lastScanSecs;
extern bool lastScanForever;
extern String macFmt6(const uint8_t *m);
extern size_t getTargetCount();
extern void getTrackerStatus(uint8_t mac[6], int8_t &rssi, uint32_t &lastSeen, uint32_t &packets);

// Buzzer control
#if BUZZER_IS_PASSIVE
static bool buzzerInit = false;

static void buzzerInitIfNeeded(uint32_t f)
{
    if (!buzzerInit)
    {
        ledcAttach(BUZZER_PIN, f, 10);
        buzzerInit = true;
    }
    else
    {
        ledcDetach(BUZZER_PIN);
        ledcAttach(BUZZER_PIN, f, 10);
    }
}

static void buzzerTone(uint32_t f)
{
    buzzerInitIfNeeded(f);
    ledcWrite(BUZZER_PIN, 512); // 50% duty cycle
}

static void buzzerOff()
{
    if (buzzerInit)
        ledcWrite(BUZZER_PIN, 0);
}

#else
static void buzzerTone(uint32_t)
{
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);
}

static void buzzerOff()
{
    digitalWrite(BUZZER_PIN, LOW);
}
#endif

void beepOnce(uint32_t freq, uint32_t ms)
{
    buzzerTone(freq);
    delay(ms);
    buzzerOff();
}

void beepPattern(int count, int gap_ms)
{
    if (count < 1)
        return;
    for (int i = 0; i < count; i++)
    {
        beepOnce();
        if (i != count - 1)
            delay(gap_ms);
    }
}

void initializeHardware()
{
    Serial.println("Loading preferences...");
    prefs.begin("ouispy", false);

    cfgBeeps = prefs.getInt("beeps", 2);
    cfgGapMs = prefs.getInt("gap", 80);

    Serial.printf("Hardware initialized: beeps=%d, gap=%dms\n", cfgBeeps, cfgGapMs);
}

void saveConfiguration()
{
    prefs.putInt("beeps", cfgBeeps);
    prefs.putInt("gap", cfgGapMs);
}

int getBeepsPerHit()
{
    return cfgBeeps;
}

int getGapMs()
{
    return cfgGapMs;
}

String getDiagnostics() {
    String s;
    String modeStr = (currentScanMode == SCAN_WIFI) ? "WiFi" : 
                     (currentScanMode == SCAN_BLE) ? "BLE" : "WiFi+BLE";

    s += "Scan Mode: " + modeStr + "\n";
    s += String("Scanning: ") + (scanning ? "yes" : "no") + "\n";
    s += "WiFi Frames seen: " + String((unsigned)framesSeen) + "\n";
    s += "BLE Frames seen: " + String((unsigned)bleFramesSeen) + "\n";
    s += "Total hits: " + String(totalHits) + "\n";
    s += "Country: " + String(COUNTRY) + "\n";
    s += "Current channel: " + String(WiFi.channel()) + "\n";
    s += "AP IP: " + WiFi.softAPIP().toString() + "\n";
    s += "Unique devices: " + String((int)uniqueMacs.size()) + "\n";
    s += "Targets: " + String(getTargetCount()) + "\n";

    // SD Card Status
    s += "SD Card: " + String(sdAvailable ? "Available" : "Not available") + "\n";
    if (sdAvailable) {
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        uint8_t cardType = SD.cardType();
        String cardTypeStr = (cardType == CARD_MMC) ? "MMC" :
                             (cardType == CARD_SD) ? "SDSC" :
                             (cardType == CARD_SDHC) ? "SDHC" : "UNKNOWN";
        s += "SD Card Type: " + cardTypeStr + "\n";
        s += "SD Card Size: " + String(cardSize) + "MB\n";

        File root = SD.open("/");
        if (root) {
            s += "SD Card Files:\n";
            while (true) {
                File entry = root.openNextFile();
                if (!entry)
                    break;

                String fileName = String(entry.name());
                if (fileName.startsWith(".")) {
                    entry.close();
                    continue;
                }

                s += "  " + fileName + " (" + String(entry.size()) + " bytes)\n";
                entry.close();
            }
            root.close();
        } else {
            s += "Failed to read SD card files.\n";
        }
    }

    /// GPS Status
    s += "GPS Status: ";
    if (gpsValid) {
        s += "Valid fix at " + String(gpsLat, 6) + ", " + String(gpsLon, 6) + "\n";
    } else {
        s += "Searching for satellites\n";
    }

    if (trackerMode) {
        uint8_t trackerMac[6];
        int8_t trackerRssi;
        uint32_t trackerLastSeen, trackerPackets;
        getTrackerStatus(trackerMac, trackerRssi, trackerLastSeen, trackerPackets);

        s += "Tracker: target=" + macFmt6(trackerMac) + " lastRSSI=" + String((int)trackerRssi) + "dBm";
        s += "  lastSeen(ms ago)=" + String((unsigned)(millis() - trackerLastSeen));
        s += " pkts=" + String((unsigned)trackerPackets) + "\n";
    }

    s += "Last scan secs: " + String((unsigned)lastScanSecs) + (lastScanForever ? " (forever)" : "") + "\n";

    float temp_c = temperatureRead();
    float temp_f = (temp_c * 9.0 / 5.0) + 32.0;
    s += "ESP32 Temp: " + String(temp_c, 1) + "°C / " + String(temp_f, 1) + "°F\n";

    s += "Beeps/Hit: " + String(cfgBeeps) + "  Gap(ms): " + String(cfgGapMs) + "\n";

    s += "WiFi Channels: ";
    for (auto c : CHANNELS) {
        s += String((int)c) + " ";
    }
    s += "\n";

    return s;
}

void initializeSD()
{
    Serial.println("Initializing SD card...");
    Serial.printf("[SD] Pins SCK=%d MISO=%d MOSI=%d CS=%d\n", SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    // Reset SPI bus
    SPI.end();
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
    delay(100); // Allow SPI bus to stabilize

    // Try multiple frequencies
    const uint32_t tryFreqs[] = {1000000, 4000000, 8000000, 10000000};
    for (uint32_t f : tryFreqs)
    {
        Serial.printf("[SD] Trying frequency: %lu Hz\n", f);
        if (SD.begin(SD_CS_PIN, SPI, f))
        {
            Serial.println("SD card initialized successfully");
            sdAvailable = true;

            // Print SD card details
            uint8_t cardType = SD.cardType();
            Serial.print("SD Card Type: ");
            if (cardType == CARD_MMC)
            {
                Serial.println("MMC");
            }
            else if (cardType == CARD_SD)
            {
                Serial.println("SDSC");
            }
            else if (cardType == CARD_SDHC)
            {
                Serial.println("SDHC");
            }
            else
            {
                Serial.println("UNKNOWN");
            }

            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.printf("SD Card Size: %lluMB\n", cardSize);
            return;
        }
        delay(100);
    }
    Serial.println("SD card initialization failed");
}

void initializeGPS()
{
    Serial.println("Initializing GPS...");
    
    GPS.setRxBufferSize(2048);
    GPS.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    delay(120); // enough time to let it settle
    
    if (GPS.available()) {
        Serial.println("[GPS] GPS module responding");
    } else {
        Serial.println("[GPS] No initial response - GPS may need more time for cold start");
        Serial.println("[GPS] Allow 5-15 minutes outdoors for first fix");
    }
    
    Serial.printf("[GPS] UART initialized on pins RX:%d TX:%d\n", GPS_RX_PIN, GPS_TX_PIN);
}

void logToSD(const String &data)
{
    if (!sdAvailable)
        return;

    File logFile = SD.open("/antihunter.log", FILE_APPEND);
    if (logFile)
    {
        logFile.print("[");
        logFile.print(millis());
        logFile.print("] ");
        logFile.println(data);
        logFile.close();
    }
}

String getGPSData()
{
    return lastGPSData;
}

void updateGPSLocation() {
    static unsigned long lastDataTime = 0;

    while (GPS.available() > 0) {
        char c = GPS.read();
        if (gps.encode(c)) {
            lastDataTime = millis();

            if (gps.location.isValid()) {
                gpsLat      = gps.location.lat();
                gpsLon      = gps.location.lng();
                gpsValid    = true;
                lastGPSData = "Lat: " + String(gpsLat, 6)
                            + ", Lon: " + String(gpsLon, 6)
                            + " (" + String((millis() - lastDataTime) / 1000) 
                            + "s ago)";
            } else {
                gpsValid    = false;
                lastGPSData = "No valid GPS fix (" 
                            + String((millis() - lastDataTime) / 1000)
                            + "s ago)";
            }
        }
    }

    if (lastDataTime > 0 && millis() - lastDataTime > 30000) {
        gpsValid    = false;
        lastGPSData = "No data for " 
                    + String((millis() - lastDataTime) / 1000)
                    + "s";
    }
}

void testGPSPins() {
    Serial.println("\n=== GPS Connection Test ===");
    Serial.printf("GPS_RX_PIN: %d, GPS_TX_PIN: %d\n", GPS_RX_PIN, GPS_TX_PIN);
    
    uint32_t baudRates[] = {4800, 9600, 19200, 38400, 57600, 115200};
    
    for (int i = 0; i < 6; i++) {
        Serial.printf("Testing baud rate: %lu\n", baudRates[i]);
        GPS.end();
        delay(200);
        GPS.begin(baudRates[i], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
        delay(1000);
        
        unsigned long start = millis();
        String receivedData = "";
        int bytesRead = 0;
        
        while (millis() - start < 5000) {  // Test for 5 seconds
            if (GPS.available()) {
                char c = GPS.read();
                receivedData += c;
                bytesRead++;
                Serial.print(c);
            }
        }
        
        Serial.printf("\nBytes received at %lu baud: %d\n", baudRates[i], bytesRead);
        
        // Check for actual NMEA sentences, not just random bytes
        if (receivedData.indexOf("$GP") >= 0 || receivedData.indexOf("$GN") >= 0) {
            Serial.printf("REAL GPS DATA FOUND at %lu baud!\n", baudRates[i]);
            Serial.println("Sample data: " + receivedData.substring(0, 100));
            return;
        } else if (bytesRead > 0) {
            Serial.printf("Got %d bytes but no NMEA sentences at %lu baud\n", baudRates[i], bytesRead);
        } else {
            Serial.printf("No data at %lu baud\n", baudRates[i]);
        }
        Serial.println("---");
    }
    
    Serial.println("NO VALID GPS DATA FOUND AT ANY BAUD RATE - CHECK WIRING");
}