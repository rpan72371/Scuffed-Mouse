#include <Arduino.h>

const int UP_PIN = 2;
const int DOWN_PIN = 3;
const int LEFT_PIN = 4;
const int RIGHT_PIN = 5;
const int LEFT_CLICK_PIN = 6;
const int RIGHT_CLICK_PIN = 7;

void setup() {
  Serial.begin(115200);

  pinMode(UP_PIN, INPUT_PULLUP);
  pinMode(DOWN_PIN, INPUT_PULLUP);
  pinMode(LEFT_PIN, INPUT_PULLUP);
  pinMode(RIGHT_PIN, INPUT_PULLUP);
  pinMode(LEFT_CLICK_PIN, INPUT_PULLUP);
  pinMode(RIGHT_CLICK_PIN, INPUT_PULLUP);
}

void loop() {
  int up = digitalRead(UP_PIN) == LOW;
  int down = digitalRead(DOWN_PIN) == LOW;
  int left = digitalRead(LEFT_PIN) == LOW;
  int right = digitalRead(RIGHT_PIN) == LOW;
  int lc = digitalRead(LEFT_CLICK_PIN) == LOW;
  int rc = digitalRead(RIGHT_CLICK_PIN) == LOW;

  Serial.print("U:");
  Serial.print(up);

  Serial.print(",D:");
  Serial.print(down);

  Serial.print(",L:");
  Serial.print(left);

  Serial.print(",R:");
  Serial.print(right);

  Serial.print(",LC:");
  Serial.print(lc);

  Serial.print(",RC:");
  Serial.println(rc);

  delay(5);
}