/*
   Copyright (c) 2022 Marcel Licence

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Dieses Programm ist Freie Software: Sie können es unter den Bedingungen
   der GNU General Public License, wie von der Free Software Foundation,
   Version 3 der Lizenz oder (nach Ihrer Wahl) jeder neueren
   veröffentlichten Version, weiter verteilen und/oder modifizieren.

   Dieses Programm wird in der Hoffnung bereitgestellt, dass es nützlich sein wird, jedoch
   OHNE JEDE GEWÄHR,; sogar ohne die implizite
   Gewähr der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
   Siehe die GNU General Public License für weitere Einzelheiten.

   Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
   Programm erhalten haben. Wenn nicht, siehe <https://www.gnu.org/licenses/>.
*/

/**
**/

#ifdef __CDT_PARSER__
#include <cdt.h>
#endif

#include "config.h"

#include <Arduino.h>

/*
   Library can be found on https://github.com/marcel-licence/ML_SynthTools
*/
#include <ml_epiano.h>
#include <ml_tremolo.h>

#ifdef REVERB_ENABLED
#include <ml_reverb.h>
#endif
#ifdef MAX_DELAY
#include <ml_delay.h>
#endif
#ifdef ML_CHORUS_ENABLED
#include <ml_chorus.h>
#endif
#ifdef OLED_OSC_DISP_ENABLED
#include <ml_scope.h>
#endif

#define ML_SYNTH_INLINE_DECLARATION
#include <ml_inline.h>
#undef ML_SYNTH_INLINE_DECLARATION

#include <LittleFS.h>

#include <FastLED.h>

ML_EPiano myRhodes;
ML_EPiano *rhodes = &myRhodes;

ML_Tremolo myTremolo(SAMPLE_RATE);
ML_Tremolo *tremolo = &myTremolo;

char shortName[] = "ML_Piano";

float mainVolume = 1.0f;

static bool isPlayingSong = false;
static bool isPlayedAllSongs = false;

File root;

void checkRootDir(File& aFile) 
{
  if(!aFile) {
    Serial.println("Failed to open root dir!");
  }
  if(!aFile.isDirectory()) {
    Serial.println("Not a directory!");
  }
}

String getNextMidiFileNameFromDir(File& aFile)
{
  File midiFile = aFile.openNextFile();

  if(!midiFile)
    return "NoMidiFile";
    
  Serial.print("MIDI file: ");
  String midiFileName = midiFile.name();
  Serial.println(midiFileName);
  midiFileName = "//" + midiFileName;  
  return midiFileName;
}

const uint8_t LED_PIN = 23;

void setup()
{
  /*
     this code runs once
  */
  delay(1500);

  pinMode(LED_PIN, OUTPUT);

  Serial.begin(SERIAL_BAUDRATE);

  Serial.println("Loading data...");
  Serial.println("Initialize Audio Interface\n");
  Audio_Setup();
  Midi_Setup();

#ifdef REVERB_ENABLED
  /*
     Initialize reverb
     The buffer shall be static to ensure that
     the memory will be exclusive available for the reverb module
  */
  // static float revBuffer[REV_BUFF_SIZE];
  static float *revBuffer = (float *)malloc(sizeof(float) * REV_BUFF_SIZE);
  Reverb_Setup(revBuffer);
#endif

#ifdef ML_CHORUS_ENABLED
  uint32_t chorus_len = 2048 * 2; // deep chorus
  Chorus_Init((int16_t *)malloc(sizeof(int16_t) * chorus_len), chorus_len / 2);
  Chorus_SetupDefaultPreset(0, 1.0f);
#endif

  Serial.printf("ESP.getFreeHeap() %d\n", ESP.getFreeHeap());
  Serial.printf("ESP.getMinFreeHeap() %d\n", ESP.getMinFreeHeap());
  Serial.printf("ESP.getHeapSize() %d\n", ESP.getHeapSize());
  Serial.printf("ESP.getMaxAllocHeap() %d\n", ESP.getMaxAllocHeap());

  /* PSRAM will be fully used by the looper */
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());

  Serial.printf("Firmware started successfully!\n");

  Core0TaskInit();

  App_SetVolume(0xFF, mainVolume);

  MidiStreamPlayer_Init();

  //Chorus_SetDelay(0xFF, 0.75f);
  //Chorus_SetDepth(0xFF, 0.25f);
  //Chorus_SetOutputLevel(0xFF, 0.8f);
  
  Serial.printf("Init LittleFS and getting available midi file!\n");
  
  root = LittleFS.open("/");
  // Insert multiple checks on the directory and midi files:
  checkRootDir(root);

  // Play the first song found in the directory!
  //String midiFileName = getNextMidiFileNameFromDir(root);
  //MidiStreamPlayer_PlayMidiFile_fromLittleFS((char*)midiFileName.c_str(), 1);
}

