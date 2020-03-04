

int SIGNAL_PIN = 4;

void setup() {
    Serial.begin(115200);
    pinMode(SIGNAL_PIN, INPUT);
}

void loop() {
    int pinValue = digitalRead(SIGNAL_PIN);
    Serial.print("signal pin is "); Serial.println(pinValue);
    delay(1000);
}