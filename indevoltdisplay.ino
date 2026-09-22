#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

#define RGB_PIN 42      // vaak GPIO42 op ESP32-S3
#define NUMPIXELS 1

int pulseBrightness = 0;
int pulseDirection = 1;
int currentBatteryState = 1000;

Adafruit_NeoPixel rgb(
    NUMPIXELS,
    RGB_PIN,
    NEO_GRB + NEO_KHZ800);

// ======================================================
// CONFIG
// ======================================================

#define TFT_BL 45

#define TFT_BL 45

const char* ssid = "YOUR-SSID";
const char* password = "YOUR-PASSWORD";

const String API_URL =
    "http://" + INDEVOLT_IP +
    ":8080/rpc/Indevolt.GetData?config={\"t\":[6001,6002,2101,2108,6004,6005]}";

// ======================================================
// GLOBALS
// ======================================================

TFT_eSPI tft = TFT_eSPI();

static lv_display_t *display;
static lv_color_t draw_buf[320 * 60];

lv_obj_t *lblTitle;
lv_obj_t *lblWifiIcon;

lv_obj_t *lblSOC;
lv_obj_t *lblState;
lv_obj_t *lblTotal;
lv_obj_t *lblToday;
lv_obj_t *arcSOC;

// ======================================================
// DISPLAY FLUSH
// ======================================================

void my_flush_cb(
    lv_display_t *disp,
    const lv_area_t *area,
    uint8_t *px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();

    tft.setAddrWindow(
        area->x1,
        area->y1,
        w,
        h);

    tft.pushColors(
        (uint16_t *)px_map,
        w * h);

    tft.endWrite();

    lv_display_flush_ready(disp);
}

void updateStatusLed(int batteryState)
{
    switch (batteryState)
    {
        case 1000: // Standby
        {
            rgb.setPixelColor(
                0,
                rgb.Color(0, 0, 255));
            break;
        }

        case 1001: // Charging
        {
            pulseBrightness += pulseDirection * 4;

            if (pulseBrightness >= 255)
            {
                pulseBrightness = 255;
                pulseDirection = -1;
            }

            if (pulseBrightness <= 10)
            {
                pulseBrightness = 10;
                pulseDirection = 1;
            }

            rgb.setPixelColor(
                0,
                rgb.Color(0, pulseBrightness, 0));

            break;
        }

        case 1002: // Discharging
        {
            rgb.setPixelColor(
                0,
                rgb.Color(255, 0, 0));
            break;
        }

        default:
        {
            rgb.setPixelColor(
                0,
                rgb.Color(255, 255, 255));
            break;
        }
    }

    rgb.show();
}

// ======================================================
// WIFI
// ======================================================

