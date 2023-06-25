//
// 19.06.2023 Abschluss, Übergabe SE
//
const float FW_VERSION = 1.01;
// KLAMMER - MASTER - CONTROL
// Übernahme Adaption ESP32 / Stromzähler
//
// 
//

//#define DEBUGMODE    // If you comment this line, the DPRINT & DPRINTLN lines are defined as blank.
#ifdef DEBUGMODE        // Macros are usually in all capital letters.
#define DPRINT(...) Serial.print(__VA_ARGS__)     // DPRINT is a macro, debug print
#define DPRINTLN(...) Serial.println(__VA_ARGS__) // DPRINTLN is a macro, debug print with new line
#else
#define DPRINT(...)   // now defines a blank line
#define DPRINTLN(...) // now defines a blank line
#endif

#include <Arduino.h>
#include <Preferences.h>
#include "TFT_eSPI.h"
//#include <AiEspRotaryEncoder.h>
//#include "P:\KLAMMER\KLAMMER_PIO\.pio\libdeps\lilygo-t-display-s3\Ai Esp32 Rotary Encoder\src\AiEsp32RotaryEncoder.h"
#include "..\.pio\libdeps\lilygo-t-display-s3\Ai Esp32 Rotary Encoder\src\AiEsp32RotaryEncoder.h"
#include <esp32WS2811.h>


// IO Mapping:
#define ROTARY_ENCODER_A_PIN        1   // Encoder Kanal A
#define ROTARY_ENCODER_B_PIN        2   // Encoder Kanal B
#define ROTARY_ENCODER_BUTTON_PIN   3   // Encoder Push Button
#define ROTARY_ENCODER_VCC_PIN     -1
#define ROTARY_ENCODER_STEPS        4


#define SW_CLOSE        16              // Start Taste Input IO16
#define PAUSE           43              // PAUSE Eingang Input IO43
#define SW_OPEN         21              // Open Taste Input IO21


// Debounce Zeitkonstanten:
#define DEBOUNCE        3               // 3ms Debounce Delay
#define DEBOUNCE_CMD    100             // 100ms Debounce Delay vor Ausführung Anforderung
#define DEBOUNCE_STOP   2000            // 2s Key Pressed before STOP triggered

// State Maschine
#define STATE_INIT          0
#define STATE_CLOSE_CLAMP   3
#define STATE_ALL_CLOSED    50
#define STATE_OPEN          100



// Datenobjekt für einen NVS Namespace
Preferences prefs;
const char * nvs_namespace = "nvs";



// variables /////////////////////////////////////////////////////////////////////////////
unsigned long  CLOSE_DELAY = 200;       // Verzögerung Klammer schließen in ms
unsigned long  OPEN_DELAY = 100;        // Verzögerung Klammer öffnen in ms
unsigned long  N_CLAMP = 100;           // Anzahl Klammern
#define MAX_CLAMP 100                   // Max. Anzahl Klammern

// WS2811 Objekt erstellen:
// first argument is the data pin, the second argument is the number of LEDs
WS2811 clampString(18, N_CLAMP);

// Encoder Objecte erstellen:
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, ROTARY_ENCODER_BUTTON_PIN, ROTARY_ENCODER_VCC_PIN, ROTARY_ENCODER_STEPS);

// Display Objekte erstellen:
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite( &tft );
TFT_eSprite m_sprite = TFT_eSprite( &tft );

// progress bar variables
int progress    = 0;
int w           = 170; //121;
int h           = 25;  //18;
int x           = 6;
int y           = 100;
int blocks      = 0;



// Menu and submenu/setting declarations
byte Mode = 0;          // This is which menu mode we are in at any given time (top level or one of the submenus)
const byte modeMax = 3; // This is the number of submenus/settings you want



