/*
    Copyright (c) 2015,
    - Kazuyuki TAKASE - https://github.com/Guvalif
    - PLEN Project Company Inc. - https://plen.jp

    This software is released under the MIT License.
    (See also : http://opensource.org/licenses/mit-license.php)
*/
#include <Ticker.h>
#include "Arduino.h"
#include "Pin.h"
#include "System.h"
#include <ESP8266WiFi.h>
#include <WiFiUDP.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include "ExternalFs.h"
#include "Profiler.h"

#define PLEN2_SYSTEM_SERIAL Serial

#define MAX_AP_NAME_SIZE 1024
#define MAX_AP_PSW_SIZE  1024
#define MAX_ROBOT_NAME_SIZE 1024
#define CONNECT_TO_CNT 100

#define ENABLE_SPIFFS_DOWNLOAD false
#define FM_VERSION "V2"

IPAddress broadcastIp(255, 255, 255, 255);
#define BROADCAST_PORT 6000
WiFiUDP udp;

const char* host = "jrobot-ota";
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
static bool upload_server_status = false;
#define HOSTNAME "JROBOT-OTA"


WiFiServer tcp_server(23);
WiFiClient serverClient;
Ticker smartconfig_tricker;

String robot_name = "JRobot-" + String(ESP.getChipId()) + ":" + FM_VERSION;
const char *wifi_psd = "12345678xyz";

volatile bool update_cfg;

extern File fp_syscfg;

void PLEN2::System::StartAp()
{
#if CLOCK_WISE
    String ap_name = "JRobot-M-" + String(ESP.getChipId());
#else
    String ap_name = "JRobot-N-" + String(ESP.getChipId());
#endif
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_name.c_str(), wifi_psd);

    IPAddress my_ip = WiFi.softAPIP();
    outputSerial().print("start AP! SSID:");
    outputSerial().print(ap_name);
    outputSerial().print("PSWD:");
    outputSerial().println(wifi_psd);
}

PLEN2::System::System()
{
    PLEN2_SYSTEM_SERIAL.begin(SERIAL_BAUDRATE());
    WiFi.mode(WIFI_STA);
    tcp_server.begin();
    tcp_server.setNoDelay(true);
}

void PLEN2::System::setup_smartconfig()
{
    unsigned char cnt;
    update_cfg = true;
    if( fp_syscfg && fp_syscfg.available())
    {
        fp_syscfg.seek(0, SeekSet);
        String ext_apname = fp_syscfg.readStringUntil('\n');
        String ext_appsw;
        if(ext_apname.length() > 1)
        {
            outputSerial().print("ap:");
            outputSerial().println(ext_apname);
            if(fp_syscfg.available())
            {
                ext_appsw = fp_syscfg.readStringUntil('\n');
                outputSerial().print("psw:");
                outputSerial().println(ext_appsw);

                char extap_name_char[ext_apname.length()];
                memset(extap_name_char, '\0', ext_apname.length());
                for (int i = 0; i < ext_apname.length() - 1; i++)
                {
                    extap_name_char[i] = ext_apname.charAt(i);
                    outputSerial().println(extap_name_char);
                }
                if (ext_appsw.length() > 1)
                {
                    char extap_psw_char[ext_appsw.length()];
                    memset(extap_psw_char, '\0', ext_appsw.length());
                    for (int i = 0; i < ext_appsw.length() - 1; i++)
                    {
                        extap_psw_char[i] = ext_appsw.charAt(i);
                    }
                    outputSerial().println(extap_psw_char);

                    WiFi.begin(extap_name_char, extap_psw_char);
                }
                else
                {
                    outputSerial().println("psd is NULL!\n");
                    WiFi.begin(extap_name_char, NULL);
                }
                cnt = 0;
                while (WiFi.status() != WL_CONNECTED)
                {
                    delay(100);
                    outputSerial().print(".");
                    cnt++;
                    if(cnt >= CONNECT_TO_CNT)
                    {
                        break;
                    }
                }
                if(cnt < CONNECT_TO_CNT)
                {
                    update_cfg = false;
                }
            }
        }
    }
    if(update_cfg)
    {
        WiFi.beginSmartConfig();
    }
    smartconfig_tricker.attach_ms(1024, PLEN2::System::smart_config);
}

