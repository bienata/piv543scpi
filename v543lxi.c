/*

kompilacja:
  g++ -o v543lxi -lwiringPi v543.c
  
uruchomienie:
  ./v543lxi
  
*/

#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <wiringPi.h>

#define SCPI_PORT	5555

#define LINE_READY  0
#define LINE_CLK    1
#define LINE_LOAD   2
#define LINE_DATA   3

#define LED_READY   4
#define LED_SCPI    5


#define DEVICE_VENDOR       "Meratronik"
#define DEVICE_NAME         "V543"
#define DEVICE_SERIAL       "01473"
#define FIRMWARE_VERSION    "NB02-666-tasza-2018"

unsigned char   uchLedReady = 0;
unsigned char   uchLedScpi = 0;
unsigned long   ulRawMeterData = 0L;
unsigned char   uchRangeId = 0;
unsigned char   uchModeId = 0;
unsigned char   uchPolarity = 0;

char* trim(char*);  
void makeLower (char*);

void handleIdn(char*);
void handleRaw(char*);
void handleDisplay(char*);
void handleSystemError(char*); 
void handleMeasureVoltage(char*);
void handleMeasureResistance(char*);
void handleSenseFunction(char*);
void handleSenseVoltageRange(char*);
void handleSenseResistanceRange(char*);

// prototyp handlerka komendy scpi, po prostu wypełnia wynik w out i tyle
typedef void (*TScpiCommandHandler)(char*);

// parka komenda-handler
typedef struct {
    const char *cmd;
    TScpiCommandHandler handler;
} TCommand;

// na informacje o zakresie
typedef struct {
    const char  *label;     // nalepka
    float value;            // zakres numerycznie
    float scale;            // skalowanie
//    const char  *format;    
} TRangeInfo;


//------------------------------------------------------------------------------
// to póki co obsługujemy
TCommand scpiCommands[] = {
    // identyfikacja
    {   "*idn?",                    &handleIdn }, 
    // pomiary
    {   ":measure:voltage:dc?",     &handleMeasureVoltage },
    {   ":measure:voltage:ac?",     &handleMeasureVoltage },
    {   ":measure:resistance?",     &handleMeasureResistance },            
    // zakresy
    {   ":sense:voltage:dc:range?", &handleSenseVoltageRange },
    {   ":sense:voltage:ac:range?", &handleSenseVoltageRange },
    {   ":sense:resistance:range?", &handleSenseResistanceRange },        
    // tryb pracy
    {   ":sense:function?",         &handleSenseFunction },
    // polecenia systemowe
    {   ":system:raw?",             &handleRaw },
    {   ":system:display?",         &handleDisplay }, 
    // bledy
    {   ":syst:err?",               &handleSystemError },
    {   "syst:err?",                &handleSystemError },     
    {   NULL ,                      NULL }
};

TRangeInfo volRangeInfo[] = {
    //  label,      value,  scale
    {   "100V",     100,    100     },//0  
    {   "1V",       1,      10000   },//1  
    {   "1kV",      1000,   10      },//2 
    {   "10V",      10,     1000    },//3 
    {   "100mV",    0.1,    100000  },//4 
    {   "error",    1,      1       },//5
    {   "error",    1,      1       },//6
    {   "error",    1,      1       } //7
};

TRangeInfo resRangeInfo[] = {
    // label,       value,  scale
    {   "100k",     100E3,  0.1     },//0
    {   "1k",       1E3,    10      },//1  
    {   "error",    0,      1       },//2
    {   "10k",      10E3,   1       },//3  
    {   "error",    0,      1       },//4
    {   "1M",       1E6,    0.01    },//5
    {   "error",    0,      1       },//6
    {   "10M",      10E6,   0.001   } //7
};


const char *pszModeDesc[] = {
      "error",    // 0
      "R",        // 1
      "AC",       // 2
      "error",    // 3
      "DC"        // 4        
};

//------------------------------------------------------------------------------
// numeryczna zawartośc wystwietlacza    
int getNumericDisplay( void ) {
    char s[16];
    sprintf( s, "%05X", ulRawMeterData &0x1FFFF );        
    return atoi( s );
}


//------------------------------------------------------------------------------
// zawsze wszystko jest ok
void handleSystemError(char *out) {    
    strcpy ( out, "0,\"No error\"\n" );
}

//------------------------------------------------------------------------------
// identyfikacja urządzenia 
void handleIdn(char *out) {    
    sprintf ( 
        out, 
        "%s,%s,%s,%s\n",
        DEVICE_VENDOR,
        DEVICE_NAME,
        DEVICE_SERIAL,
        FIRMWARE_VERSION
    );    
}

//------------------------------------------------------------------------------
// tryb pracy R,AC,DC
void handleSenseFunction(char *out) {    
    sprintf( out, "%d|%s\n", uchModeId, pszModeDesc[ uchModeId ] );
}