//
// PAUSE Signal entprellen
//
unsigned int Debounce_PAUSE( void  )
{
    static int State = 0;
    static unsigned long st;

    switch ( State )
    {
        case 0:     // Wait till falling edge 1 => 0
            // DPRINTLN("Debounce PAUSE State: 0");
            if( digitalRead( PAUSE ) == false)
            {
                st = millis();
                State++;   
            }
        break;


        case 1:     // Delay x ms to make sure signal is stable
            // DPRINTLN("Debounce PAUSE State: 1");
            if( ( millis() - st ) >= DEBOUNCE )
                State++;
        break;


        case 2:     // Double check if PAUSE signal is still low
            // DPRINTLN("Debounce PAUSE State: 2");
            if( digitalRead( PAUSE ) == false)
            {
                if ( millis() - st >= 60000 )
                    st = millis() - 60000;  // auf max. 60000ms klemmen
                
                return( (unsigned int)(millis() - st) );
            }
            else // PAUSE signal released
            {
                State = 0;
                return( (unsigned int)0 );
            }            
        break;


        default:
            return( (unsigned int)0 );
          
    } // end of switch

    // will never make it to this line
    return( (unsigned int)0 );
                
} // end of Debounce_PAUSE()

//
// SW_OPEN entprellen
//
unsigned int Debounce_SW_OPEN( void  )
{
    static int State = 0;
    static unsigned long st;

    switch ( State )
    {
        case 0:     // Wait till falling edge 1 => 0
            // DPRINTLN("Debounce SW_OPEN State: 0");
            if( digitalRead( SW_OPEN) == false)
            {
                st = millis();
                State++;   
            }
        break;


        case 1:     // Delay x ms to make sure signal is stable
            // DPRINTLN("Debounce SW_OPEN State: 1");
            if( ( millis() - st ) >= DEBOUNCE )
                State++;
        break;


        case 2:     // Double check if signal is still Low
            // DPRINTLN("Debounce SW_OPEN State: 2");
            if( digitalRead( SW_OPEN) == false)
            {           
                if ( millis() - st >= 60000 )
                    st = millis() - 60000;  // auf max. 60000ms klemmen
                
                return( (unsigned int)(millis() - st) );
            }
            else // Taste released
            {
                State = 0;
                return( (unsigned int)0 );
            }            
        break;


        default:
            return( (unsigned int)0 );

          
    } // end of switch

    // will never make it to this line
    return( (unsigned int)0 );

} // end of Debounce_SW_OPEN()

//
// SW_CLOSE entprellen
//
unsigned int Debounce_SW_CLOSE( void  )
{
    static int State = 0;
    static unsigned long st;

    switch ( State )
    {
        case 0:     // Wait till falling edge 1 => 0
            // DPRINTLN("Debounce SW_CLOSE State: 0");
            if( digitalRead( SW_CLOSE) == false)
            {
                st = millis();
                State++;   
            }
        break;


        case 1:     // Delay x ms to make sure signal is stable
            // DPRINTLN("Debounce SW_CLOSE State: 1");
            if( ( millis() - st ) >= DEBOUNCE )
                State++;
        break;


        case 2:     // Double check if signal is still Low
            // DPRINTLN("Debounce SW_CLOSE State: 2");
            if( digitalRead( SW_CLOSE) == false)
            {
                if ( millis() - st >= 60000 )
                    st = millis() - 60000;  // auf max. 60000ms klemmen
                
                return( (unsigned int)(millis() - st) );
            }
            else // Taste released
            {
                State = 0;
                return( (unsigned int)0 );
            }            
        break;


        default:
            return( (unsigned int)0 );

          
    } // end of switch

    // will never make it to this line
    return( (unsigned int)0 );

} // end of Debounce_SW_CLOSE()


//
// Encoder ISR
//
void IRAM_ATTR readEncoderISR()
{
	rotaryEncoder.readEncoder_ISR();
}




