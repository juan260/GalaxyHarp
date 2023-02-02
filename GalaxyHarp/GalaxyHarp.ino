#include <FastLED.h>
#include <MIDI.h>

#define NUM_LEDS_PER_STRIP 60
#define DATA_PIN 3
#define NUM_SENSORS_AND_STRIPS 5
#define MAX_LED_ANIMATIONS 50



// Define the array of leds
CRGB leds[NUM_SENSORS_AND_STRIPS][NUM_LEDS_PER_STRIP];
CRGB debugLED;
CRGB black = CRGB(0,0,0);
CRGB white = CRGB(255,255,255);
int sensor_pins[NUM_SENSORS_AND_STRIPS];
int trigger_pins[NUM_SENSORS_AND_STRIPS];
float distanceReadings[NUM_SENSORS_AND_STRIPS];
float smoothenedDistanceReadings[NUM_SENSORS_AND_STRIPS];
float distanceSmoothening = 0.95;
const int ledDataPins[] = {12, 11, 10, 9, 8, 7, 6, 5};
int distanceThreshold;
int animationInterval = 20;
int handDetected[NUM_SENSORS_AND_STRIPS];
int midiNotesPressed[NUM_SENSORS_AND_STRIPS];
int listHead = 0;
int LEDStepsUntilNoteOff = 3;
int midiChannel = 1;
int debug_prevTimeReading = 0;
int sensorTimeout;
int standardSensorTimeout = 2300;
int higherSensorTimeout = 2500;
int currentProgram = 0;
int prog2FadeTime = 2000;
int midiChannelToSend = 3;
int prev_minDistanceReading = 0;
int verbose = 2;
int last_verbose_hand_reading = 0;
float minDistanceReading = 0.0;
int minimumHand = -1;
int msUntilHandIsGone = 500;
int prog5MidiStates[NUM_SENSORS_AND_STRIPS];
int CCstates[NUM_SENSORS_AND_STRIPS];
int prog6LastNote = 0;
int listenChannel = 1;
int listenReturnChannel = 2;


typedef struct {
  int millisecond_started;
  int strip;
  int led;
  CRGB color;
  int midiNote;
  int programNumber;
} LEDAnimation;

int animations_length=0;
LEDAnimation animations[MAX_LED_ANIMATIONS];


MIDI_CREATE_DEFAULT_INSTANCE();

void colorWholeStrip(int index, CRGB color){
  for(int j=0;j<NUM_LEDS_PER_STRIP;j++){  
        setLed(index, j,color);
  }
}
void turnOffWholeStrip(int index){
  colorWholeStrip(index, black);
}
void animations_delete(int index){
  if(verbose) Serial.println("Deleting animation " + String(index) +".");
  if(index<animations_length && index>=0){
    int i;

    if(animations[index].programNumber == 4){
      for(int k= 0; k<NUM_SENSORS_AND_STRIPS;k++){
        turnOffWholeStrip(k);
      }
    } else if(animations[index].led<0){
      if(verbose) Serial.println("Deleting the whole strip " + String(index) +".");
      
      turnOffWholeStrip(animations[index].strip);
      
    }else{
      setLed(animations[index].strip,animations[index].led, black);
    } 
    FastLED.show();
    for(i=index;i<animations_length-1;i++){
      animations[i] = animations[i+1];
    }
    animations_length--;
  }

}

int obtainNoteToPlay(int strip){
  for(int i = 0; i<NUM_SENSORS_AND_STRIPS ; i++){
    if(strip+i<NUM_SENSORS_AND_STRIPS){
      if(midiNotesPressed[strip+i]){
        // We found a note!
        return midiNotesPressed[strip+i];
        break;
      }
    }else if(strip-i>=0){
        if(midiNotesPressed[strip-i]){
        // We found a note!
        return midiNotesPressed[strip-i];
        break;
      }    
    } else {
      return 0;
    }
  }
  return 0;
}

