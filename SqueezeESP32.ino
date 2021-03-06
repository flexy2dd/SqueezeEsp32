#include <Arduino.h>
#include "config.h"

#ifdef ESP32
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
#endif

#include <WiFiUdp.h>


#ifdef VS1053_MODULE
  #include "flac_plugin.h"


  #undef I2S_DAC_MODULE
  #ifdef ESP32
    #define VS1053_CS     5   // D1 // 5
    #define VS1053_DCS    16  // D0 // 16
    #define VS1053_DREQ   4   // D3 // 4
  #else
  // ESP8266
    #define VS1053_CS     D1 // 5
    #define VS1053_DCS    D0 // 16
    #define VS1053_DREQ   D3 // 4
  #endif
  #ifdef ADAFRUIT_VS1053
    #include <Adafruit_VS1053.h>
  #else
    #include <VS1053.h>
  #endif
#endif

#ifdef I2S_DAC_MODULE 
  #include "AudioFileSourceICYStream.h"
  #include "AudioFileSourceBuffer.h"
  #include "AudioGeneratorMP3.h"
  #include "AudioOutputI2SNoDAC.h"
#endif

#include "slimproto.h"


#ifdef VS1053_MODULE
  #ifdef ADAFRUIT_VS1053
    Adafruit_VS1053 viplayer(-1, VS1053_CS, VS1053_DCS, VS1053_DREQ);
  #else
    VS1053 viplayer(VS1053_CS, VS1053_DCS, VS1053_DREQ);
  #endif
#endif



#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#ifdef ESP32
#include <WebServer.h> 
#else
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#endif


#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

slimproto * vislimCli = 0;
WiFiClient client;
int       viCnxAttempt = -1; 

WiFiUDP udp;

#ifdef VS1053_MODULE
void LoadPlugin(const uint16_t* plugin, uint16_t plugin_size)
{
int i = 0;
while (i<plugin_size)
  {
  uint16_t addr, n, val;
  addr = plugin[i++];
  n = plugin[i++];
  if (n & 0x8000U)
    { /* RLE run, replicate n samples */
    n &= 0x7FFF;
    val = plugin[i++];
    while (n--)
      {
      #ifdef ADAFRUIT_VS1053
        viplayer.sciWrite(addr, val);
      #else
        viplayer.write_register(addr, val);
      #endif
      }
    }
  else
    { /* Copy run, copy n samples */
    while (n--)
      {
      val = plugin[i++];
      #ifdef ADAFRUIT_VS1053
        viplayer.sciWrite(addr, val);
      #else
        viplayer.write_register(addr, val);
      #endif
      }
    }
  }
}

#endif //VS1053_MODULE
void setup()
{


  
  viCnxAttempt = 0;
  
  Serial.begin(115200);
  delay(1000);
  Serial.println("Connecting to WiFi");

  SPI.begin();
  #ifdef VS1053_MODULE
    viplayer.begin();
    #ifndef ADAFRUIT_VS1053
      viplayer.switchToMp3Mode();
      LoadPlugin(plugin,PLUGIN_SIZE);
      viplayer.setVolume(80);
    #else
      viplayer.begin();
      //viplayer.applyPatch(plugin,PLUGIN_SIZE);
      LoadPlugin(plugin,PLUGIN_SIZE);
      viplayer.setVolume(20,20);
    #endif
  #endif

  
  WiFiManager wifiManager;


  //reset saved settings
  //wifiManager.resetSettings();
  
  wifiManager.autoConnect("SqueezeEsp");



  

/*
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  
  WiFi.begin("ssid","wifipass");

  // Try forever
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("...Connecting to WiFi");
    delay(1000);
  }
  Serial.println("Connected");

*/
   udp.begin(UDP_PORT);
}


void loop()
{
   

   // Reset addr of LMS
   if(viCnxAttempt == -1)
    LMS_addr = IPAddress(0,0,0,0);


   // If no LMS found yet
   if(LMS_addr[0] == 0)
    {
     Serial.println("Search for LMS server..."); 
     for(int nbSend = 0; nbSend < 10; nbSend++)
      {
       // start UDP server
       //Send udp packet for autodiscovery
       udp.flush();
       udp.beginPacket("255.255.255.255",UDP_PORT);
       udp.printf("e");
       udp.endPacket();
    
       delay(2000);
    
       if(udp.parsePacket()> 0)
        {
        char upd_packet; 
        upd_packet = udp.read();

        if(upd_packet == 'E')
          {
          LMS_addr = udp.remoteIP();
          Serial.print("Found LMS server @ "); 
          Serial.println(LMS_addr); 
          //udp.stop(); 
          break; // LMS found we can go to the next step
          }
        }
       else
        delay(2000);
      }
    }

if(LMS_addr[0] != 0)
  {
  Serial.print("Connecting to server @");
  Serial.print(LMS_addr);
  Serial.println("..."); 
  
  // DEBUG server 3484
  // Real server 3483

   viCnxAttempt++;
  
   if (!client.connect(LMS_addr, 3483)) {
        Serial.println("connection failed, pause and try connect...");

        viCnxAttempt++;
        if(viCnxAttempt > 30)
          viCnxAttempt = -1; // Will erase LMS addr in the next attempt
        
        delay(2000);
        return;
      }

   
  viCnxAttempt = 0;
  
  if(vislimCli) delete vislimCli,vislimCli = 0;
  
   #ifdef VS1053_MODULE
    vislimCli = new slimproto(LMS_addr.toString(), & client, &viplayer);
   #else
    vislimCli = new slimproto(LMS_addr.toString(), client);
   #endif
  
  Serial.println("Connection Ok, send hello to LMS");
  reponseHelo *  HeloRsp = new reponseHelo(&client);
  HeloRsp->sendResponse();
  
  while(client.connected())
    {
    if(!vislimCli->HandleMessages())
      break;
    vislimCli->HandleAudio();
    }
  }
 else
 {
 Serial.println("No LMS server found, try again in 10 seconds"); 
 delay(10000);
 }
}
