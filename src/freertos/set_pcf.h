#include "PCF8574.h"
PCF8574 pcf1(0x20);
PCF8574 pcf2(0x21);
PCF8574 pcf3(0x22);

unsigned long cur_time_led, old_time_led;
unsigned long cur_time_pcf, old_time_pcf;
byte pin_led  = 13;
byte pin_wdt = 14;
bool stateLed = 0;

byte in[] = {0, 1, 2, 3, 4, 5, 6, 7};
const int jml_dt = 24;
byte dt_in1[8];
byte dt_in2[8];
byte dt_in3[8];

int c_dt1[8];
int c_dt2[8];
int c_dt3[8];

int buf_dt1[8];
int buf_dt2[8];
int buf_dt3[8];

int dt_pln[jml_dt];

void init_pcf() {
  for (int i = 0; i < sizeof(in); i++) {
    pcf1.pinMode(in[i], INPUT);
    pcf2.pinMode(in[i], INPUT);
    pcf3.pinMode(in[i], INPUT);
    Serial.print(in[i]);
    Serial.print(",");
  }
  Serial.println("Init pcf1...");
  if (pcf1.begin()) {
    Serial.println("OK");
  } else {
    Serial.println("KO");
  }

  Serial.println("Init pcf2...");
  if (pcf2.begin()) {
    Serial.println("OK");
  } else {
    Serial.println("KO");
  }

  Serial.println("Init pcf3...");
  if (pcf3.begin()) {
    Serial.println("OK");
  } else {
    Serial.println("KO");
  }
}

void baca_pcf() {
  //Serial.println("baca pcf");
  cur_time_pcf = millis();
  if (cur_time_pcf - old_time_pcf >= 30) {
    for (int i = 0; i < sizeof(in); i++) {
      dt_in1[i] = !pcf1.digitalRead(in[i]);
      if (dt_in1[i] == 1)c_dt1[i]++;
    }
    for (int i = 0; i < sizeof(in); i++) {
      dt_in2[i] = !pcf2.digitalRead(in[i]);
      if (dt_in2[i] == 1)c_dt2[i]++;
    }
    for (int i = 0; i < sizeof(in); i++) {
      dt_in3[i] = !pcf3.digitalRead(in[i]);
      if (dt_in3[i] == 1)c_dt3[i]++;
    }
    //Serial.println();
    old_time_pcf = millis();
  }

  cur_time_led = millis();
  if (cur_time_led - old_time_led >= 3000) {
    stateLed = !stateLed;
    //Serial.print("raw:");
    for (int i = 0; i < sizeof(in); i++) {
      buf_dt1[i] = (c_dt1[i] > 0) ? 1 : 0;
      //Serial.print(c_dt1[i]));
      //Serial.print(buf_dt1[i]);
      //Serial.print(",");
    }
    for (int i = 0; i < sizeof(in); i++) {
      buf_dt2[i] = (c_dt2[i] > 0) ? 1 : 0;
      //Serial.print(buf_dt2[i]);
      //Serial.print(",");
    }
    for (int i = 0; i < sizeof(in); i++) {
      buf_dt3[i] = (c_dt3[i] > 0) ? 1 : 0;
      //Serial.print(buf_dt3[i]);
      //Serial.print(",");
    }
    //Serial.println();

    Serial.print("urt:");
    dt_pln[0] = buf_dt1[0];
    dt_pln[1] = buf_dt1[1];
    dt_pln[2] = buf_dt1[2];
    dt_pln[3] = buf_dt1[3];

    dt_pln[4] = buf_dt1[7];
    dt_pln[5] = buf_dt1[6];
    dt_pln[6] = buf_dt1[5];
    dt_pln[7] = buf_dt1[4];

    dt_pln[8] = buf_dt2[0];
    dt_pln[9] = buf_dt2[1];
    dt_pln[10] = buf_dt2[2];
    dt_pln[11] = buf_dt2[3];

    dt_pln[12] = buf_dt2[7];
    dt_pln[13] = buf_dt2[6];
    dt_pln[14] = buf_dt2[5];
    dt_pln[15] = buf_dt2[4];

    dt_pln[16] = buf_dt3[7];
    dt_pln[17] = buf_dt3[6];
    dt_pln[18] = buf_dt3[5];
    dt_pln[19] = buf_dt3[4];

    dt_pln[20] = buf_dt3[0];
    dt_pln[21] = buf_dt3[1];
    dt_pln[22] = buf_dt3[2];
    dt_pln[23] = buf_dt3[3];

    for (int i = 0; i < jml_dt; i++) {
      Serial.print(dt_pln[i]);
      Serial.print(",");
    }
    Serial.println();
    for (int i = 0; i < sizeof(in); i++) {
      c_dt1[i] = 0;
      c_dt2[i] = 0;
      c_dt3[i] = 0;
    }
    digitalWrite(pin_led, stateLed);
    digitalWrite(pin_wdt, stateLed);
    old_time_led = millis();
  }
}
