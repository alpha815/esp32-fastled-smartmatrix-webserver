/*
   ESP32 FastLED WebServer: https://github.com/jasoncoon/esp32-fastled-webserver
   Copyright (C) 2017 Jason Coon

   Built upon the amazing FastLED work of Daniel Garcia and Mark Kriegsman:
   https://github.com/FastLED/FastLED

   ESP32 support provided by the hard work of Sam Guyer:
   https://github.com/samguyer/FastLED

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  This is an only partially functional port of the sketch to the SmartMatrix Library

  Right now it just runs like the FastLED_Functions example, with "power" controlling the circle, and "speed" controlling the noise speed

  It's released as of now as a bare minimum example of SmartMatrix Library working with WiFi and a web server
*/

#include <SmartMatrix3.h>
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <EEPROM.h>

#define EXTRA_RAM_PRINTFS

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001008)
#warning "Requires FastLED 3.1.8 or later; check github for latest code."
#endif

#define COLOR_DEPTH 24                  // This sketch and FastLED uses type `rgb24` directly, COLOR_DEPTH must be 24
const uint8_t kMatrixWidth = 32;        // known working: 32, 64, 96, 128
const uint8_t kMatrixHeight = 16;       // known working: 16, 32, 48, 64
const uint8_t kRefreshDepth = 36;       // known working: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save memory, more to keep from dropping frames and automatically lowering refresh rate
const uint8_t kPanelType = SMARTMATRIX_HUB75_16ROW_MOD8SCAN;   // use SMARTMATRIX_HUB75_16ROW_MOD8SCAN for common 16x32 panels
const uint8_t kMatrixOptions = (SMARTMATRIX_OPTIONS_NONE);      // see http://docs.pixelmatix.com/SmartMatrix for options
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);
const uint8_t kScrollingLayerOptions = (SM_SCROLLING_OPTIONS_NONE);

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);
SMARTMATRIX_ALLOCATE_SCROLLING_LAYER(scrollingLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kScrollingLayerOptions);

const rgb24 defaultBackgroundColor = {0x0, 0, 0};
#define NUM_LEDS  512
// The 32bit version of our coordinates
static uint16_t x;
static uint16_t y;
static uint16_t z;

WebServer webServer(80);


uint8_t autoplay = 0;
uint8_t autoplayDuration = 10;
unsigned long autoPlayTimeout = 0;

uint8_t currentPatternIndex = 0; // Index number of which pattern is current

uint8_t gHue = 0; // rotating "base color" used by many of the patterns

uint8_t power = 1;
uint8_t brightness = 8;

uint8_t speed = 20;

// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100
uint8_t cooling = 50;
uint16_t scale = 31;

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
uint8_t sparking = 120;

CRGB solidColor = CRGB::Blue;

uint8_t cyclePalettes = 0;
uint8_t paletteDuration = 10;
uint8_t currentPaletteIndex = 0;
unsigned long paletteTimeout = 0;

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#include "patterns.h"

#include "field.h"
#include "fields.h"

#include "secrets.h"
#include "wifi.h"
#include "web.h"

// wifi ssid and password should be added to a file in the sketch named secrets.h
// the secrets.h file should be added to the .gitignore file and never committed or
// pushed to public source control (GitHub).
// const char* ssid = "........";
// const char* password = "........";

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

// This is the array that we keep our computed noise values in
uint8_t noise[kMatrixWidth][kMatrixHeight];