//------------------------------------------------------------------------------
// zakres dla rezystancji
void handleSenseResistanceRange (char *out) {
    sprintf( 
        out, 
        "%E|%d|%s\n", 
        resRangeInfo[ uchRangeId ].value,
        uchRangeId, 
        resRangeInfo[ uchRangeId ].label
    );                
}

//------------------------------------------------------------------------------
// zakres dla napiecia, AC czy DC już wszystko jedno :(
void handleSenseVoltageRange (char *out) {
    sprintf( 
        out, 
        "%E|%d|%s\n", 
        volRangeInfo[ uchRangeId ].value,
        uchRangeId, 
        volRangeInfo[ uchRangeId ].label
    );                
}

    
//------------------------------------------------------------------------------
// pomiar napiecia
void handleMeasureVoltage (char *out) { 
    if ( uchModeId != 4 /*DC*/ && uchModeId != 2 /*AC*/) {
        strcpy ( out, "1, wrong mode error\n" );            
        return;
    }
    char sign = ' ';
    if ( uchModeId == 4 /*DC*/){
        sign = uchPolarity == 1 ? '+' : '-';
    }    
    float v = ((float)getNumericDisplay()) / volRangeInfo[ uchRangeId ].scale;
    sprintf( 
        out, 
        "%c%E\n", 
        sign,
        v
    );            
}

//------------------------------------------------------------------------------
// pomiar rezystancji
void handleMeasureResistance (char *out) { 
    if ( uchModeId != 1 /*R*/) {
        strcpy ( out, "1, wrong mode error\n" );            
        return;
    }
    float r = ((float)getNumericDisplay()) / resRangeInfo[ uchRangeId ].scale;
    sprintf( 
        out, 
        "%E\n", 
        r
    );            
}


//------------------------------------------------------------------------
// odsyła znak i pięć cyferek wyświetlacza
void handleDisplay(char *out) {
    char sign = ' ';
    if ( uchModeId == 4 /*DC*/){
        sign = uchPolarity == 1 ? '+' : '-';
    }
    else if ( uchModeId == 2 /*AC*/){
        sign = '~';
    }
    sprintf( out, "%c%05X\n", sign, ulRawMeterData &0x1FFFF );    
} 

//------------------------------------------------------------------------
// odsyła surowe 32 bity hex
void handleRaw(char *out) {
    sprintf( out, "%08X\n", ulRawMeterData );    
}


//------------------------------------------------------------------------
// rozpoznanie i wykonanie polecenia SCPI
void processScpiCommand ( char *cmd, char *out ) {
    // domyslnie blad
    strcpy ( out, "error\n" );    
    for( int i = 0; scpiCommands[i].cmd != NULL; ++i ) {
        if ( strcmp ( cmd, scpiCommands[i].cmd ) == 0 ) {
            // trafiony!
            (scpiCommands[i].handler)(out);
            break;
        }
    }
    digitalWrite ( LED_SCPI, uchLedScpi ) ;        
    uchLedScpi ^= 1;    
}

//------------------------------------------------------------------------
// :) żywcem zerżnięte z dawnego kodu dla Arduino, jak pisałam dla EdW
unsigned long readV543rawData( void ) {
  unsigned long rawFrame = 0L;
  int n;  
  digitalWrite( LINE_LOAD, LOW ); // do -\/- pulse
  digitalWrite( LINE_LOAD, HIGH ); 
  digitalWrite( LINE_CLK, LOW );
  for ( n = 31; n >= 0; n-- )  {
    if ( digitalRead( LINE_DATA ) ) {
          rawFrame |= ( (unsigned long)1 << n );
    }     
    digitalWrite( LINE_CLK, HIGH);        
    digitalWrite( LINE_CLK, LOW);
  } // for       
  return rawFrame & 0x03FFFFFFL;
}

//------------------------------------------------------------------------
// obsługa przerwania od GPIO z pinu LINE_READY Meratronika
void onMeterReadyInterrupt( void ) {        
    // fizyczny odczyt
    ulRawMeterData = readV543rawData();    
    uchRangeId = (ulRawMeterData >> 17) & 0x07;
    uchModeId = (ulRawMeterData >> 22) & 0x07;
    uchPolarity = (ulRawMeterData >> 20) & 0x03;
    // mignięcie ledem
    digitalWrite ( LED_READY, uchLedReady ) ;        
    uchLedReady ^= 1;
}