//
// Disply Menu
//
void Menu( byte name, uint16_t setting )
{

    #define Xs  5

    char str[20];



        m_sprite.fillSprite( TFT_WHITE );        
        m_sprite.drawRoundRect( 1, 1, 137, 168, 3, TFT_BLACK );
        //m_sprite.setFreeFont( &Orbitron_Light_24 );
        m_sprite.drawString( "SETUP", Xs + 20, 16, 4 );


        if( name == 0 && setting == 1 )
        {
            m_sprite.setTextColor( TFT_BLACK, TFT_YELLOW, 1 );
            m_sprite.drawString( "Close Delay (0.1s)", Xs, 40, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_WHITE, 0 );
            m_sprite.drawString( String( CLOSE_DELAY / 100 ), Xs, 60, 2 );

            m_sprite.drawString( "Open Delay (0.1s)", Xs, 90, 2 );
            m_sprite.drawString( String( OPEN_DELAY / 100 ), Xs, 110, 2 );

            m_sprite.drawString( "# Clamps", Xs, 140, 2 );
            sprintf(str, "%03lu", N_CLAMP);
            m_sprite.drawString( str, Xs, 160, 2 ); 
        }
        else if( name == 0  && setting == 2 )
        {
            m_sprite.drawString( "Close Delay (0.1s)", Xs, 40, 2 );
            m_sprite.drawString( String( CLOSE_DELAY / 100 ), Xs, 60, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_YELLOW, 1 );
            m_sprite.drawString( "Open Delay (0.1s)", Xs, 90, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_WHITE, 0 );
            m_sprite.drawString( String( OPEN_DELAY / 100 ), Xs, 110, 2 );

            m_sprite.drawString( "# Clamps", Xs, 140, 2 );
            sprintf(str, "%03lu", N_CLAMP);
            m_sprite.drawString( str, Xs, 160, 2 );

        }
        else if( name == 0 && setting == 3)
        {
            m_sprite.drawString( "Close Delay (0.1s)", Xs, 40, 2 );
            m_sprite.drawString( String( CLOSE_DELAY / 100 ), Xs, 60, 2 );

            m_sprite.drawString( "Open Delay (0.1s)", Xs, 90, 2 );
            m_sprite.drawString( String( OPEN_DELAY / 100 ), Xs, 110, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_YELLOW, 1 );
            m_sprite.drawString( "# Clamps", Xs, 140, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_WHITE, 0 );
            sprintf(str, "%03lu", N_CLAMP);
            m_sprite.drawString( str, Xs, 160, 2 );

        }
        else
        {
            m_sprite.setTextColor( TFT_BLACK, TFT_WHITE, 0 );
            
            m_sprite.drawString( "Close Delay (0.1s)", Xs, 40, 2 );
            m_sprite.drawString( String( CLOSE_DELAY / 100 ), Xs, 60, 2 );

            m_sprite.drawString( "Open Delay (0.1s)", Xs, 90, 2 );
            m_sprite.drawString( String( OPEN_DELAY / 100 ), Xs, 110, 2 );

            m_sprite.drawString( "# Clamps", Xs, 140, 2 );
            sprintf(str, "%03lu", N_CLAMP);
            m_sprite.drawString( str, Xs, 160, 2 );

        }


      
        
        if( name == 1 )
        {
            m_sprite.drawString( "Close Delay (0.1s)", Xs, 40, 2 );
            m_sprite.setTextColor( TFT_BLACK, TFT_YELLOW, 1 );
            m_sprite.drawString( String( setting ), Xs, 60, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_WHITE, 0 );
            m_sprite.drawString( "Open Delay (0.1s)", Xs, 90, 2 );
            m_sprite.drawString( String(OPEN_DELAY / 100 ), Xs, 110, 2 );

            m_sprite.drawString( "# Clamps", Xs, 140, 2 );
            sprintf(str, "%03lu", N_CLAMP);
            m_sprite.drawString( str, Xs, 160, 2 );

        }
        else if( name == 2 )
        {
            m_sprite.drawString( "Open Delay (0.1s):", Xs, 90, 2 );
            m_sprite.setTextColor( TFT_BLACK, TFT_YELLOW, 1 );
            m_sprite.drawString( String( setting ), Xs, 110, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_WHITE, 0 );
            m_sprite.drawString( "Close Delay (0.1s)", Xs, 40, 2 );
            m_sprite.drawString( String( CLOSE_DELAY / 100 ), Xs, 60, 2 );

            m_sprite.drawString( "# Clamps:", Xs, 140, 2 );            
            sprintf(str, "%03lu", N_CLAMP);
            m_sprite.drawString( str, Xs, 160, 2 );

        }
        else if( name == 3 )
        {
            m_sprite.drawString( "# Clamps:", Xs, 140, 2 );
            m_sprite.setTextColor( TFT_BLACK, TFT_YELLOW, 1 );
            sprintf(str, "%03lu", setting);
            m_sprite.drawString( str, Xs, 160, 2 );

            m_sprite.setTextColor( TFT_BLACK, TFT_WHITE, 0 );
            m_sprite.drawString( "Close Delay (0.1s):", Xs, 40, 2 );
            m_sprite.drawString( String( CLOSE_DELAY / 100 ), Xs, 60, 2 );

            m_sprite.drawString( "Open Delay (0.1s):", Xs, 90, 2 );
            m_sprite.drawString( String( OPEN_DELAY / 100 ), Xs, 110, 2 );

            
        }


        m_sprite.pushSprite( 180, 0 );

        

} // end of Menu()


