#include <VirtualWire.h>
#include <TinyGPS++.h>
#include <TinyGPSPlus.h>
#include <FS.h>
#include <FSImpl.h>
#include <vfs_api.h>
#include <dummy.h>
#include <DFRobot_BMP388_SPI.h>
#include <bmp3_defs.h>
#include <DFRobot_BMP388.h>
#include <DFRobot_BMP388_I2C.h>
#include <DFRobot_BMX160.h>
#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h> 
#include <ESP32_FTPClient.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <ESP32Time.h>
#include <Math.h>
#include <SD.h>
#include <Wire.h>







#define WIFI_SSID "**********" //Wifi baglantisi icin
#define WIFI_PASS "**********"
#define BAUDRATE 115200

#define GPS_RX_PIN 11 //BELİRLENMEDİ
#define GPS_RX_PIN 12

#define RF_RX_PIN 13// BELIRLENMEDİ  HATA VERMESİN DİYE RASTGELE YAZDIM

#define SD_PIN 19

char ftp_server[] = "**********";
char ftp_user[] = "**********"; //Filezilla'dan olusturulacak (13.01.23)
char ftp_pass[] = "**********";



int32_t packageNo;
int8_t status=0;
String errorCode="00000";
String dateTime;
double pressure1;
double pressure2;
double altitude1;
double altitude2;
double prevAltitude;
double altitudeDiff;
double landingSpeed;
double temperature;
double batteryVoltage;
double gpsLatitude;
double gpsLongtitude;
double gpsAltitude;
double roll;
double pitch;
double yaw;
int teamNo;
double oldAltitude1;
String telemetryString;

bool standby = true;
bool rising;
bool landing;
bool seperated;
bool landed;

ESP32Time rtc(3600);
TinyGPSPlus gps;
SoftwareSerial ss(GPS_RX_PIN, GPS_RX_PIN);




//FTP bilgilerini filezilla uzerinden olusturduktan sonra, alinan server username ve password bilgilerini yer istasyonuna aktar (13.01.23)
ESP32_FTPClient ftp(ftp_server, ftp_user, ftp_pass, 5000, 2);

DFRobot_BMX160 bmx160;
DFRobot_BMP388_I2C bmp388;

hw_timer_t* timer = NULL;// 1 hz data gönderimi için timer



float seaLevel;

void setup()
{
    Serial.begin(BAUDRATE);
    Serial1.begin(9600);
    
    EEPROM.begin(512);

    //CONTAINER RF SETUP
    vw_set_ptt_inverted(true);
    vw_set_rx_pin(RF_RX_PIN);
    vw_setup(4000);
    vw_rx_start();
    
    //SD KART KONTROLÜ
    if (!SD.begin(SD_PIN)) {
        Serial.println("Sd failure!!");
        while (true);
    }

    //ZAMAN SET
    rtc.setTime(30, 24, 15, 17, 1, 2042);  // 17th Jan 2021 15:24:30
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("Connecting Wifi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("IP address: "); //cmd'deki IP adresiyle ayni olup olmadigini kontrol et (15.01.23)
    Serial.println(WiFi.localIP());


    ftp.OpenConnection();
    ftp.ChangeWorkDir("C:/Users/metin/Desktop/FTP_TEST/TEST FOLDER");
    ftp.InitFile("TEST-PAYLOAD I");
    ftp.NewFile("telemetri.csv"); //CSV uzantisi ile kaydediyoruz (calismazsa .txt'yi kullan (18.01.23))
    
    if (bmx160.begin() != true) //Ivmeolcer sensorunun initialize edilip edilmedigini kontrol ediyoruz
    {
        Serial.println("init false");
        while (1);
    }
    bmp388.set_iic_addr(BMP3_I2C_ADDR_SEC);
    while (bmp388.begin()) //Ayni islemi basinc sensorumuz icin yapiyoruz
    {
        Serial.println("Initialize error!");
        delay(1000);
    }
    delay(100);
    seaLevel = bmp388.readSeaLevel(525.0);
    Serial.print("seaLevel : ");
    Serial.print(seaLevel);
    Serial.println(" Pa");

    //1 HZ DOSYA GONDERIMI SETUP
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, sendTelemetry, true);
    timerAlarmWrite(timer, 4000000, true);
    timerAlarmEnable(timer);



}
void sendTelemetry() {

    createTelemetryFile();







}

void createDateTime() {

    dateTime += String(rtc.getDay());
    dateTime += "/";
    dateTime += String(rtc.getMonth());
    dateTime += "/";
    dateTime += String(rtc.getYear());
    dateTime += "-";
    dateTime += rtc.getTime();

}
void readGps() {

    while (ss.available() > 0)
        gps.encode(ss.read());



}




void listenRF() {

    uint8_t buf[VW_MAX_MESSAGE_LEN];
    uint8_t bufflen = VW_MAX_MESSAGE_LEN;

    if (vw_get_message(buf, &bufflen)) { // Check if a message has been received
        char str[bufflen + 1];
        for (uint8_t i = 0; i < bufflen; i++) {
            str[i] = (char)buf[i];
        }
        str[bufflen] = '\0';

        pressure2 = atof(str) * 100.0F;

    }
}


