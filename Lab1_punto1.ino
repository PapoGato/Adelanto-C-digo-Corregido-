#include <driver/adc.h>
#include <driver/dac.h>
#include <math.h>

// ==================== DEFINICIÓN DE PINES ====================
#define ADC_CHANNEL ADC1_CHANNEL_6
#define ADC_PIN 34
#define DAC_CHANNEL DAC_CHANNEL_1
#define DAC_PIN 25
#define POT_CHANNEL ADC1_CHANNEL_7
#define POT_PIN 35
#define BUTTON_PIN 26
#define BUTTON_DEBOUNCE 50

// ==================== VARIABLES GLOBALES ====================
bool generatorEnabled = true;
bool oscilloscopeEnabled = true;
bool buttonState = false;
bool lastButtonState = false;
unsigned long lastDebounceTime = 0;

// Parámetros del generador
enum WaveType { SINE, SQUARE, TRIANGLE, SAWTOOTH };
WaveType currentWave = SINE;
float frequency = 1.0;
float amplitude = 1.0;
int dacOffset = 128;

// Variables para plotter
int currentDACValue = 0;
int currentADCValue = 0;
float currentDACVoltage = 0.0;
float currentADCVoltage = 0.0;
int currentPotValue = 0;
float currentPotPercentage = 0.0;

unsigned long lastPlotTime = 0;
unsigned long plotInterval = 20;

bool verboseMode = false;

void setup() {
  Serial.begin(115200);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(POT_CHANNEL, ADC_ATTEN_DB_11);
  
  dac_output_enable(DAC_CHANNEL);
  
  delay(1000);
  
  Serial.println("=== SERIAL PLOTTER - ESP32 OSCILLOSCOPE ===");
  Serial.println("Comandos: g o w f# a# v s h");
  Serial.println("===========================================");
  delay(2000);
}

void loop() {
  processSerialCommands();
  handleButton();
  readPotentiometer();
  
  // Generar señal DAC
  if (generatorEnabled) {
    generateWaveform();
  } else {
    currentDACValue = dacOffset;
    dac_output_voltage(DAC_CHANNEL, currentDACValue);
  }
  
  // Muestrear ADC
  if (oscilloscopeEnabled) {
    currentADCValue = adc1_get_raw(ADC_CHANNEL);
  } else {
    currentADCValue = 0;
  }
  
  // Calcular voltajes
  currentDACVoltage = (currentDACValue / 255.0) * 3.3;
  currentADCVoltage = (currentADCValue / 4095.0) * 3.3;
  
  // Enviar datos al Serial Plotter
  unsigned long currentTime = millis();
  if (currentTime - lastPlotTime >= plotInterval) {
    lastPlotTime = currentTime;
    sendPlotterData();
  }
  
  delayMicroseconds(100);
}

void generateWaveform() {
  static unsigned long lastUpdate = 0;
  static float phase = 0.0;
  
  unsigned long currentTime = micros();
  float deltaTime = (currentTime - lastUpdate) / 1000000.0;
  lastUpdate = currentTime;
  
  phase += 2.0 * PI * frequency * deltaTime;
  if (phase > 2.0 * PI) {
    phase -= 2.0 * PI;
  }
  
  float value = 0.0;
  
  switch (currentWave) {
    case SINE:
      value = sin(phase);
      break;
      
    case SQUARE:
      value = (phase < PI) ? 1.0 : -1.0;
      break;
      
    case TRIANGLE:
      if (phase < PI) {
        value = -1.0 + (2.0 * phase / PI);
      } else {
        value = 3.0 - (2.0 * phase / PI);
      }
      break;
      
    case SAWTOOTH:
      value = -1.0 + (phase / PI);
      break;
  }
  
  currentDACValue = dacOffset + (int)(value * amplitude * 127);
  currentDACValue = constrain(currentDACValue, 0, 255);
  
  dac_output_voltage(DAC_CHANNEL, currentDACValue);
}

void readPotentiometer() {
  static unsigned long lastRead = 0;
  
  if (millis() - lastRead > 50) {  // Leer cada 50ms para mejor respuesta
    lastRead = millis();
    currentPotValue = adc1_get_raw(POT_CHANNEL);
    currentPotPercentage = (currentPotValue / 4095.0) * 100.0;
    amplitude = currentPotValue / 4095.0;
  }
}

// ==================== MANEJO DE PULSADOR ====================
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > BUTTON_DEBOUNCE) {
    if (reading != buttonState) {
      buttonState = reading;
      
      if (buttonState == LOW) {
        generatorEnabled = !generatorEnabled;
        if (verboseMode) {
          Serial.print("# Generador:");
          Serial.println(generatorEnabled ? "ON" : "OFF");
        }
      }
    }
  }
  
  lastButtonState = reading;
}

// ENVÍO DE DATOS AL PLOTTER
void sendPlotterData() {
  // Formato con etiquetas para Serial Plotter
  // Arduino IDE reconoce automáticamente "Etiqueta:Valor"
  
  Serial.print("DAC_Output:");
  Serial.print(currentDACVoltage, 3);
  Serial.print(",");
  
  Serial.print("ADC_Input:");
  Serial.print(currentADCVoltage, 3);
  Serial.print(",");
  
  Serial.print("Potentiometer:");
  Serial.print(currentPotPercentage, 1);
  Serial.print(",");
  
  // Línea de referencia de 1.65V (punto medio)
  Serial.print("Reference_1.65V:");
  Serial.print(1.65);
  Serial.print(",");
  
  // Límites superior e inferior
  Serial.print("Max_3.3V:");
  Serial.print(3.3);
  Serial.print(",");
  
  Serial.print("Min_0V:");
  Serial.println(0.0);
}