//
// Carry out common activities each time a setting is changed
//
void setAdmin( byte name, u_long setting )
{
    DPRINT( "Setting " );       // DEBUGGING
    DPRINT( name );             // DEBUGGING
    DPRINT( " = " );            // DEBUGGING
    DPRINTLN( setting );        // DEBUGGING
    Mode = 0;                   // go back to top level of menu, now that we've set values

    prefs.putULong("CLOSE_DELAY", CLOSE_DELAY);
    prefs.putULong("OPEN_DELAY",  OPEN_DELAY);
    prefs.putULong("N_CLAMP",     (u_long)N_CLAMP );

    DPRINTLN( "Main Menu" );    // DEBUGGING
}



//
// Menue()
//
void rotaryMenu()
{ 
    // This handles the bulk of the menu functions without needing to install/include/compile a menu library
    Menu( Mode, (uint16_t)rotaryEncoder.readEncoder() );
   

    // Main menu section
    if (Mode == 0)
    {
        rotaryEncoder.setBoundaries( 0, 3, true );
        
        if ( rotaryEncoder.isEncoderButtonClicked() )
        {
            Mode = (byte)rotaryEncoder.readEncoder();              // set the Mode to the current value of input if button has been pressed
            DPRINT( "Mode selected: " );    // DEBUGGING: print which mode has been selected
            DPRINTLN( Mode );               // DEBUGGING: print which mode has been selected
            
            if ( Mode == 1 )
            {
                DPRINTLN( "CLOSE DELAY (* 0.1s)" );             // DEBUGGING: print which mode has been selected
                rotaryEncoder.setBoundaries(1, 200, false);
                rotaryEncoder.setEncoderValue( (long)(CLOSE_DELAY / 100) );

                Menu( 1, (uint16_t)rotaryEncoder.readEncoder() );                          // Refresh Menu, Feld 1 aktiv
            }
            if ( Mode == 2 )
            {
                DPRINTLN( "OPEN DELAY (* 0.1s)" );              // DEBUGGING: print which mode has been selected
                rotaryEncoder.setBoundaries(1, 200, false);
                rotaryEncoder.setEncoderValue( (long)(OPEN_DELAY / 100) );
                
                Menu( 2, (uint16_t)rotaryEncoder.readEncoder() );                          // Refresh Menu, Feld 2 aktiv
            }
            if ( Mode == 3 )
            {
                DPRINTLN( "# CLAMPs" );                        // DEBUGGING: print which mode has been selected
                rotaryEncoder.setBoundaries(1, 100, false);
                rotaryEncoder.setEncoderValue( (long)(N_CLAMP) );
                
                Menu( 3, (uint16_t)rotaryEncoder.readEncoder() );                          // Refresh Menu, Feld 3 aktiv
            }
            else
            {
                Menu( 0, 0);                                    // Refresh Menu, kein aktives Feld
            }
        }
    }
    if ( Mode == 1 && rotaryEncoder.isEncoderButtonClicked() )
    {
        // code to do other things with setting1 here, perhaps update display
        CLOSE_DELAY = ((unsigned long)rotaryEncoder.readEncoder() ) * 100;        
        setAdmin( 1, CLOSE_DELAY );
    }
    if ( Mode == 2 && rotaryEncoder.isEncoderButtonClicked() )
    {
        // code to do other things with setting2 here, perhaps update display
        OPEN_DELAY = ((unsigned long)rotaryEncoder.readEncoder()) * 100;
        setAdmin( 2, OPEN_DELAY );
    }
    if ( Mode == 3 && rotaryEncoder.isEncoderButtonClicked() )
    {
        // code to do other things with setting3 here, perhaps update display    
        N_CLAMP = (unsigned int)rotaryEncoder.readEncoder();
        setAdmin( 3, N_CLAMP );
    }
} // end of rotaryMenu()









