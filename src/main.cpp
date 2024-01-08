#include <Arduino.h>

// Example sketch which shows how to display a 64x32 animated GIF image stored in FLASH memory
// on a 64x32 LED matrix
//
// Credits: https://github.com/bitbank2/AnimatedGIF/tree/master/examples/ESP32_LEDMatrix_I2S
// 

/* INSTRUCTIONS
 *
 * 1. First Run the 'ESP32 Sketch Data Upload Tool' in Arduino from the 'Tools' Menu.
 *    - If you don't know what this is or see it as an option, then read this:
 *      https://github.com/me-no-dev/arduino-esp32fs-plugin 
 *    - This tool will upload the contents of the data/ directory in the sketch folder onto
 *      the ESP32 itself.
 *
 * 2. You can drop any animated GIF you want in there, but keep it to the resolution of the 
 *    MATRIX you're displaying to. To resize a gif, use this online website: https://ezgif.com/
 *
 * 3. Have fun.
 */

#define FILESYSTEM SPIFFS
#include <SPIFFS.h>
#include <string>
#include <AnimatedGIF.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ----------------------------

// Configure for your panel(s) as appropriate!
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 32  	// Panel height of 64 will required PIN_E to be defined.
#define NUM_ROWS 2 // Number of rows of chained INDIVIDUAL PANELS
#define NUM_COLS 2 // Number of INDIVIDUAL PANELS per ROW

#define PANE_WIDTH (PANEL_WIDTH * NUM_COLS)
#define PANE_HEIGHT (PANEL_HEIGHT * NUM_ROWS)

#define PANELS_NUMBER NUM_ROWS*NUM_COLS    // total number of panels chained one to another
#define PIN_E 32

// Change this to your needs, for details on VirtualPanel pls read the PDF!
#define SERPENT true
#define TOPDOWN true

#define R1_PIN 05
#define G1_PIN 04
#define B1_PIN 18
#define R2_PIN 19
#define G2_PIN 2
#define B2_PIN 23
#define A_PIN 13
#define B_PIN 15
#define C_PIN 12
#define D_PIN 25
#define E_PIN 32 
#define LAT_PIN 26
#define OE_PIN 27
#define CLK_PIN 14

#define WIFI_SSID "<SSID>"
#define WIFI_PSK "<PSK>"

#define FILENAME "/disp.gif"
#define TEMPFILENAME "/temp.gif"
#define BOOTFILENAME "/boot.gif"

HUB75_I2S_CFG::i2s_pins _pins={R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};

#define MAX_FILESZ 255

MatrixPanel_I2S_DMA *dma_display = nullptr;
VirtualMatrixPanel  *virtualDisp = nullptr;

AnimatedGIF gif;
File f;
int x_offset, y_offset;

// Set your Static IP address
IPAddress local_IP(10, 42, 28, 7);
// Set your Gateway IP address
IPAddress gateway(10, 42, 1, 1);

IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8);   //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional


AsyncWebServer *server;               // initialise webserver
bool downloadedGif = false;
bool uploadStarted = false; // True if currently downloading a gif


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
</head>
<body>
  <p><h1>File Upload</h1></p>
  <p>Free Storage: %FREESPIFFS% | Used Storage: %USEDSPIFFS% | Total Storage: %TOTALSPIFFS%</p>
  <form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="data"/><input type="submit" name="upload" value="Upload" title="Upload File"></form>
  <p>After clicking upload it will take some time for the file to firstly upload and then be written to SPIFFS, there is no indicator that the upload began.  Please be patient.</p>
  <p>Once uploaded the page will refresh and the newly uploaded file will appear in the file list.</p>
  <p>If a file does not appear, it will be because the file was too big, or had unusual characters in the file name (like spaces).</p>
  <p>You can see the progress of the upload by watching the serial output.</p>
  <p>%FILELIST%</p>
</body>
</html>
)rawliteral";

/**
 * @brief Hack to allow for the used non-standard display configuration.
 * ~3   ~4
 *  2    1
 * (Not supported by virtualdisplay)
 * 
 * @param x 
 * @param y 
 * @param color RGB color in 565 format
 */
void drawPixelToScreen(int16_t x, int16_t y, uint16_t color){
  virtualDisp->drawPixel(x, ( (y+PANEL_HEIGHT)%(PANEL_HEIGHT*NUM_ROWS) ), color);
}