void connectWifi()
{
    Serial.println("Verbinden met WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }

    Serial.println();
    Serial.println("WiFi verbonden");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}

void updateWifiSignal()
{
    lv_label_set_text(
        lblWifiIcon,
        LV_SYMBOL_WIFI);

    if (WiFi.status() == WL_CONNECTED)
    {
        lv_obj_set_style_text_color(
            lblWifiIcon,
            lv_color_white(),
            0);
    }
    else
    {
        lv_obj_set_style_text_color(
            lblWifiIcon,
            lv_color_hex(0xFF0000),
            0);
    }
}

// ======================================================
// INDEVOLT
// ======================================================

void updateIndevolt()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;

    http.begin(API_URL);

    int httpCode = http.GET();

    if (httpCode != 200)
    {
        Serial.printf("HTTP fout: %d\n", httpCode);
        http.end();
        return;
    }

    String payload = http.getString();

    Serial.println(payload);

    JsonDocument doc;

    if (deserializeJson(doc, payload))
    {
        Serial.println("JSON fout");
        http.end();
        return;
    }

    int batteryState = doc["6001"];

    updateStatusLed(batteryState);

    switch (batteryState)
    {
        case 1001: // Charging
            lv_obj_set_style_arc_color(
                arcSOC,
                lv_palette_main(LV_PALETTE_GREEN),
                LV_PART_INDICATOR);
            break;

        case 1002: // Discharging
            lv_obj_set_style_arc_color(
                arcSOC,
                lv_palette_main(LV_PALETTE_RED),
                LV_PART_INDICATOR);
            break;

        case 1000: // Stand-by
            lv_obj_set_style_arc_color(
                arcSOC,
                lv_color_hex(0x40C4FF),
                LV_PART_INDICATOR);
            break;

        default:
            lv_obj_set_style_arc_color(
                arcSOC,
                lv_color_white(),
                LV_PART_INDICATOR);
            break;
    }

    int soc          = doc["6002"];

    float acOutput   = doc["2101"];
    float gridPower  = doc["2108"];
    float totalKwh   = doc["6004"];
    float dayKwh     = doc["6005"];

    lv_arc_set_value(
        arcSOC,
        soc);

    lv_label_set_text_fmt(
        lblSOC,
        "%d%%",
        soc);

    char stateBuffer[64];

    switch (batteryState)
    {
        case 1001: // Charging

            snprintf(
                stateBuffer,
                sizeof(stateBuffer),
                "Charging with %.0f W",
                acOutput);

            lv_obj_set_style_text_color(
                lblState,
                lv_palette_main(LV_PALETTE_GREEN),
                0);

            break;

        case 1002: // Discharging

            snprintf(
                stateBuffer,
                sizeof(stateBuffer),
                "Discharging with %.0f W",
                gridPower);

            lv_obj_set_style_text_color(
                lblState,
                lv_palette_main(LV_PALETTE_RED),
                0);

            break;

        case 1000: // Stand-by

            snprintf(
                stateBuffer,
                sizeof(stateBuffer),
                "Stand-by");

            lv_obj_set_style_text_color(
                lblState,
                lv_color_hex(0x40C4FF),
                0);

            break;

        default:

            snprintf(
                stateBuffer,
                sizeof(stateBuffer),
                "Unknown");

            lv_obj_set_style_text_color(
                lblState,
                lv_color_white(),
                0);

            break;
    }

    lv_label_set_text(
        lblState,
        stateBuffer);

    char buffer[64];

    snprintf(
        buffer,
        sizeof(buffer),
        "%s %.2f kWh",
        LV_SYMBOL_BATTERY_1,
        totalKwh);

    lv_label_set_text(
        lblTotal,
        buffer);

    snprintf(
        buffer,
        sizeof(buffer),
        "%s %.2f kWh",
        LV_SYMBOL_BATTERY_3,
        dayKwh);

    lv_label_set_text(
        lblToday,
        buffer);

    http.end();

    lv_refr_now(display);

    currentBatteryState = batteryState;
}

// ======================================================
// GUI
// ======================================================