////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Start of main programm !
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//
// setup
// 
void setup()
{
    char  message[50];
    
    // IO konfigurieren
    pinMode(15, OUTPUT); // to boot with battery...
    digitalWrite(15,1);  // and/or power from 5v rail instead of USB

    pinMode(SW_CLOSE,   INPUT_PULLUP);
    pinMode(SW_OPEN,    INPUT_PULLUP);
    pinMode(PAUSE,      INPUT_PULLUP);


    //we must initialize rotary encoder
	rotaryEncoder.begin();
	rotaryEncoder.setup(readEncoderISR);
	//set boundaries and if values should cycle or not
	//in this example we will set possible values between 0 and 1000;
	bool circleValues = true;
	rotaryEncoder.setBoundaries(0, 3, circleValues);    //minValue, maxValue, circleValues true|false (when max go to min and vice versa)
    rotaryEncoder.setAcceleration(1);                   //or set the value - larger number = more accelearation; 0 or 1 means disabled acceleration

    




    // Parameter aus NVS laden
    prefs.begin(nvs_namespace, false);
        
    CLOSE_DELAY = prefs.getULong("CLOSE_DELAY", CLOSE_DELAY);
    OPEN_DELAY  = prefs.getULong("OPEN_DELAY",  OPEN_DELAY);
    N_CLAMP     = prefs.getULong("N_CLAMP",     N_CLAMP );
 
 
    // Debug Ausgabe init:
     #ifdef DEBUGMODE
        Serial.begin(115200);
    #endif
    
    // Clamp String init:
    DPRINTLN("Init Clamp String....");
    clampString.begin();

   
    // Display init:
    tft.init();
    tft.setRotation( 1 );
    tft.setSwapBytes( true );
    tft.fillScreen( TFT_WHITE );
    // tft.pushImage(106,0,214,170,install);
    //sprite.createSprite(320,170);
    sprite.createSprite( 180, 170 );
    sprite.setTextColor( TFT_BLACK, TFT_WHITE );
    sprite.setTextDatum( MC_DATUM );       // middle centre

    m_sprite.createSprite( 140, 170 );
    m_sprite.setTextColor( TFT_BLACK, TFT_WHITE );
    m_sprite.setTextDatum( ML_DATUM );     // middle left

} // end of setup()