/*
   Core 0
*/
/* this is used to add a task to core 0 */
TaskHandle_t Core0TaskHnd;

inline void Core0TaskInit()
{
  /* we need a second task for the terminal output */
  xTaskCreatePinnedToCore(Core0Task, "CoreTask0", 8000, NULL, 999, &Core0TaskHnd, 0);
}

void Core0TaskSetup()
{
  /*
     init your stuff for core0 here
  */
  Serial.println("Starting core0 with FastLED!");
  setupXmasLights();
}

void Core0Task(void *parameter)
{
  Core0TaskSetup();

  while (true)
  {
      xmasLightsLoop(); 
  }
}

inline void playSongsInDir()
{
  if(!isPlayedAllSongs)
  {
    if(!isPlayingSong)
    {
      String midiFileName = getNextMidiFileNameFromDir(root);
      // Do not play anything if no midi file found:
      if (midiFileName == "NoMidiFile")
        isPlayedAllSongs = true;
      else {
        MidiStreamPlayer_PlayMidiFile_fromLittleFS((char*)midiFileName.c_str(), 1);
      }
    }
  }
}

void loop_1Hz()
{
  playSongsInDir();
}

inline void playMidiLoop()
{
  static int loop_cnt_1hz = 0; /*!< counter to allow 1Hz loop cycle */

  //playSongsInDir();

#ifdef SAMPLE_BUFFER_SIZE
  loop_cnt_1hz += SAMPLE_BUFFER_SIZE;
#else
  loop_cnt_1hz += 1; /* in case only one sample will be processed per loop cycle */
#endif
  if (loop_cnt_1hz >= SAMPLE_RATE)
  {
    loop_cnt_1hz -= SAMPLE_RATE;
    loop_1Hz();
  }

  Status_Process();

  /*
     MIDI processing
  */
  //Midi_Process();
  isPlayingSong = MidiStreamPlayer_Tick(SAMPLE_BUFFER_SIZE);

  /*
     And finally the audio stuff
  */
#if 1
  float mono[SAMPLE_BUFFER_SIZE], left[SAMPLE_BUFFER_SIZE], right[SAMPLE_BUFFER_SIZE];

  memset(left, 0, sizeof(left));
  memset(right, 0, sizeof(right));

  rhodes->Process(mono, SAMPLE_BUFFER_SIZE);

  /* reduce gain to avoid clipping */
  for (int n = 0; n < SAMPLE_BUFFER_SIZE; n++)
  {
    mono[n] *= mainVolume;
  }

#ifdef REVERB_ENABLED
  Reverb_Process(mono, SAMPLE_BUFFER_SIZE);
#endif
  /* mono to left and right channel */
  for (int i = 0; i < 48; i++)
  {
    left[i] += mono[i];
    right[i] += mono[i];
  }

#ifdef ML_CHORUS_ENABLED
  Chorus_Process_Buff(mono, left, right, SAMPLE_BUFFER_SIZE);
#else
  /* mono to left and right channel */
  for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++)
  {
    left[i] += mono[i];
    right[i] += mono[i];
  }
#endif

  tremolo->process(left, right, SAMPLE_BUFFER_SIZE);

  /*
     Output the audio
  */
  Audio_Output(left, right);

#endif

  Status_Process_Sample(SAMPLE_BUFFER_SIZE);

}