void setup() {
  //  delay(3000); // 3 second delay for recovery
  Serial.begin(115200);

#ifdef EXTRA_RAM_PRINTFS
  printf("\r\n*** Start: \r\n");
  printf("Heap Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0));
  printf("8-bit Accessible Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  printf("32-bit Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_32BIT), heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));
  printf("DMA Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
#endif

  SPIFFS.begin();
  listDir(SPIFFS, "/", 1);

//  loadFieldsFromEEPROM(fields, fieldCount);

#ifdef EXTRA_RAM_PRINTFS
  printf("\r\nSPIFFS: \r\n");
  printf("Heap Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0));
  printf("8-bit Accessible Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  printf("32-bit Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_32BIT), heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));
  printf("DMA Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
#endif

  setupWifi();

#ifdef EXTRA_RAM_PRINTFS
  printf("\r\nWiFi: \r\n");
  printf("Heap Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0));
  printf("8-bit Accessible Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  printf("32-bit Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_32BIT), heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));
  printf("DMA Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
#endif

  setupWeb();

#ifdef EXTRA_RAM_PRINTFS
  printf("\r\nWeb: \r\n");
  printf("Heap Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0));
  printf("8-bit Accessible Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  printf("32-bit Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_32BIT), heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));
  printf("DMA Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
#endif

  matrix.addLayer(&backgroundLayer); 
  matrix.addLayer(&scrollingLayer); 

  // 30kB extra (DMA) RAM left over seems to be enough for the WiFi+Web portion of the sketch to work (For 64x64/32 24-bit, 5kB was not enough to run, 10kB-20kB runs but fails when serving the webpage, 30kB worked with "Lowest 8-bit Accessible Memory Available: 4112" observed)
  matrix.begin(30*1024);

  backgroundLayer.setBrightness(128);
  // Show off smart matrix scrolling text

}



void loop()
{


  handleWeb();

  // if sketch uses swapBuffers(false), wait to get a new backBuffer() pointer after the swap is done:
  while(backgroundLayer.isSwapPending());

  rgb24 *buffer = backgroundLayer.backBuffer();


  if (power == 0) {
    backgroundLayer.fillScreen(defaultBackgroundColor);
  }
  else {
    // Call the current pattern function once, updating the 'leds' array
    patterns[currentPatternIndex].pattern();

    EVERY_N_MILLISECONDS(40) {
      // slowly blend the current palette to the next
      nblendPaletteTowardPalette(currentPalette, targetPalette, 8);
      gHue++;  // slowly cycle the "base color" through the rainbow
     
    }

    if (autoplay == 1 && (millis() > autoPlayTimeout)) {
      nextPattern();
      autoPlayTimeout = millis() + (autoplayDuration * 1000);
    }

    if (cyclePalettes == 1 && (millis() > paletteTimeout)) {
      nextPalette();
      paletteTimeout = millis() + (paletteDuration * 1000);
    }
  }
  // buffer is filled completely each time, use swapBuffers without buffer copy to save CPU cycles
  backgroundLayer.swapBuffers(false);
  //matrix.countFPS();      // print the loop() frames per second to Serial

#ifdef EXTRA_RAM_PRINTFS
  static unsigned long nextMillis = 0;
  if(millis() > nextMillis) {
    nextMillis = millis() + 10000;

    printf("\r\nPeriodic: \r\n");
    printf("Heap Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(0), heap_caps_get_largest_free_block(0));
    printf("8-bit Accessible Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    printf("32-bit Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_32BIT), heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));
    printf("DMA Memory Available: %d bytes total, %d bytes largest free block: \r\n", heap_caps_get_free_size(MALLOC_CAP_DMA), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    printf("Lowest Heap Memory Available: %d\r\n", heap_caps_get_minimum_free_size(0));
    printf("Lowest 8-bit Accessible Memory Available: %d\r\n", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    printf("Lowest 32-bit Memory Available: %d\r\n", heap_caps_get_minimum_free_size(MALLOC_CAP_32BIT));
    printf("Lowest DMA Memory Available: %d\r\n", heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));
  }
#endif
}
void nextPattern()
{
  // add one to the current pattern number, and wrap around at the end
  currentPatternIndex = (currentPatternIndex + 1) % patternCount;
}

void nextPalette()
{
  currentPaletteIndex = (currentPaletteIndex + 1) % paletteCount;
  targetPalette = palettes[currentPaletteIndex];
}
