// --- Pin Definitions for ATmega8A ---
const int Dpv = 10;   // PB2: Digital pin to activate connection of PV
const int Apv = A1;   // PC1: Analog pin to sense voltage of PV
const int Dst = 9;    // PB1 (OC1A): Digital pin to activate connection of DC Street light
const int Ast = A2;   // PC2: Analog pin to sense current of DC Street light
const int Dbat = 4;   // PD4: Digital pin to activate connection of battery for charging
const int Avolt = A0; // PC0: Analog pin to measure voltage of battery
const int Lgreen = 7; // PD7: LED to indicate battery charging state
const int Lred = 8;   // PB0: LED to indicate low battery

// --- Constants ---
const float BATTERY_CUTOFF_VOLTAGE = 14.4; // Stop charging at this voltage
const float BATTERY_RESUME_VOLTAGE = 13.5; // Resume charging if voltage drops below this

// --- State Variables ---
bool is_charging = false;
float last_voltage = 0.0;
bool street_light_on = false; 
bool battery_full = false; 

// --- Timers for Twilight (Mid-State) Debounce ---
unsigned long dark_start_time = 0;
unsigned long light_start_time = 0;
bool timing_dark = false;
bool timing_light = false;

// ==========================================
// PWM HELPER FUNCTIONS
// ==========================================
void startStreetLightPWM() {
  // Configure Timer1 for Fast PWM, Mode 14 (TOP = ICR1)
  // Frequency = F_CPU / (Prescaler * (1 + TOP))
  // We use Prescaler 64. F_CPU automatically adapts to 8MHz or 16MHz.
  unsigned int topValue = (F_CPU / (64UL * 600)) - 1; 
  ICR1 = topValue;
  
  // Active-LOW relay: 80% ON means 80% LOW and 20% HIGH.
  // Inverted PWM (COM1A1=1, COM1A0=1) sets pin LOW from 0 to OCR1A.
  OCR1A = (unsigned int)(topValue * 0.80); // CHANGED TO 80% Duty Cycle
  
  // Apply Timer1 Settings
  TCCR1A = (1 << COM1A1) | (1 << COM1A0) | (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11) | (1 << CS10);
}

void stopStreetLight() {
  // Disconnect Timer1 from pin PB1 (Dst) to stop PWM
  TCCR1A &= ~((1 << COM1A1) | (1 << COM1A0));
  // Force pin HIGH (Constant OFF)
  digitalWrite(Dst, HIGH);
}

void setup() {
  pinMode(Dpv, OUTPUT);
  pinMode(Dst, OUTPUT);
  pinMode(Dbat, OUTPUT);
  pinMode(Lgreen, OUTPUT);
  pinMode(Lred, OUTPUT);

  // Initially set Dpv, Dbat, Dst pins to HIGH (OFF)
  digitalWrite(Dpv, HIGH);
  digitalWrite(Dbat, HIGH);
  stopStreetLight(); // Ensures PWM is off and pin is HIGH

  digitalWrite(Lgreen, LOW);
  digitalWrite(Lred, LOW);
}

void loop() {
  // ==========================================
  // 1. READ SUPPLY VOLTAGE (With Averaging)
  // ==========================================
  float VOLT = 0;
  for(int i = 0; i < 10; i++) {
    VOLT += analogRead(Avolt) * (5.0 / 1023.0);
    delay(2);
  }
  VOLT /= 10.0;
  float voltage = VOLT * (138.0 / 18.0); 

  if (voltage < BATTERY_RESUME_VOLTAGE) {
    battery_full = false;
  }

  // ==========================================
  // 2. LOW BATTERY INDICATION
  // ==========================================
  if (voltage < 10.0) {
    digitalWrite(Lred, HIGH);
  } else {
    digitalWrite(Lred, LOW);
  }

  // ==========================================
  // 3. THE "DANGEROUS LOOP" (CHARGING STATE)
  // ==========================================
  if (is_charging) {
    if (voltage >= BATTERY_CUTOFF_VOLTAGE) {
      digitalWrite(Dpv, HIGH);
      digitalWrite(Dbat, HIGH);
      digitalWrite(Lgreen, LOW);
      is_charging = false;
      battery_full = true; 
      delay(500);
    }
    else if (voltage >= last_voltage) {
      last_voltage = voltage;  
      stopStreetLight(); // Ensure street light is OFF
      street_light_on = false;
      delay(1000);             
      return;                  
    }
    else if (voltage < (last_voltage - 0.05)) {
      digitalWrite(Dpv, HIGH); 
      digitalWrite(Dbat, HIGH); 
      digitalWrite(Lgreen, LOW);
      is_charging = false;     
      delay(500); 
    }
    else {
      stopStreetLight(); 
      delay(1000);
      return;                  
    }
  }

  // ==========================================
  // 4. READ PV VOLTAGE (With Averaging)
  // ==========================================
  float PV_V = 0;
  for(int i = 0; i < 10; i++) {
    PV_V += analogRead(Apv) * (5.0 / 1023.0);
    delay(2);
  }
  PV_V /= 10.0;

  // ==========================================
  // 5. BATTERY CHARGING CONTROL
  // ==========================================
  if (PV_V > 4.86 && !battery_full) {
    digitalWrite(Dpv, LOW);
    digitalWrite(Dbat, LOW);
    stopStreetLight(); // Constant high (OFF)
    digitalWrite(Lgreen, HIGH);
    
    street_light_on = false;
    timing_dark = false; 
    timing_light = false;
    
    is_charging = true;
    last_voltage = voltage; 

    delay(1000); 
    return;      
  } else if (!is_charging) {
    digitalWrite(Dpv, HIGH);
    digitalWrite(Dbat, HIGH);
    digitalWrite(Lgreen, LOW);
  }

  // ==========================================
  // 6. STREET LIGHT CONTROL (Twilight Timer)
  // ==========================================
  if (PV_V < 4.15) {
    if (!timing_dark) {
      dark_start_time = millis(); 
      timing_dark = true;
    }
    timing_light = false; 

    if (millis() - dark_start_time > 5000) { 
      street_light_on = true;
    }
  } 
  else if (PV_V > 4.30) { 
    if (!timing_light) {
      light_start_time = millis(); 
      timing_light = true;
    }
    timing_dark = false; 

    if (millis() - light_start_time > 5000) { 
      street_light_on = false;
    }
  } 
  else {
    timing_dark = false;
    timing_light = false;
  }

  // ==========================================
  // 7. APPLY STREET LIGHT STATE & CHECK CURRENT
  // ==========================================
  if (street_light_on) {
    startStreetLightPWM(); // Send 600Hz 80% Duty Cycle PWM

    // Read current
    float ST_V = 0;
    for(int i = 0; i < 10; i++) {
      ST_V += analogRead(Ast) * (5.0 / 1023.0);
      delay(2);
    }
    ST_V /= 10.0;
    float current = ST_V / 5.5; // in Ampere (Rshunt=0.5 ohm, gain=11)

    // Overcurrent Protection
    if (current > 5.0) {
      stopStreetLight(); // Constant High (Force OFF)
      street_light_on = false; 
      timing_dark = false;     

      // Blink the Lred pin for 5 seconds
      for (int i = 0; i < 5; i++) {
        digitalWrite(Lred, HIGH);
        delay(500);
        digitalWrite(Lred, LOW);
        delay(500);
      }
    }
  } else {
    stopStreetLight(); // Turn OFF street light
  }

  delay(100); // Small loop delay for microcontroller stability
}