// main foo.
int main( int argc, char *argv[] ) {

    struct sockaddr_in serverAddress, clientAddress;  
    int serverSocket, clientSocket;
    int clientAddressLen;
    int n;
    int data;
    char commandBuffer[ 64 ];
    char responseBuffer[ 64 ];  
    int sessionCntr = 0;
    int commandCntr = 0;    
     
    wiringPiSetup () ;     
    pinMode ( LINE_READY, INPUT );
    pinMode ( LINE_CLK,   OUTPUT );
    pinMode ( LINE_LOAD,  OUTPUT );     digitalWrite( LINE_LOAD, HIGH );   
    pinMode ( LINE_DATA,  INPUT );      digitalWrite( LINE_CLK, HIGH );   
    
    pinMode ( LED_READY, OUTPUT );  
    pinMode ( LED_SCPI, OUTPUT );

    
    if ( wiringPiISR( LINE_READY, INT_EDGE_RISING, &onMeterReadyInterrupt ) < 0 ) { 
        printf ( "Unable to setup ISR: %s\n", strerror (errno) );
        exit(1) ;
    }    
         
     serverSocket = socket( AF_INET, SOCK_STREAM, 0 );
     if ( serverSocket < 0 ) {
         printf ( "00 error when opening server socket: %s\n", strerror (errno) ) ;         
	     exit (1);
     }
     
     bzero( (char *)&serverAddress, sizeof(serverAddress) );
     serverAddress.sin_family = AF_INET;
     serverAddress.sin_addr.s_addr = INADDR_ANY;
     serverAddress.sin_port = htons( SCPI_PORT );
     
     if ( bind( serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress) ) < 0 ) {
         printf ( "01 error when binding server socket: %s\n", strerror(errno) );
	     exit (1);
     }
     
     listen( serverSocket, 5);     
     clientAddressLen = sizeof(clientAddress);
  
     int exitRequest;
     
     // czekaj na polecenia     
     
     while ( 1 ) {
       
        printf( "10 waiting for connection [%04d]\n", sessionCntr++ );
	
        if ( ( clientSocket = accept( serverSocket, (struct sockaddr *)&clientAddress, (socklen_t*)&clientAddressLen) ) < 0 ) {	        
            printf ( "02 error on accept incoming connection: %s\n", strerror (errno) );                     
	        exit (1);
	    }
        
        printf ( "03 begin session\n" );                     
        
        printf( "03 remote peer ip %s , port %d \n" , 
                inet_ntoa( clientAddress.sin_addr ) , 
                ntohs( clientAddress.sin_port ) 
        );           
        
	    while( 1 ){	
            
    	    bzero( commandBuffer, sizeof( commandBuffer ) );
	        bzero( responseBuffer, sizeof( responseBuffer ) );	  	
	
        	if ( ( n = read( clientSocket, commandBuffer, sizeof( commandBuffer ) - 1 ) ) == 0 ){    
                close( clientSocket );            
                printf ( "33 end session\n" );                                     
                break;
	        }
	        else {
                printf ( "04 process session [%04d]\n", commandCntr++ );
                if ( strlen(commandBuffer) == 0 ) {
                    close( clientSocket );                                
                    printf ( "55 end session\n" );                                                                 
                    break;
                }

                makeLower( commandBuffer );
	            
	            processScpiCommand ( trim( commandBuffer ), responseBuffer );
    
	            if ( (n = write( clientSocket, responseBuffer, strlen( responseBuffer ) ) ) < 0 ) {	        
                    printf ( "04 error when sending response: %s\n", strerror (errno) );
	                exit (1);	      	      	   
	            } 
	            else {
                    printf( "12 SCPI [%s]->[%s]\n", commandBuffer, trim( responseBuffer)  );    
                }       
            }        
        } // of session while     
        commandCntr = 0;
     } // of server while
     return 0; 
}


// -------------- przydasie ----------------------------------------------------

void makeLower ( char *s ) {
    for(int i = 0; s[ i ]; i++ ) {
      s[ i ] = tolower( s[ i ] );
    }   
}


// ----------- obce z sieci ----------------------------------------------------
// 
// zapożyczone:
// https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
// 
char *trim(char *str)
{
    size_t len = 0;
    char *frontp = str;
    char *endp = NULL;

    if( str == NULL ) { return NULL; }
    if( str[0] == '\0' ) { return str; }

    len = strlen(str);
    endp = str + len;

    /* Move the front and back pointers to address the first non-whitespace
     * characters from each end.
     */
    while( isspace((unsigned char) *frontp) ) { ++frontp; }
    if( endp != frontp )
    {
        while( isspace((unsigned char) *(--endp)) && endp != frontp ) {}
    }

    if( str + len - 1 != endp )
            *(endp + 1) = '\0';
    else if( frontp != str &&  endp == frontp )
            *str = '\0';

    /* Shift the string so that it starts at str so that if it's dynamically
     * allocated, we can still free it on the returned pointer.  Note the reuse
     * of endp to mean the front of the string buffer now.
     */
    endp = str;
    if( frontp != str )
    {
            while( *frontp ) { *endp++ = *frontp++; }
            *endp = '\0';
    }
    return str;
}
// koniec zapożyczen
//