/************************* GIF Stuff *******************************/
// Draw a line of image directly on the LED Matrix
void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth, iHeight, x_offs, y_offs;

    iWidth = pDraw->iWidth;
    if (iWidth > PANE_WIDTH)
        iWidth = PANE_WIDTH;
    iHeight = pDraw->iHeight;
    //Serial.printf(" Frame: %d x %d\n", iWidth, iHeight);
    x_offs = (PANE_WIDTH - iWidth)/2;
    if (x_offs < 0) x_offs = 0;
    y_offs = (PANE_HEIGHT - iHeight)/2;
    if (y_offs < 0) y_offs = 0;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line
    
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
      for (x=0; x<iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }
    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) // if transparency used
    {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + pDraw->iWidth;
      x = 0;
      iCount = 0; // count non-transparent pixels
      while(x < pDraw->iWidth)
      {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent) // done, stop
          {
            s--; // back up to treat it like transparent
          }
          else // opaque
          {
             *d++ = usPalette[c];
             iCount++;
          }
        } // while looking for opaque pixels
        if (iCount) // any opaque pixels?
        {
          for(int xOffset = 0; xOffset < iCount; xOffset++ ){
            drawPixelToScreen(x + xOffset + pDraw->iX, y + pDraw->iY, usTemp[xOffset]); // 565 Color Format
          }
          x += iCount;
          iCount = 0;
        }
        // no, look for a run of transparent pixels
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--; 
        }
        if (iCount)
        {
          x += iCount; // skip these
          iCount = 0;
        }
      }
    }
    else // does not have transparency
    {
      s = pDraw->pPixels;
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      for (x=0; x<pDraw->iWidth; x++)
      {
        drawPixelToScreen(x + pDraw->iX, y + pDraw->iY, usPalette[*s++]); // color 565
      }
    }
} /* GIFDraw() */


void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  f = FILESYSTEM.open(fname);
  if (f)
  {
    *pSize = f.size();
    return (void *)&f;
  }
  return NULL;
} /* GIFOpenFile() */

void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
} /* GIFCloseFile() */

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
} /* GIFReadFile() */

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{ 
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
//  Serial.printf("Seek time = %d us\n", i);
  return pFile->iPos;
} /* GIFSeekFile() */

unsigned long start_tick = 0;

void ShowGIF(char *name)
{
  start_tick = millis();
   
  if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
  {
    x_offset = (PANE_WIDTH - gif.getCanvasWidth())/2;
    if (x_offset < 0) x_offset = 0;
    y_offset = (PANE_HEIGHT - gif.getCanvasHeight())/2;
    if (y_offset < 0) y_offset = 0;
    Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    Serial.flush();
    while (gif.playFrame(true, NULL))
    {      
      // if ( (millis() - start_tick) > 8000) { // we'll get bored after about 8 seconds of the same looping gif
      //   break;
      // }
      if(uploadStarted)
        break; // stop playing the gif if we're uploading a new one
    }
    gif.close();
  }

} /* ShowGIF() */

// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32
String humanReadableSize(const size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}
// list all of the files, if ishtml=true, return html rather than simple text
String listFiles(bool ishtml) {
  String returnText = "";
  Serial.println("Listing files stored on SPIFFS");
  File root = SPIFFS.open("/");
  File foundfile = root.openNextFile();
  if (ishtml) {
    returnText += "<table><tr><th align='left'>Name</th><th align='left'>Size</th></tr>";
  }
  while (foundfile) {
    if (ishtml) {
      returnText += "<tr align='left'><td>" + String(foundfile.name()) + "</td><td>" + humanReadableSize(foundfile.size()) + "</td></tr>";
    } else {
      returnText += "File: " + String(foundfile.name()) + "\n";
    }
    foundfile = root.openNextFile();
  }
  if (ishtml) {
    returnText += "</table>";
  }
  root.close();
  foundfile.close();
  return returnText;
}

String processor(const String& var) {
  if (var == "FILELIST") {
    return listFiles(true);
  }
  if (var == "FREESPIFFS") {
    return humanReadableSize((SPIFFS.totalBytes() - SPIFFS.usedBytes()));
  }

  if (var == "USEDSPIFFS") {
    return humanReadableSize(SPIFFS.usedBytes());
  }

  if (var == "TOTALSPIFFS") {
    return humanReadableSize(SPIFFS.totalBytes());
  }

  return String();
}
// handles uploads
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);

  if (!index) {
    logmessage = "Upload Start: " + String(filename);
    uploadStarted = true;
    // open the file on first call and store the file handle in the request object
    request->_tempFile = SPIFFS.open(TEMPFILENAME, "w");
    Serial.println(logmessage);
  }

  if (len) {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    Serial.println(logmessage);
  }

  if (final) {
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    // close the file handle as the upload is now done
    request->_tempFile.close();
    downloadedGif = true;
    Serial.println(logmessage);
    request->redirect("/");
  }
}