//
// loop
//
void loop()
{
    char  message[50];

    static unsigned long StartTime = 0;
    static unsigned long T10ms = 0;
    static unsigned long T100ms = 0;
    static unsigned long STATE_DELAY = 0;
    static unsigned long DIAG_DELAY = 0;
    static unsigned st = 0;
    static unsigned STEP = 0;



    // bei jedem Durchlauf:
    Debounce_SW_CLOSE();
    Debounce_SW_OPEN();
    Debounce_PAUSE();
    rotaryMenu();


    // 10ms Loop:
    if( millis() - T10ms >= 10 )
    {
        //DPRINTLN("Start Clamping, Delay vor 1st Clamp");
      
        T10ms = millis();       // 10ms Timer neu aufziehen
        clampString.show();     // Refresh String Data

        // String State Maschine
        switch( st )
        {
            // Init Statemaschine:
            case STATE_INIT:
                clampString.clearAll(); // alle Klammern auf!
                STEP = 0;               // Schritt  0

                st++;

            break;

            // Wait for START Command:
            case 1:

                if( Debounce_SW_CLOSE() >= DEBOUNCE_CMD )
                {
                    STATE_DELAY = millis(); // Timer aufziehen
                    st++;

                    DPRINTLN("Start Clamping, Delay vor 1st Clamp");
                }
                
            break;

            // Delay until 1st Clamp:
            case 2:
                
                if( millis() - STATE_DELAY  >= CLOSE_DELAY )
                {
                    STATE_DELAY = millis();
                    st++;
                    // DPRINTLN("Close Clamp");
                }
                
            break;


             // Close Clamp:
            case 3:
                
                // RGB ON
                clampString.setRed  ((size_t)STEP, 255);
                clampString.setBlue ((size_t)STEP, 255);
                clampString.setGreen((size_t)STEP, 255);
            
                st++;

                DPRINT("Clamp #: ");
                DPRINTLN( STEP+1 );
                
            break;


            // Init Delay:
            case 4:

                STATE_DELAY = millis();
                DIAG_DELAY = millis() - 500;    // Diagnosemeldung nur alle 500ms                 
                st++;
                
            break;


            // Delay until next Clamp:
            case 5:

                // Pause aktiviert:
                if( Debounce_PAUSE() >= DEBOUNCE_CMD )
                {   
                    STATE_DELAY = millis();     // Timer neu aufziehen, solange PAUSE aktiv!

                    if(  (millis() - DIAG_DELAY) >= 500 )
                    {
                        DPRINTLN( "PAUSE AKTIV" );
                        DIAG_DELAY = millis();
                    }                                     
                }
                                               
                // Delay abgelaufen?
                if( millis() - STATE_DELAY  >= CLOSE_DELAY )
                {
                    STEP++;     // zur nächsten Klammer schalten
                    st++;       
                }


                // Check STOP Key
                if( Debounce_SW_OPEN() >= DEBOUNCE_STOP )
                {
                    st = STATE_ALL_CLOSED + 2;
                }

                
            break;


            // Check if all Clamps are closed
            case 6:

                if( STEP >= N_CLAMP )
                {
                    STEP = N_CLAMP;         // auf max. Klammerzahl klemmen
                    st = STATE_ALL_CLOSED;  // Schließvorgang abgeschlossen

                    DPRINTLN( "Alle Klammern geschlossen" );
                }
                else
                    st = STATE_CLOSE_CLAMP;
                               
            break;


 
            // Wait for Open command
            case STATE_ALL_CLOSED:

                if( Debounce_SW_OPEN() >= DEBOUNCE_CMD )
                {
                    STATE_DELAY = millis();
                    st++;
                }
                                
            break;


            // Timer läuft
            case STATE_ALL_CLOSED + 1:

                if(millis() - STATE_DELAY >= OPEN_DELAY)
                    st++;                       
                                
            break;


            // Open Clamp:
            case STATE_ALL_CLOSED + 2:

                if( STEP )
                {
                    STEP--;
                    
                    // RGB OFF
                    clampString.setRed  ((size_t)STEP, 0);
                    clampString.setBlue ((size_t)STEP, 0);
                    clampString.setGreen((size_t)STEP, 0);

                    STATE_DELAY = millis();     // Timer aufziehen
                    st--; 

                    DPRINT("Clamp #: ");
                    DPRINTLN( STEP );
                }
                else
                    st++;
                
            break;


            // Fertig mit Arbeitstakt:
            case STATE_ALL_CLOSED + 3:

                st = 0;     // alles auf Start
                                
            break;
            
            // wird normalerweise nie angesprungen...
            default:
                st = 0;     // alles auf Start

        } // end of switch()
    
    } // end of 10ms loop
        


    // 100ms Loop:
    if( millis() - T100ms >= 100 )
    {
    
        T100ms = millis();      // 100ms Timer neu aufziehen

        // Display Ausgabe
        sprite.fillSprite( TFT_WHITE );
        sprite.setFreeFont( &Orbitron_Light_24 );
        sprite.drawString( "Progress:", 75, 16 );
        sprite.setFreeFont( &Orbitron_Light_32 );
        //sprite.setFreeFont( &chrtbl_f72 );
        sprite.drawString( String( STEP ), 75, 54 );

        // Fortschrittsbalken
        progress = ( STEP * 100 ) / N_CLAMP;    // Akt. Fortschritt in % ermitteln
        if( progress == 101 )
            progress = 0;

        blocks = progress / 5;                  // Anz. Segmente im Fortschrittsbalken
        sprite.drawRoundRect( x, y, w - 1, h, 3, TFT_BLACK );
        for( int i = 0; i < blocks; i++ )
            sprite.fillRect( i * 7 + (x + 3) + (i * 1), y + 4, 7, h - 7, TFT_BLACK );

        #define KEY_LINE_Y 139


        sprite.drawRect( x, KEY_LINE_Y, 55, 22, TFT_BLACK );
        sprite.drawString( "START", x + 27, KEY_LINE_Y + 11, 2 );

        sprite.drawRect( x + 59, KEY_LINE_Y, 55, 22, TFT_BLACK );
        sprite.drawString( "STOP", x + 80 + 6, KEY_LINE_Y + 11, 2 );

        sprite.drawRect( x + 118, KEY_LINE_Y, 54, 22, TFT_BLACK );
            if( Debounce_PAUSE() >= DEBOUNCE_CMD )
                sprite.drawString( "PAUSE", x + 138 + 5, KEY_LINE_Y + 11, 2 );
            else
                sprite.drawString("     ", x + 138 + 5, KEY_LINE_Y + 11, 2 );  
            
        sprite.setTextFont( 0 );
        //sprite.drawString("Installation almost done!!",80,160);
        sprite.pushSprite( 0, 0 );


        

    }// end of 100ms loop
    
} // end of loop





























