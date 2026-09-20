#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// CHANGE THIS ID NUMBER FOR EACH DEVICE (CHOOSE 1 TO 5)
#define MY_DEVICE_ID 1 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

RF24 radio(4, 5);

const uint64_t BASE_ADDRESS = 0x7878787800LL;
const uint8_t PIPE_SUFFIXES[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

const int pinDot = 12;
const int pinDash = 14;
const int pinBuzzer = 13;

String currentMorse = "";
String currentWord = "";

struct MessagePacket {
  uint8_t senderID;
  char text[32];
};

MessagePacket incomingPacket;
MessagePacket outgoingPacket;

bool hasIncomingMessage = false;
unsigned long incomingMessageTime = 0;

unsigned long lastInputTime = 0;
bool inputPending = false;

const unsigned long letterTimeout = 1000;
const unsigned long sendTimeout = 3000;
const unsigned long displayIncomingTimeout = 5000;

unsigned long bothPressedStartTime = 0;
bool bothButtonsHeld = false;

int rapidDotCount = 0;
unsigned long lastDotTapTime = 0;
const unsigned long rapidTapWindow = 400;

struct MorseMap {
  const char* morse;
  char letter;
};

const MorseMap morseTable[] = {
  {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'}, {".", 'E'},
  {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'}, {"..", 'I'}, {".---", 'J'},
  {"-.-", 'K'}, {".-..", 'L'}, {"--", 'M'}, {"-.", 'N'}, {"---", 'O'},
  {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
  {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'}, {"-.--", 'Y'},
  {"--..", 'Z'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'},
  {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
  {"-----", '0'}
};
const int morseTableSize = sizeof(morseTable) / sizeof(MorseMap);

char decodeMorse(String morse) {
  for (int i = 0; i < morseTableSize; i++) {
    if (morse == morseTable[i].morse) {
      return morseTable[i].letter;
    }
  }
  return '?';
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (hasIncomingMessage) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("FROM PERSON ");
    display.print(incomingPacket.senderID);
    display.setTextSize(2);
    display.setCursor(0, 18);
    display.print(incomingPacket.text);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("ROOM (ID:");
    display.print(MY_DEVICE_ID);
    display.print(") WORD:");
    
    display.setTextSize(2);
    display.setCursor(0, 15);
    display.print(currentWord);
    
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print("Morse: ");
    display.print(currentMorse);
  }
  display.display();
}

void triggerBuzzer(int duration) {
  digitalWrite(pinBuzzer, HIGH);
  delay(duration);
  digitalWrite(pinBuzzer, LOW);
}

void playMarioTheme() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("MARIO JINGLE");
  display.display();
  int melody[] = {660, 660, 0, 660, 0, 510, 660, 0, 770};
  int durations[] = {100, 100, 100, 100, 100, 100, 100, 100, 100};
  for (int i = 0; i < 9; i++) {
    if (melody[i] == 0) {
      delay(durations[i]);
    } else {
      tone(pinBuzzer, melody[i], durations[i]);
      delay(durations[i] * 1.30);
    }
  }
  noTone(pinBuzzer);
  delay(1000);
}

void triggerHackerMode() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("HACKER MODE");
  display.display();
  for (int i = 100; i < 2000; i += 40) {
    tone(pinBuzzer, i, 10);
    delay(15);
  }
  noTone(pinBuzzer);
  delay(1500);
}

void triggerSOSEasterEgg() {
  for(int r = 0; r < 2; r++) {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(10, 20);
    display.print("ALERT!");
    display.display();
    for (int i = 0; i < 3; i++) { triggerBuzzer(100); delay(100); }
    display.clearDisplay();
    display.display();
    for (int i = 0; i < 3; i++) { triggerBuzzer(300); delay(100); }
  }
}

void setup() {
  pinMode(pinDot, INPUT_PULLUP);
  pinMode(pinDash, INPUT_PULLUP);
  pinMode(pinBuzzer, OUTPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }
  display.clearDisplay();
  display.display();

  radio.begin();
  radio.setPALevel(RF24_PA_MIN);
  
  for (int i = 1; i <= 5; i++) {
    uint64_t pipeAddress = BASE_ADDRESS | PIPE_SUFFIXES[i];
    radio.openReadingPipe(i, pipeAddress);
  }
  
  radio.startListening();
  updateDisplay();
}

void broadcastMessage() {
  radio.stopListening();
  outgoingPacket.senderID = MY_DEVICE_ID;
  memset(outgoingPacket.text, 0, sizeof(outgoingPacket.text));
  currentWord.toCharArray(outgoingPacket.text, sizeof(outgoingPacket.text));
  
  bool anySuccess = false;
  for (int i = 1; i <= 5; i++) {
    if (i == MY_DEVICE_ID) continue; 
    
    uint64_t targetAddress = BASE_ADDRESS | PIPE_SUFFIXES[i];
    radio.openWritingPipe(targetAddress);
    
    if(radio.write(&outgoingPacket, sizeof(outgoingPacket))) {
      anySuccess = true;
    }
  }
  
  currentWord = "";
  currentMorse = "";
  inputPending = false;
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 20);
  if(anySuccess) {
    display.print("Broadcast!");
  } else {
    display.print("No Room!"); 
  }
  display.display();
  delay(1200);
  
  radio.startListening();
  updateDisplay();
}

void loop() {
  unsigned long currentTime = millis();

  if (digitalRead(pinDot) == LOW && digitalRead(pinDash) == LOW) {
    if (!bothButtonsHeld) {
      bothPressedStartTime = currentTime;
      bothButtonsHeld = true;
    } else if (currentTime - bothPressedStartTime >= 2000) {
      playMarioTheme();
      currentWord = "";
      currentMorse = "";
      inputPending = false;
      bothButtonsHeld = false;
      radio.startListening();
      updateDisplay();
      while(digitalRead(pinDot) == LOW || digitalRead(pinDash) == LOW);
    }
    return;
  } else {
    bothButtonsHeld = false;
  }

  if (hasIncomingMessage && (currentTime - incomingMessageTime >= displayIncomingTimeout)) {
    hasIncomingMessage = false;
    updateDisplay();
  }

  if (!inputPending && radio.available()) {
    memset(&incomingPacket, 0, sizeof(incomingPacket));
    radio.read(&incomingPacket, sizeof(incomingPacket));
    if (incomingPacket.senderID != MY_DEVICE_ID && incomingPacket.senderID >= 1 && incomingPacket.senderID <= 5) {
      hasIncomingMessage = true;
      incomingMessageTime = currentTime;
      triggerBuzzer(60);
      delay(60);
      triggerBuzzer(60);
      updateDisplay();
    }
  }

  if (digitalRead(pinDot) == LOW) {
    if (!inputPending) {
      hasIncomingMessage = false;
    }
    if (currentTime - lastDotTapTime <= rapidTapWindow) {
      rapidDotCount++;
      if (rapidDotCount >= 10) {
        triggerHackerMode();
        rapidDotCount = 0;
        currentWord = "";
        currentMorse = "";
        inputPending = false;
        radio.startListening();
        updateDisplay();
        while(digitalRead(pinDot) == LOW);
        return;
      }
    } else {
      rapidDotCount = 1;
    }
    lastDotTapTime = currentTime;

    currentMorse += ".";
    lastInputTime = currentTime;
    inputPending = true;
    triggerBuzzer(80);
    updateDisplay();
    while(digitalRead(pinDot) == LOW);
    delay(50);
  }

  if (digitalRead(pinDash) == LOW) {
    if (!inputPending) {
      hasIncomingMessage = false;
    }
    currentMorse += "-";
    lastInputTime = currentTime;
    inputPending = true;
    triggerBuzzer(200);
    updateDisplay();
    while(digitalRead(pinDash) == LOW);
    delay(50);
  }

  if (inputPending) {
    if (currentMorse.length() > 0 && (currentTime - lastInputTime >= letterTimeout)) {
      char decoded = decodeMorse(currentMorse);
      if (decoded != '?') {
        currentWord += decoded;
        if (currentWord == "SOS") {
          triggerSOSEasterEgg();
          currentWord = "";
          currentMorse = "";
          inputPending = false;
          radio.startListening();
          updateDisplay();
          return;
        }
      }
      currentMorse = "";
      updateDisplay();
    }

    if (currentWord.length() > 0 && (currentTime - lastInputTime >= sendTimeout)) {
      broadcastMessage();
    }
  }
}