void configureWebServer() {
  server->on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() + + " " + request->url();
    Serial.println(logmessage);
    request->send_P(200, "text/html", index_html, processor);
  });

  // run handleUpload function when any file is uploaded
  server->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
      }, handleUpload);
}

void copyFile(String FileOriginal, String FileCopy) {
  uint8_t ibuffer[64];  //declare a buffer
    
  Serial.println("In copy");     //debug


  if (SPIFFS.exists(FileCopy) == true) {  //remove file copy
    SPIFFS.remove(FileCopy);
  }

  File f1 = SPIFFS.open(FileOriginal, "r");    //open source file to read
  if (!f1) {
    return;
  }

  File f2 = SPIFFS.open(FileCopy, "w");    //open destination file to write
  if (!f2) {
    return;
  }
    
  while (f1.available() > 0) {
    byte i = f1.read(ibuffer, 64); // i = number of bytes placed in buffer from file f1
    f2.write(ibuffer, i);               // write i bytes from buffer to file f2
  }
    
  f2.close(); // done, close the destination file
  f1.close(); // done, close the source file

}



/************************* Arduino Sketch Setup and Loop() *******************************/
void setup() {

  HUB75_I2S_CFG mxconfig;
  mxconfig.mx_height = PANEL_HEIGHT;      // we have 64 pix heigh panels
  mxconfig.chain_length = PANELS_NUMBER;  // we have 2 panels chained
  //mxconfig.gpio.e = PIN_E; // we MUST assign pin e to some free pin on a board to drive 64 pix height panels with 1/32 scan
  mxconfig.gpio = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};;                // we MUST assign pin e to some free pin on a board to drive 64 pix height panels with 1/32 scan

  // Display Setup
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(37); //0-255

  // create VirtualDisplay object based on our newly created dma_display object
  virtualDisp = new VirtualMatrixPanel((*dma_display), NUM_ROWS, NUM_COLS, PANEL_WIDTH, PANEL_HEIGHT, SERPENT, TOPDOWN);
  virtualDisp->clearScreen();
  
  Serial.begin(115200);

  // Start filesystem
  Serial.println(" * Loading SPIFFS");
  if(!SPIFFS.begin()){
        Serial.println("SPIFFS Mount Failed");
  }
  copyFile(BOOTFILENAME, FILENAME);

  Serial.print("SPIFFS Free: "); Serial.println(humanReadableSize((SPIFFS.totalBytes() - SPIFFS.usedBytes())));
  Serial.print("SPIFFS Used: "); Serial.println(humanReadableSize(SPIFFS.usedBytes()));
  Serial.print("SPIFFS Total: "); Serial.println(humanReadableSize(SPIFFS.totalBytes()));

  Serial.println(listFiles(false));

  Serial.println("\nLoading Configuration ...");
  
  /* all other pixel drawing functions can only be called after .begin() */
  virtualDisp->fillScreen(virtualDisp->color565(0, 0, 0));
  gif.begin(LITTLE_ENDIAN_PIXELS);

  // Configures static IP address
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }

  Serial.printf("\nConnecting to Wifi \"%s\":", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n\nNetwork Configuration:");
  Serial.println("----------------------");
  Serial.print("         SSID: "); Serial.println(WiFi.SSID());
  Serial.print("  Wifi Status: "); Serial.println(WiFi.status());
  Serial.print("Wifi Strength: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  Serial.print("          MAC: "); Serial.println(WiFi.macAddress());
  Serial.print("           IP: "); Serial.println(WiFi.localIP());
  Serial.print("       Subnet: "); Serial.println(WiFi.subnetMask());
  Serial.print("      Gateway: "); Serial.println(WiFi.gatewayIP());
  Serial.print("        DNS 1: "); Serial.println(WiFi.dnsIP(0));
  Serial.print("        DNS 2: "); Serial.println(WiFi.dnsIP(1));
  Serial.print("        DNS 3: "); Serial.println(WiFi.dnsIP(2));

  // configure web server
  Serial.println("\nConfiguring Webserver ...");
  server = new AsyncWebServer(80);
  configureWebServer();

  // startup web server
  Serial.println("Starting Webserver ...");
  server->begin();

}

void loop() {
  if(!uploadStarted)
    ShowGIF(FILENAME); // restart gif only if upload is not in progress
  //else
  //  delay(100); // delay otherwise
  if(downloadedGif){
    SPIFFS.remove(FILENAME);
    SPIFFS.rename(TEMPFILENAME, FILENAME);
    downloadedGif = false;
    uploadStarted = false; // reset upload flag
  }
}