void createGui()
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_set_style_bg_color(
        scr,
        lv_color_hex(0x101820),
        0);

    // Titel

    lblTitle = lv_label_create(scr);

    lv_label_set_text(
        lblTitle,
        "INDEVOLT");

    lv_obj_set_style_text_color(
        lblTitle,
        lv_color_hex(0x505050),
        0);

    lv_obj_set_style_text_font(
        lblTitle,
        &lv_font_montserrat_22,
        0);

    lv_obj_align(
        lblTitle,
        LV_ALIGN_TOP_LEFT,
        10,
        2);

    // WiFi

    lblWifiIcon = lv_label_create(scr);

    lv_label_set_text(
        lblWifiIcon,
        LV_SYMBOL_WIFI);

    lv_obj_set_style_text_font(
        lblWifiIcon,
        &lv_font_montserrat_12,
        0);

    lv_obj_align(
        lblWifiIcon,
        LV_ALIGN_TOP_RIGHT,
        -10,
        2);

    // SOC ARC

    arcSOC = lv_arc_create(scr);

    lv_obj_set_style_arc_color(
        arcSOC,
        lv_color_hex(0x404040),
        LV_PART_MAIN);

    lv_obj_set_style_arc_width(
        arcSOC,
        12,
        LV_PART_MAIN);

    lv_obj_set_style_arc_width(
        arcSOC,
        12,
        LV_PART_INDICATOR);

    lv_obj_set_style_arc_width(
        arcSOC,
        12,
        LV_PART_MAIN);

    lv_obj_set_style_arc_width(
        arcSOC,
        12,
        LV_PART_INDICATOR);

    lv_obj_set_size(
        arcSOC,
        140,
        140);

    lv_obj_align(
        arcSOC,
        LV_ALIGN_TOP_MID,
        0,
        50);

    lv_arc_set_range(
        arcSOC,
        0,
        100);

    lv_arc_set_value(
        arcSOC,
        75);

    lv_obj_remove_style(
        arcSOC,
        NULL,
        LV_PART_KNOB);

    lv_obj_clear_flag(
        arcSOC,
        LV_OBJ_FLAG_CLICKABLE);

    // Percentage in midden

    lblSOC = lv_label_create(scr);

    lv_obj_set_style_text_color(
        lblSOC,
        lv_color_white(),
        0);

    lv_obj_set_style_text_font(
        lblSOC,
        &lv_font_montserrat_36,
        0);

    lv_label_set_text(
        lblSOC,
        "100%");

    lv_obj_set_width(
        lblSOC,
        100);

    lv_obj_set_style_text_align(
        lblSOC,
        LV_TEXT_ALIGN_CENTER,
        0);

    lv_obj_align_to(
        lblSOC,
        arcSOC,
        LV_ALIGN_CENTER,
        0,
        0);


    // Status

    lblState = lv_label_create(scr);

    lv_obj_set_style_text_color(
        lblState,
        lv_color_white(),
        0);

    lv_obj_align(
        lblState,
        LV_ALIGN_TOP_MID,
        0,
        185);

    lv_label_set_text(
        lblState,
        "Loading...");

    // Total

    lblTotal = lv_label_create(scr);

    lv_obj_set_style_text_color(
        lblTotal,
        lv_color_white(),
        0);

    lv_obj_align(
        lblTotal,
        LV_ALIGN_BOTTOM_LEFT,
        10,
        -10);

    // Today

    lblToday = lv_label_create(scr);

    lv_obj_set_style_text_color(
        lblToday,
        lv_color_white(),
        0);

    lv_obj_align(
        lblToday,
        LV_ALIGN_BOTTOM_RIGHT,
        -10,
        -10);
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
    Serial.begin(115200);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);

    rgb.begin();
    rgb.clear();
    rgb.show();

    lv_init();

    display = lv_display_create(320, 240);

    lv_display_set_flush_cb(
        display,
        my_flush_cb);

    lv_display_set_buffers(
        display,
        draw_buf,
        NULL,
        sizeof(draw_buf),
        LV_DISPLAY_RENDER_MODE_PARTIAL);

    createGui();

    connectWifi();

    updateWifiSignal();
    updateIndevolt();

    Serial.println("Dashboard gestart");
}

// ======================================================
// LOOP
// ======================================================

void loop()
{
    lv_timer_handler();

    static uint32_t lastWifiUpdate = 0;
    static uint32_t lastIndevoltUpdate = 0;
    static uint32_t lastLedUpdate = 0;

    if (millis() - lastLedUpdate > 50)
    {
        lastLedUpdate = millis();
        updateStatusLed(currentBatteryState);
    }

    if (millis() - lastWifiUpdate > 5000)
    {
        lastWifiUpdate = millis();
        updateWifiSignal();
    }

    if (millis() - lastIndevoltUpdate > 5000)
    {
        lastIndevoltUpdate = millis();
        updateIndevolt();
    }
}