void animations_new(int millisecond_started, int strip, int distance, 
  int ignore_note = 0, int inProgram = -1, int sendNoteOn = 1){
  int program;
  if(inProgram>=0){
    program = inProgram;
  } else {
    program = currentProgram;
  }
  // TODO: Read also CC message 64 sustain pedal
  if(program == 4 && sendNoteOn){
      for(int i=0; i<NUM_SENSORS_AND_STRIPS;i++){
        if(midiNotesPressed[i]>0){
          usbMIDI.sendNoteOn(midiNotesPressed[i], 127-distance*2, midiChannel);
        }
    }
    return;
  }  

  if((program == 3 || program == 6) && midiNotesPressed[0]>0 && sendNoteOn){
    usbMIDI.sendNoteOn(midiNotesPressed[0], 127-distance*2, midiChannel);
  }

  if(program == 5){
    return;
  }
  // Look for a midi note to play
  if(verbose) Serial.println("Creating animation " + strip);
  int midiNote = obtainNoteToPlay(strip);
  if( (midiNote > 0 || ignore_note) ){
  
    if(animations_length>=MAX_LED_ANIMATIONS){
      animations_delete(0);
    }
    
    LEDAnimation anim = {millisecond_started, strip, -1, CRGB(random(0,255),random(0,255),random(0,255)), midiNote, program};
    animations[animations_length] = anim;
    animations_length++;
  
  //setLed(strip,0, anim.color);
  //FastLED.show()
    if(midiNote>0) usbMIDI.sendNoteOn(midiNote, 127-distance*2, midiChannel);
  }
 }

int setLed(int strip, int led, CRGB color){
  if(strip>=0 && strip<NUM_SENSORS_AND_STRIPS && led>=0 && led< NUM_LEDS_PER_STRIP){
      if(verbose>3) Serial.println("LED Set: " + String(strip) + " " + String(led));
      leds[strip][led] =  color;
      return 1;
    } else {
      Serial.println("WARNING: couldn't set LED " + String(led) + " of string " + String(strip) +".");
      return 0;
    }
}
// Updated the LED animations and sends back MIDI data when necessary
void updateAnimations(){
  int current_time = millis();
  int i, elapsedTime, stepsForward;
  float brightness = 1.0;
  int is_there_an_animation_that_triggers_all_LED_strips = -1;
  CRGB newColor = updateDistanceAnimations();
  // For each animation
  for(i=animations_length-1;i>=0;i--){
    elapsedTime = current_time - animations[i].millisecond_started;
    switch(animations[i].programNumber){
      case 0: // Program 1
        stepsForward = elapsedTime / animationInterval;
        if(stepsForward>=NUM_LEDS_PER_STRIP){
          animations_delete(i);
          continue;
        }
        
        if(stepsForward > animations[i].led){
          setLed(animations[i].strip,stepsForward, animations[i].color);
          setLed(animations[i].strip,animations[i].led,black);
          animations[i].led = stepsForward;
          if(stepsForward > LEDStepsUntilNoteOff && animations[i].midiNote != 0){
            usbMIDI.sendNoteOff(animations[i].midiNote, 1, midiChannel);
            animations[i].midiNote = 0;
          }
        }
        break;

      case 1: // Program 2
        

        if(elapsedTime>prog2FadeTime){
          usbMIDI.sendNoteOff(animations[i].midiNote, 1, midiChannel);
          animations_delete(i);
        } else {
          brightness = (1-float(elapsedTime)/prog2FadeTime);
        }
        break;

      case 2: // Program 3
        if(handDetected[animations[i].strip]){
          if(distanceReadings[animations[i].strip]!=0){
            int selectedLed = NUM_LEDS_PER_STRIP - (smoothenedDistanceReadings[animations[i].strip]\
            * NUM_LEDS_PER_STRIP)/ distanceThreshold;

              if(selectedLed != animations[i].led){
                //Serial.println(smoothenedDistanceReadings[animations[i].strip]);
                setLed(animations[i].strip,selectedLed, animations[i].color);
                setLed(animations[i].strip,animations[i].led, black);
                animations[i].led = selectedLed; 
              }
              /*usbMIDI.sendAfterTouchPoly(animations[i].midiNote, 
                round((1-(smoothenedDistanceReadings[animations[i].strip])/distanceThreshold)*127),
                midiChannelToSend);*/
                usbMIDI.sendAfterTouch(
                round((1-(smoothenedDistanceReadings[animations[i].strip])/distanceThreshold)*127),
                midiChannelToSend);
          } 
          
        } else {
          usbMIDI.sendNoteOff(animations[i].midiNote, 1, midiChannel);
          animations_delete(i);
        }
        break;
        
      
      case 3: // Program 4
      case 6: // Program 7
        if(handDetected[animations[i].strip]){
          if(distanceReadings[animations[i].strip]!=0){
            /*brightness = (1.0 - (smoothenedDistanceReadings[animations[i].strip])/ \
                distanceThreshold);*/

              /*usbMIDI.sendAfterTouch( 
                round((smoothenedDistanceReadings[animations[i].strip] * 127)/distanceThreshold), 
                midiChannelToSend);*/

            /*if(currentProgram==5)
              usbMIDI.sendControlChange(20+i, 
              round((smoothenedDistanceReadings[animations[i].strip] * 127)/distanceThreshold),
              midiChannelToSend);*/
              colorWholeStrip(animations[i].strip, newColor);
            }
        } else {
          usbMIDI.sendNoteOff(animations[i].midiNote, 1, midiChannel);
          animations_delete(i);
        }
        break;                                                                                                                                                                                                                                            
    }
  
    if(currentProgram == 1 ){
      if(brightness != 1.0 && brightness>0.0){
        colorWholeStrip(animations[i].strip, CRGB(pow(brightness,(5))*255));
      } 
    }
  }
  
  FastLED.show();
}