// COMANDOS SERIALES
void processSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case 'g':
      case 'G':
        generatorEnabled = !generatorEnabled;
        printStatus("Generador", generatorEnabled ? "ON" : "OFF");
        break;
        
      case 'o':
      case 'O':
        oscilloscopeEnabled = !oscilloscopeEnabled;
        printStatus("Osciloscopio", oscilloscopeEnabled ? "ON" : "OFF");
        break;
        
      case 'w':
      case 'W':
        currentWave = (WaveType)((currentWave + 1) % 4);
        printWaveType();
        break;
        
      case 'f':
      case 'F':
        frequency = Serial.parseFloat();
        frequency = constrain(frequency, 0.1, 100.0);
        printStatus("Frecuencia", String(frequency, 2) + "Hz");
        break;
        
      case 'a':
      case 'A':
        {
          float amp = Serial.parseFloat();
          amplitude = constrain(amp / 100.0, 0.0, 1.0);
          printStatus("Amplitud", String(amplitude * 100, 1) + "%");
        }
        break;
        
      case 'v':
      case 'V':
        verboseMode = !verboseMode;
        printStatus("Verbose", verboseMode ? "ON" : "OFF");
        break;
        
      case 's':
      case 'S':
        printDetailedStatus();
        break;
        
      case 'h':
      case 'H':
        printHelp();
        break;
        
      case 'p':
      case 'P':
        // Cambiar velocidad de actualización del plotter
        {
          int interval = Serial.parseInt();
          if (interval >= 5 && interval <= 200) {
            plotInterval = interval;
            printStatus("Plot_Interval", String(interval) + "ms");
          }
        }
        break;
    }
  }
}

//FUNCIONES DE IMPRESIÓN
void printStatus(String param, String value) {
  Serial.print("# ");
  Serial.print(param);
  Serial.print(": ");
  Serial.println(value);
}

void printWaveType() {
  String waveNames[] = {"SENO", "CUADRADA", "TRIANGULAR", "DIENTE_SIERRA"};
  printStatus("Forma_de_onda", waveNames[currentWave]);
}

void printDetailedStatus() {
  Serial.println("# =========================================");
  Serial.print("# Generador: ");
  Serial.println(generatorEnabled ? "ON" : "OFF");
  Serial.print("# Osciloscopio: ");
  Serial.println(oscilloscopeEnabled ? "ON" : "OFF");
  
  String waveNames[] = {"SENO", "CUADRADA", "TRIANGULAR", "DIENTE_SIERRA"};
  Serial.print("# Forma de onda: ");
  Serial.println(waveNames[currentWave]);
  
  Serial.print("# Frecuencia: ");
  Serial.print(frequency, 2);
  Serial.println(" Hz");
  
  Serial.print("# Amplitud (potenciómetro): ");
  Serial.print(currentPotPercentage, 1);
  Serial.println("%");
  
  Serial.println("# -----------------------------------------");
  Serial.print("# DAC Output: ");
  Serial.print(currentDACValue);
  Serial.print(" (");
  Serial.print(currentDACVoltage, 3);
  Serial.println("V)");
  
  Serial.print("# ADC Input: ");
  Serial.print(currentADCValue);
  Serial.print(" (");
  Serial.print(currentADCVoltage, 3);
  Serial.println("V)");
  
  Serial.print("# Potenciómetro: ");
  Serial.print(currentPotValue);
  Serial.print(" (");
  Serial.print(currentPotPercentage, 1);
  Serial.println("%)");
  
  Serial.print("# Intervalo de ploteo: ");
  Serial.print(plotInterval);
  Serial.println("ms");
  
  Serial.println("# =========================================");
}

void printHelp() {
  Serial.println("# =========================================");
  Serial.println("# COMANDOS DISPONIBLES:");
  Serial.println("# g       - Toggle generador ON/OFF");
  Serial.println("# o       - Toggle osciloscopio ON/OFF");
  Serial.println("# w       - Cambiar forma de onda");
  Serial.println("#           (seno→cuadrada→triangular→sierra)");
  Serial.println("# f<val>  - Frecuencia en Hz (ej: f10)");
  Serial.println("#           Rango: 0.1 - 100 Hz");
  Serial.println("# a<val>  - Amplitud en % (ej: a75)");
  Serial.println("#           Rango: 0 - 100%");
  Serial.println("# p<val>  - Intervalo ploteo ms (ej: p50)");
  Serial.println("#           Rango: 5 - 200 ms");
  Serial.println("# v       - Toggle modo verbose");
  Serial.println("# s       - Estado detallado del sistema");
  Serial.println("# h       - Mostrar esta ayuda");
  Serial.println("# =========================================");
  Serial.println("# SEÑALES EN EL PLOTTER:");
  Serial.println("# 1. DAC_Output     - Salida generador (V)");
  Serial.println("# 2. ADC_Input      - Entrada medida (V)");
  Serial.println("# 3. Potentiometer  - Amplitud control (%)");
  Serial.println("# 4. Reference_1.65V- Línea de referencia");
  Serial.println("# 5. Max_3.3V       - Límite superior");
  Serial.println("# 6. Min_0V         - Límite inferior");
  Serial.println("# =========================================");
}