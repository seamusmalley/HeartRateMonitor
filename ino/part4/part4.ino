#include <LiquidCrystal.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>

#define OUTLIER 0
#define VALID 1
#define DATA_REQUEST 2
#define SHOW 3
#define PAUSE 4
#define RESUME 5
#define ENV 6

#define RTC_ADDR 0x68

#define RUNNING 1
#define PAUSED 0

#define HEART_SENSOR_PIN A0
#define LED 13
#define BLINK_LED 8

LiquidCrystal lcd(12,11,5,4,3,2);

Adafruit_BMP085 bmp;

int STATE;
int bpm;
int temp;
int pressure;

struct datetime {
        int year;          
        int month;
        int day;
        int ss;
        int mm;
        int hh;
};

void setup(){
        STATE = RUNNING;  
  
        pinMode(7, OUTPUT);
        pinMode(BLINK_LED, OUTPUT);
  
        // setup serial to talk to C code
        Serial.begin(9600);
        while(!Serial);
        
        bmp.begin();
    
        // setup Wire to talk to RTC
        Wire.begin();        

        delay(3000);
        
        sync_clocks();
        //setTime(0, 20, 1, 10, 4, 2017);
        
        delay(2000);

        // setup LCD
        lcd.begin(16,2); 
        
        lcd.clear();
        lcd.print("100");    

        Serial.println("Clocks synced!");        
}

int lightState = LOW;
int bhbCount = 0;
double blinkingBpm = 0;
double doBlink = 0;

void blinkHeartBeat(){
  ++bhbCount;
  doBlink += 0.1;
  
  if(bhbCount == 20){
    bhbCount = 0;
    
    blinkingBpm = ((double)bpm) / 60.0 / 10.0;
  }
  
  if(doBlink >= blinkingBpm){
  if(lightState == LOW){
     lightState = HIGH; 
  }else{
    lightState = LOW;
  }
  doBlink = 0;
  }
  
  digitalWrite(BLINK_LED, lightState);
}

void loop(){    
  
        blinkHeartBeat();
  
        bpm = readHeartRate();
        temp = bmp.readTemperature();
        pressure = bmp.readPressure();

        if(STATE != PAUSED){
             lcd.clear();
	        lcd.print(bpm);  
        }
        
        if(Serial.available() > 0){
                byte inputByte = Serial.read();                                              

                switch(inputByte){
                        case OUTLIER: handleOutlier(); break;
                        case VALID: handleValid(); break;
                        case DATA_REQUEST: handleDataRequest(); break;
                        case SHOW: handleShow(); break;
                        case PAUSE: handlePause(); break;
                        case RESUME: handleResume(); break;
                        case ENV: handleEnvRequest(); break;
                }
        }

        delay(100);
}

void handleOutlier(){
        digitalWrite(7, HIGH);                
}

void handleValid(){
        digitalWrite(7, LOW);
}

void handleDataRequest(){                       
        struct datetime* dt = readTime();
        
        write_data(bpm, dt);
}

void handleEnvRequest(){
        struct datetime* dt = readTime();
        int env_reading = readFromEnvSensor();
        
        write_data(env_reading, dt);
}

void handleShow(){
        int showval = Serial.read();
        lcd.clear();
        lcd.print(showval);
        if(STATE == PAUSED){
              lcd.print(" (p)");
        }
}

void handlePause(){
        STATE = PAUSED;
  
        lcd.clear();
        lcd.print(bpm);
        lcd.print(" (p)");        
}

void handleResume(){
        STATE = RUNNING;
}

void write_data(int bpm, struct datetime* dt){
                
        Serial.print(bpm);
        Serial.print(" ");
                       
        Serial.print(dt->day);
        Serial.print(" ");
        
        Serial.print(dt->month);
        Serial.print(" ");
        
        Serial.print(dt->year);
        Serial.print(" ");
        
        Serial.print(dt->ss);
        Serial.print(" ");

        Serial.print(dt->mm);
        Serial.print(" ");

        Serial.println(dt->hh);
        
        Serial.flush();
}

int readHeartRate(){
        int heartRate = analogRead(HEART_SENSOR_PIN);  // read in heart rate
        heartRate = 60000/heartRate;        // convert to BPM

        return heartRate;
}

int readFromEnvSensor(){
        return temp;
}

void sync_clocks(){
        while(Serial.available() <= 0);        
        int day = Serial.read();
        while(Serial.available() <= 0);        
        int month = Serial.read();
        while(Serial.available() <= 0);        
        int year = Serial.read();
        while(Serial.available() <= 0);        
        int ss = Serial.read();
        while(Serial.available() <= 0);        
        int mm = Serial.read();
        while(Serial.available() <= 0);        
        int hh = Serial.read();
        
        setTime(ss, mm, hh, day, month, year);
}

void setTime(int second, int minute, int hour, int day, int month, int year){
        
        Wire.beginTransmission(RTC_ADDR);
        Wire.write((byte)0); // start at location 0
        Wire.write(bin2bcd(second));
        Wire.write(bin2bcd(minute));
        Wire.write(bin2bcd(hour));
        Wire.write(bin2bcd(0));
        Wire.write(bin2bcd(day));
        Wire.write(bin2bcd(month));
        Wire.write(bin2bcd(year - 2000));
        Wire.endTransmission();       
}

static uint8_t bcd2bin (uint8_t val) { return val - 6 * (val >> 4); }
static uint8_t bin2bcd (uint8_t val) { return val + 6 * (val / 10); }

struct datetime* readTime(){
        Wire.beginTransmission(RTC_ADDR);
        Wire.write((byte)0);
        Wire.endTransmission();

        Wire.requestFrom(RTC_ADDR, 7);
        int ss = bcd2bin(Wire.read() & 0x7F); 
        int mm = bcd2bin(Wire.read());
        int hh = bcd2bin(Wire.read());
        Wire.read();
        int d = bcd2bin(Wire.read());
        int m = bcd2bin(Wire.read());
        int y = bcd2bin(Wire.read()) + 2000;
        
        struct datetime* dt = (struct datetime*)malloc(sizeof(struct datetime));
        dt->month = m;
        dt->year = y;
        dt->day = d;
        dt->ss = ss;
        dt->mm = mm;
        dt->hh = hh;                
                
        return dt;               
}