CRGB updateDistanceAnimations(){
  if(currentProgram == 4 || currentProgram == 3 || currentProgram == 1 || currentProgram == 6){
    float brightness = 1.0;
    
    // Find Minimum Distance reading
    int dummyMinDistanceReading = distanceThreshold;
    int j, areAnyNotesPressed=0;
    for(int i=0;i<NUM_SENSORS_AND_STRIPS;i++){
      if(midiNotesPressed[i]>0){
        areAnyNotesPressed =1;
        break;
      }
    }
    if(areAnyNotesPressed || currentProgram ==6){ 
      for(j=0; j<NUM_SENSORS_AND_STRIPS; j++){
        if(handDetected[j] && distanceReadings[j] < dummyMinDistanceReading && distanceReadings[j]>1){ 
          dummyMinDistanceReading = distanceReadings[j];
          minimumHand = j;
        }
      }
    }
    if(minimumHand != -1 || areAnyNotesPressed == 0)
     minDistanceReading = dummyMinDistanceReading * (1-distanceSmoothening) + minDistanceReading * distanceSmoothening;
    
    //CRGB newColor = white / uint8_t(round((distanceThreshold/minDistanceReading)*(1-smoothingFactor) + prev_minDistanceReading*smoothingFactor));
    //brightness = round(255*pow((distanceThreshold/minDistanceReading)*(1-smoothingFactor) + prev_minDistanceReading*smoothingFactor, 5));
    
    brightness = 255 * pow(1.0 - (float(minDistanceReading)/ distanceThreshold), 5);
    
      CRGB newColor = CRGB(uint8_t(brightness), uint8_t(brightness), uint8_t(brightness));
      //Serial.println(String(uint8_t(brightness)) + " " + String(minDistanceReading) + " " + String(distanceThreshold));
     if(currentProgram == 4 || currentProgram == 6){ 
      for(j=0; j<NUM_SENSORS_AND_STRIPS;j++){
        colorWholeStrip(j, newColor);
      }
    }
    if(currentProgram == 6){
      int newMidiNoteToPlay = round((1.0 - (float(minDistanceReading)/ distanceThreshold))*24) +60;
      if(abs(distanceThreshold - minDistanceReading) < 1){
        if(prog6LastNote != 0){
          usbMIDI.sendNoteOff(prog6LastNote, 100, midiChannelToSend);
          prog6LastNote = 0;
        }
      } else if(newMidiNoteToPlay != prog6LastNote){
        usbMIDI.sendNoteOn(newMidiNoteToPlay, 100, midiChannelToSend);
        usbMIDI.sendNoteOff(prog6LastNote, 100, midiChannelToSend);
        prog6LastNote = newMidiNoteToPlay;
        
      }
      Serial.println(String(minDistanceReading) + " " + String(newMidiNoteToPlay) + " " + String(minimumHand) + " " +String(prog6LastNote) + " " + distanceReadings[0]);
      usbMIDI.sendAfterTouch(round(pow(1.0 - (float(minDistanceReading)/ distanceThreshold), 0.1)*127), 
       midiChannelToSend);
    } else {
      //Serial.println(minDistanceReading);
      //Serial.println(distanceThreshold);
      if(areAnyNotesPressed)
        usbMIDI.sendAfterTouch(
          round((1.0 - (float(minDistanceReading)/ distanceThreshold))*127),
          midiChannelToSend);
      prev_minDistanceReading = minDistanceReading;
    }

    /*Serial.println(String(minimumHand)+ " " + 
    String(minDistanceReading) + " " + 
    distanceThreshold +
    " " + String(smoothenedDistanceReadings[minimumHand]) + " " + 
    String(distanceReadings[minimumHand]) + " " + 
    String(round((1.0 - (float(minDistanceReading)/ distanceThreshold))*127)));*/
  FastLED.show();
  return newColor;
  }
  return CRGB();
  
}