//http:///download?file=/Config.txt
void PLEN2::System::handle_download()
{
    if (!SPIFFS.begin())
    {
        outputSerial().println("SPIFFS failed to mount !\r\n");
    }
    else
    {
        String str = "";
        File f = SPIFFS.open(httpServer.arg(0), "r");
        if (!f)
        {
            outputSerial().println("Can't open SPIFFS file !\r\n");
        }
        else
        {
            char buf[1024];
            int siz = f.size();
            while(siz > 0)
            {
                size_t len = std::min((int)(sizeof(buf) - 1), siz);
                f.read((uint8_t *)buf, len);
                buf[len] = 0;
                str += buf;
                siz -= sizeof(buf) - 1;
            }
            f.close();
            httpServer.send(200, "text/plain", str);
        }
    }
}

void PLEN2::System::smart_config()
{
    static int cnt = 0;
    static int timeout = 30;


    if(!update_cfg && ((WiFi.status() == WL_CONNECTED) || WiFi.softAPgetStationNum()))
    {
        udp.beginPacketMulticast(broadcastIp, BROADCAST_PORT, WiFi.localIP());
        udp.write(robot_name.c_str(), robot_name.length());
        udp.endPacket();

        if (!upload_server_status)
        {
#if ENABLE_SPIFFS_DOWNLOAD
            httpServer.on("/download", handle_download);
#endif

            //if (!MDNS.begin(host))
            //{
            //    Serial.println("Error setting up MDNS responder!");
            //    while(1)
            //  {
            //      delay(1000);
            //    }
            //}

            httpUpdater.setup(&httpServer);
            httpServer.begin();
            upload_server_status = true;
            outputSerial().printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", host);
        }
    }

    if(update_cfg && WiFi.smartConfigDone())
    {
        outputSerial().println("smartConfigDone!\n");
        outputSerial().printf("SSID:%s\r\n", WiFi.SSID().c_str());
        outputSerial().printf("PSW:%s\r\n", WiFi.psk().c_str());

        if(fp_syscfg)
        {
            fp_syscfg.close();
            fp_syscfg = SPIFFS.open(SYSCFG_FILE, "w");
            fp_syscfg.println(WiFi.SSID().c_str());
            fp_syscfg.println(WiFi.psk().c_str());
            fp_syscfg.close();
            fp_syscfg = SPIFFS.open(SYSCFG_FILE, "r");
        }
        update_cfg = false;
    }

    if(update_cfg && (cnt++ > timeout))
    {
        WiFi.stopSmartConfig();
        StartAp();
        update_cfg = false;
    }
}
void PLEN2::System::handleClient()
{
    if (upload_server_status)
    {
        httpServer.handleClient();
    }
}

bool PLEN2::System::tcp_available()
{
    if (tcp_server.hasClient())
    {
        serverClient = tcp_server.available();
        if (!serverClient || !serverClient.connected())
        {
            if(serverClient)
            {
                serverClient.stop();
            }
            serverClient = tcp_server.available();
        }
    }
    if (serverClient && serverClient.connected())
    {
        return serverClient.available();
    }
    return false;
}

bool PLEN2::System::tcp_connected()
{
    return serverClient && serverClient.connected();
}

char PLEN2::System::tcp_read()
{
    return serverClient.read();
}

size_t PLEN2::System::tcp_write(const char* sbuf, size_t len)
{
    const size_t unit_size = 512;
    size_t size_to_send = len;
    const char* send_start = sbuf;
    if (tcp_connected())
    {
        while (size_to_send) 
        {
            size_t will_send = (size_to_send < unit_size) ? size_to_send : unit_size;
            size_t sent = serverClient.write(send_start, will_send);
            if (sent == 0) {
                break;
            }
            size_to_send -= sent;
            send_start += sent;
        };
    }
    return (size_t)(send_start - sbuf);
}

Stream& PLEN2::System::SystemSerial()
{
    return PLEN2_SYSTEM_SERIAL;
}


Stream& PLEN2::System::inputSerial()
{
    return PLEN2_SYSTEM_SERIAL;
}


Stream& PLEN2::System::outputSerial()
{
    return PLEN2_SYSTEM_SERIAL;
}

Stream& PLEN2::System::debugSerial()
{
    return PLEN2_SYSTEM_SERIAL;
}


void PLEN2::System::dump()
{
#if DEBUG
    volatile Utility::Profiler p(F("System::dump()"));
#endif

    outputSerial().println(F("{"));

    outputSerial().print(F("\t\"device\": \""));
    outputSerial().print(DEVICE_NAME);
    outputSerial().println(F("\","));

    outputSerial().print(F("\t\"codename\": \""));
    outputSerial().print(CODE_NAME);
    outputSerial().println(F("\","));

    outputSerial().print(F("\t\"version\": \""));
    outputSerial().print(VERSION);
    outputSerial().println(F("\""));

    outputSerial().println(F("}"));
}