void loop()
{
  if(!isPlayedAllSongs)
    playMidiLoop();
  else
  {
    Serial.println("Done playing! Reset to play music again! :D");
    while(1)
    {
      digitalWrite(LED_PIN, HIGH);
      delay(500);
      digitalWrite(LED_PIN, LOW);
      delay(500);
    }
  }
}

/*
   MIDI callbacks
*/
bool subBase = false;

void App_NoteOn(uint8_t ch, uint8_t note, float vel)
{
  //Serial.printf("note on %d\n", note);
  rhodes->NoteOn(ch, note, vel);
  if (subBase && note <= 60)
  {
    rhodes->NoteOn(ch, note + 12, vel);
  }
}

void App_NoteOff(uint8_t ch, uint8_t note)
{
  rhodes->NoteOff(ch, note);
  if (subBase && note <= 60)
  {
    rhodes->NoteOff(ch, note + 12);
  }
}

void App_Sustain(uint8_t unused __attribute__((unused)), float val)
{
  rhodes->Sustain(val);
}

void App_PitchBend(uint8_t unused __attribute__((unused)), float val)
{
  rhodes->PitchBend(val);
}

void App_ModWheel(uint8_t unused __attribute__((unused)), float val)
{
  tremolo->setDepth(val);
}

void App_ModSpeed(uint8_t unused __attribute__((unused)), float val)
{
  tremolo->setSpeed(0.5 + val * 15);
}

#define PARAM_QUICK_DAMP_VALUE 0
#define PARAM_QUICK_DAMP_THRES 1
#define PARAM_MODULATION_DEPTH 2
#define PARAM_TREMOLO_DEPTH 3
#define PARAM_TREMOLO_SHIFT 4
#define PARAM_TREMOLO_SPEED 5
#define PARAM_SOUND_C1 6
#define PARAM_SOUND_C2 7

void App_SetVolume(uint8_t unused, float val)
{
  mainVolume = val;
}

void App_ModParam(uint8_t param, float val)
{
  switch (param)
  {
  case PARAM_QUICK_DAMP_VALUE:
    rhodes->SetQuickDamping(val);
    break;
  case PARAM_QUICK_DAMP_THRES:
    rhodes->SetQuickDampingThr(val);
    break;
  case PARAM_MODULATION_DEPTH:
    rhodes->SetModulationDepth(val);
    break;
  case PARAM_TREMOLO_SHIFT:
    tremolo->setPhaseShift(val);
    break;
  case PARAM_TREMOLO_DEPTH:
    tremolo->setDepth(val);
    break;
  case PARAM_TREMOLO_SPEED:
  {
    float speed = 6.5f * 0.5f;
    speed += val * 6.5f * 3.0f;
    tremolo->setSpeed(speed);
  }
  break;
  case PARAM_SOUND_C1:
    rhodes->SetCurve(val);
    break;
  case PARAM_SOUND_C2:
    rhodes->SetCurve2(val);
    break;
  }
}

#define PIANO_SWITCH_SUB_BASE 4

void App_ModSwitch(uint8_t param, float val)
{
  if (val > 0)
  {
    switch (param)
    {
    case 0:
      rhodes->SetCenterTuneA(220);
      break;
    case 1:
      rhodes->SetCenterTuneA(440);
      break;
    case 2:
      rhodes->SetCenterTuneA(444);
      break;
    case 3:
      rhodes->SetCenterTuneA(880);
      break;
    case PIANO_SWITCH_SUB_BASE:
      subBase = !subBase;
      break;
    case 5:
      rhodes->CalcCurvePreset1();
      break;
    case 6:
      rhodes->SetCurve(0);
      rhodes->SetCurve2(0);
      break;
    case 7:
      rhodes->SetCurve(0);
      rhodes->SetCurve2(0.5f);
      break;
    }
  }
}

inline void Overdrive_SetDrive(uint8_t unused __attribute__((unused)), float value)
{
}

inline void Delay_SetOutputLevelInt(uint8_t unused __attribute__((unused)), uint8_t value)
{
}

inline void Delay_SetFeedbackInt(uint8_t unused __attribute__((unused)), uint8_t value)
{
}