void handleNoteOn(byte inChannel, byte inNote, byte inVelocity){
  if(inChannel != listenChannel && inChannel != listenReturnChannel){
    if(verbose) Serial.println("REJ Note In " + String(inNote) + " " + String(inChannel));
    return;
  }
  if(verbose) Serial.println("Note In " + String(inNote) + " " + String(inChannel));
  if(inChannel == listenReturnChannel){
    animations_new(millis(), inNote-60, 4, 1, 0, 0);
  }else {
    if(currentProgram == 3 ||currentProgram == 6){
      usbMIDI.sendNoteOn(inNote, inVelocity, midiChannelToSend);
      if(midiNotesPressed[0]>0)
        usbMIDI.sendNoteOff(midiNotesPressed[0], 1, midiChannelToSend);
      midiNotesPressed[0] = inNote;
    } else{
      midiNotesPressed[listHead] = inNote;
      if(++listHead==NUM_SENSORS_AND_STRIPS){
        listHead=0;
      }
    }
  }
}

void handleNoteOff(byte inChannel, byte inNote, byte inVelocity){
  if(inChannel != listenChannel){
    if(verbose) Serial.println("REJ Note Off " + String(inNote) + " " + String(inChannel));
    return;
  }
  int i = 0;
  if(verbose) Serial.println("Note Off " + String(inNote));
  if(currentProgram == 3 || currentProgram == 6){
    if(inNote == midiNotesPressed[0]){
      usbMIDI.sendNoteOff(midiNotesPressed[0], 0, midiChannelToSend);
      midiNotesPressed[0] = 0;
    }

  }else{
    for(; i < NUM_SENSORS_AND_STRIPS ; i++){
      if(inNote == midiNotesPressed[i]){
        midiNotesPressed[i]=0;
      }
    }

    if(currentProgram==4){
      usbMIDI.sendNoteOff(inNote, 1, midiChannel);
    }
  }
}

void handleControlChange(byte inChannel, byte controlNo, byte value){
  if(inChannel==2 && controlNo>=102 && controlNo < 102 + NUM_SENSORS_AND_STRIPS){
    
    
    controlNo = controlNo-102;
    if(value>100 && CCstates[controlNo]==0){
      CCstates[controlNo] = 1;
      animations_new(millis(), controlNo, 1, 1, value, 0);
      if(verbose) Serial.println("CC " + String(controlNo) + " " + String(value));
    } else if(value <10 && CCstates[controlNo]==1) {
      CCstates[controlNo] = 0;
      if(verbose) Serial.println("CC " + String(controlNo) + " " + String(value));
    }
    
  } else {
    if(verbose) Serial.println("REJ CC " + String(controlNo) + " " + String(value));
  }

}

void handleProgramChange(byte inChannel, byte program){
  if(verbose) Serial.println("Program Change: " + String(program));
  if(currentProgram==5){
    for(int i=MAX_LED_ANIMATIONS-1;i>=0;i--){
      if(animations[i].programNumber == 5){
        animations_delete(i);
      }
    }
  }
  currentProgram = program;
  switch(currentProgram){
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
      sensorTimeout = higherSensorTimeout;
      break;
    default:
    sensorTimeout = standardSensorTimeout;
  } 

  distanceThreshold = round(sensorTimeout * 0.034 /2);
  minDistanceReading = distanceThreshold;

  if(currentProgram == 6){
    for(int i = 1; i < NUM_SENSORS_AND_STRIPS ; i++){
      distanceReadings[i] = 0;
    }
  }
 
}


void fireTriggersReadFromSensorsFireAnimations(){

  int i;
  //Serial.print("firetriggers... : ");
  //for(i=0;i<NUM_SENSORS_AND_STRIPS;i++){
  for(i=NUM_SENSORS_AND_STRIPS-1;i>=0;i--){
    if(currentProgram != 6 || i==0 || i==NUM_SENSORS_AND_STRIPS-1){
      digitalWrite(trigger_pins[i], LOW);
      delayMicroseconds(2);
      digitalWrite(trigger_pins[i], HIGH);
      delayMicroseconds(10);
      digitalWrite(trigger_pins[i], LOW);
      distanceReadings[i] = pulseIn(sensor_pins[i], HIGH, sensorTimeout);
      distanceReadings[i] = distanceReadings[i] * 0.034 / 2.0;
      smoothenedDistanceReadings[i] = distanceReadings[i] * (1-distanceSmoothening) + smoothenedDistanceReadings[i] * distanceSmoothening;
      updateAnimations();
      //Serial.print(String(distanceReadings[i]) + " " + smoothenedDistanceReadings[i] + " || ");
    } 
    // INTERRUPT
    //Serial.print(String(distanceReadings[i])+ " ");
  }
  Serial.println(String(distanceReadings[0]) + " " + smoothenedDistanceReadings[0]);
  //Serial.print(String("            "));
  //  Serial.println("");
  
}