void Read_Altitude() //Yukseklik okuma komutumuz
{   

    /* Read the altitude */
    prevAltitude = altitude1;
    altitude1 = bmp388.readAltitude();
    altitude2 = 0; // CONTAINER YAZILINCA EKLENECEK
    altitudeDiff = abs(altitude1 - altitude2);
    Serial.print("Altitude : "); //Serial monitorde paket halinde gostermek istersek diye (09.01.23))
    Serial.print(altitude1);
    Serial.println(" m");
    delay(100);

}


void Read_Pressure() //Basinc okuma komutumuz
{
    pressure1 = bmp388.readPressure();
    
    Serial.print("Pressure : ");
    Serial.print(pressure1);
    Serial.println(" Pa");
}

void Read_Temperature()
{
    temperature = bmp388.readTemperature();
    Serial.print("Temperature : ");
    Serial.print(temperature);
    Serial.println(" C");

}

void Read_Gyro() //Ivmeolcer sensorumuzden aldigimiz veri (double'a gecisinde hata aldim, tekrar bak (12.01.23-14.01.23)) 
{
    sBmx160SensorData_t magn, gyro, accel;
    bmx160.getAllData(&magn, &gyro, &accel);

    //Once bunu dene olmazsa ikinci bir telemetri paketi kullanilabilir (13.01.23)

    roll = gyro.x; //gyro verileri 
    pitch = gyro.y;
    yaw = gyro.z;

    /* Display the gyroscope results (gyroscope data is in g) */
    Serial.print("G ");
    Serial.print("X: "); Serial.print(gyro.x); Serial.print("  ");
    Serial.print("Y: "); Serial.print(gyro.y); Serial.print("  ");
    Serial.print("Z: "); Serial.print(gyro.z); Serial.print("  ");
    Serial.println("g");

    Serial.println("");

    delay(500);
}

char *comma = ",";
void createTelemetryString() {

    telemetryString = String(packageNo) + comma + String(status) + comma + errorCode + comma + dateTime + comma + String(pressure1) + comma + String(pressure2) + comma + String(altitude1) + comma + String(altitude2) + comma + String(altitudeDiff)
        + comma + String(landingSpeed) + comma + String(temperature) + comma + String(batteryVoltage) + comma + String(gpsLatitude) + comma + String(gpsLongtitude) + comma + String(gpsAltitude) + comma + String(roll) + comma + String(pitch) + comma +
        String(yaw) + comma + String(teamNo);

   
}
File telemetryFile;

void createTelemetryFile() {

    telemetryFile = SD.open("telemetryFile.csv", FILE_APPEND);
    

    if (telemetryFile) {
        telemetryFile.println(telemetryString);
        telemetryFile.close();
        Serial.println("Telemetry file saved to SD card");
    }
    else {
        Serial.println("TELEMETRY FILE SD NOT WRITTEN");
    }

    telemetryFile.close();
   
}


void readTelemetry() {
    EEPROM.get(0, packageNo);//0 is the adress number of the packageNo
    Serial.print(packageNo);
    Serial.println(" . Telemetri Verileri");
    Read_Altitude();
    Serial.println(" ");
    Read_Pressure();
    Serial.println(" ");
    Read_Gyro();
    Serial.println(" ");
    createDateTime();
    Serial.println("***********");
}

void checkErrorARAS() {


    // 1- Model uydu iniş hızının 12-14 m/s dışındaki değerlerde olması durumunda
    if (landing && !seperated) {
        if(landingSpeed<12 || landingSpeed>14){
            errorCode[0] = '1';
        }
        else {
            errorCode[0] = '0';
        }
    }
    //2- Görev yükü iniş hızının 6-8 m/s dışındaki değerlerde olması durumunda

    if (landing && seperated) {
        if (landingSpeed < 6 || landingSpeed>8) {
            errorCode[1] = '1';
        }
        else {
            errorCode[1] = '0';
        }
    }

    //3 - Taşıyıcı basınç verisi alınamaması durumunda




    //4- Görev yükü konum verisi alınamaması durumunda


    //5 - Ayrılmanın gerçekleşmemesi durumunda,

    if (!seperated) {
        if (altitude1 < 400) {
            errorCode[4] = '1';
        }
        else {
            errorCode[4] = '0';
        }
    }







    


}

void loop()
{

    readTelemetry();
    
    
   
    // Nihayet Telemetri verilerini FTP'ye gonderiyoruz (18.01.23)
    ftp.ChangeWorkDir("C:/Users/metin/Desktop/FTP_TEST/TEST FOLDER");
    ftp.InitFile("TEST-PAYLOAD I");
    ftp.NewFile("telemetri.csv"); //CSV uzantisi ile kaydediyoruz (calismazsa .txt'yi kullan (18.01.23))
    unsigned char* data_ptr = (unsigned char*)telemetryString.c_str();
    ftp.WriteData(data_ptr, sizeof(telemetryString));
    
    EEPROM.put(0, ++packageNo);
   
    EEPROM.commit();
    dateTime = "";
    
    delay(1000); //Gonderdikten sonra 1 sn bekle
    ftp.CloseFile();

    //ftp.CloseConnection(); FTP baglantisini sonlandirir. Yer istasyonundan gelen komuta gore kullanim olabilir (19.01.23)
}