void fireSetupAnimation(){
  for(int i=0;i<NUM_SENSORS_AND_STRIPS;i++){
    animations_new(millis() +i*1000, i, 50, 1);
  }
}

void setup() {
  Serial.begin(9600);
  int i;
  if(verbose) Serial.print("Setting up pins ");
  for(i=0;i<NUM_SENSORS_AND_STRIPS;i++){
    if(verbose) Serial.print("Strip " + String(i) +" ");
    trigger_pins[i] = 14 + 2*i;
    sensor_pins[i] = 15 + 2*i;
    
    //ledDataPins[i] = 12;
    
    pinMode(trigger_pins[i], OUTPUT);  
    pinMode(sensor_pins[i], INPUT);  
    handDetected[i]=0;
    midiNotesPressed[i] = 0;
    prog5MidiStates[i]=0;
    CCstates[i] = 0;
  }
  if(verbose) Serial.print("Setting up LEDS. ");
  FastLED.addLeds<WS2812B, 12, GBR>(leds[0], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 11, GBR>(leds[1], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 10, GBR>(leds[2], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 9, GBR>(leds[3], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 8, GBR>(leds[4], NUM_LEDS_PER_STRIP);
  if(verbose) Serial.print("Setting up MIDI. ");
  usbMIDI.setHandleNoteOn(handleNoteOn);
  usbMIDI.setHandleNoteOff(handleNoteOff);
  usbMIDI.setHandleProgramChange(handleProgramChange);
  usbMIDI.setHandleControlChange(handleControlChange);
  
  MIDI.begin(4);

  sensorTimeout = standardSensorTimeout;
  distanceThreshold = round(sensorTimeout * 0.034 /2);
  if(verbose) Serial.print("Firing animation. ");
  fireSetupAnimation();
  if(verbose) Serial.println("Setup Completed");
}


void loop() {
  // put your main code here, to run repeatedly:
  fireTriggersReadFromSensorsFireAnimations();
  int i;
  
  for(i=0;i<NUM_SENSORS_AND_STRIPS;i++){
    if(distanceReadings[i]>=0.5){
      if(handDetected[i] == 0){
        if(verbose) Serial.println("Hand " + String(i) + "detected");
        animations_new(millis(), i, distanceReadings[i]);
        analogWrite(13, HIGH);
        if(currentProgram==5){
          if(prog5MidiStates[i]==0){
            usbMIDI.sendControlChange(20+i, 127, midiChannelToSend);
            prog5MidiStates[i]=127;
          } else {
            usbMIDI.sendControlChange(20+i, 0, midiChannelToSend);
            prog5MidiStates[i]=0;
          }
        }
      }
      handDetected[i] = millis();
      
    } else if(handDetected[i] != 0 && millis() > unsigned(handDetected[i]+msUntilHandIsGone)){
        handDetected[i] = 0;
        analogWrite(13, LOW);
        if(verbose) Serial.println("Hand " + String(i) +" Lost");
        if(currentProgram == 4){
          int anyHandDetected = 0;
          for(int j = 0; j<NUM_SENSORS_AND_STRIPS;j++){
            if(handDetected[j]) anyHandDetected = 1;
          }
          if(! anyHandDetected) {
            for(int j = 0;j<NUM_SENSORS_AND_STRIPS;j++){
              if(midiNotesPressed[j]>0) usbMIDI.sendNoteOff(midiNotesPressed[j], 0, midiChannel); 
            }
          }
        }
       /* if(currentProgram==5){
          usbMIDI.sendControlChange(20+i, 0, midiChannelToSend);
        }*/
    }
  }
  /*if(verbose>1 && millis()>unsigned(last_verbose_hand_reading) + 1000){
    last_verbose_hand_reading = millis();
    Serial.print("Hand Readings: ");
    for(i=0;i<NUM_SENSORS_AND_STRIPS; i++){
      Serial.print(" " + String(distanceReadings[i]));
    }
    Serial.print("\n");
  }
*/

  usbMIDI.read();
  MIDI.read();
}
