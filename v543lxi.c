/*

kompilacja:
  g++ -o v543 -lwiringPi v543.c
  
uruchomienie:
  ./v543
  
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
#define FIRMWARE_VERSION    "666-tasza-2018"


char* trim(char*);  // proto, kod niżej
void handlerIdn(char*);
void handlerRst(char*); 
void handlerSenseMode(char*);
void handlerVRange(char*);
void handlerRRange(char*);
void handlerRaw(char*);
void handlerDisplay(char*);
void handlerSystemError(char*); 
void handlerExit(char*); 

void handlerMeasureVoltage(char*);
void handlerMeasureResistance(char*);
void handlerSenseVoltageRange(char*);
void handlerSenseResistanceRange(char*);


// prototyp handlerka komendy scpi, po prostu wypełnia wynik w out i tyle
typedef void (*TScpiCommandHandler)(char*);

// parka komenda-handler
typedef struct {
    const char *cmd;
    TScpiCommandHandler handler;
} TCommand;

// na informacje o zakresie
typedef struct {
    const char  *label;
    float scale;
    const char  *format;    
} TRangeInfo;


unsigned char   uchLedReady = 0;
unsigned char   uchLedScpi = 0;
unsigned long   ulRawMeterData = 0L;
unsigned char   uchRangeId = 0;
unsigned char   uchModeId = 0;
unsigned char   uchPolarity = 0;


// to póki co obsługujemy
TCommand scpiCommands[] = {
    {   "*idn?",                    &handlerIdn }, 
    
    {   ":measure:voltage:dc?",     &handlerMeasureVoltage },
    {   ":measure:voltage:ac?",     &handlerMeasureVoltage },
    {   ":measure:resistance?",     &handlerMeasureResistance },            
    {   ":sense:voltage:dc:range?", &handlerSenseVoltageRange },
    {   ":sense:voltage:ac:range?", &handlerSenseVoltageRange },
    {   ":sense:resistance:range?", &handlerSenseResistanceRange },        
    
    {   ":sense:mode?",             &handlerSenseMode },
    
    {   ":measure:raw?",            &handlerRaw },
    {   ":measure:display?",        &handlerDisplay }, 
    
    {   ":syst:err?",               &handlerSystemError },
    {   "syst:err?",                &handlerSystemError },     
    {   NULL ,                      NULL }
};

TRangeInfo volRangeInfo[] = {
    {   "100",       100   ,       "%c%-6.2f\n"     },  //0  119.99
    {   "1",         10000 ,       "%c%-5.4f\n"     },  //1  1.1999 
    {   "1000",      10    ,       "%c%-6.1f\n"     },  //2  1199.9 
    {   "10",        1000  ,       "%c%-5.3f\n"     },  //3  11.999
    {   "0.1",       100000,       "%c%-6.5f\n"     },  //4  0.11999
    {   "error",      1     , "\n" },  //5
    {   "error",      1     , "\n" },  //6
    {   "error",      1     , "\n" }   //7
};

TRangeInfo resRanges[] = {
    {   "100000",      100   , ""},  //0
    {   "1000",        10000 , ""},  //1
    {   "error",      1     , ""},  //2
    {   "10000",       1000  , ""},  //3
    {   "error",      1     , ""},  //4
    {   "1000000",        10000 , ""},  //5
    {   "error",      1     , ""},  //6
    {   "10000000",       1000  , ""}   //7
};


const char *pszModeDesc[] = {
      "error",    // 0
      "R",        // 1
      "AC",       // 2
      "error",    // 3
      "DC"        // 4        
};

    
void makeLower ( char *s ) {
    for(int i = 0; s[ i ]; i++ ) {
      s[ i ] = tolower( s[ i ] );
    }   
}


// identyfikacja urządzenia 
void handlerSystemError(char *out) {    
    strcpy ( out, "0,\"No error\"\n" );
}


// identyfikacja urządzenia 
void handlerIdn(char *out) {    
    sprintf ( out, 
              "%s,%s,%s,%s\n",
              DEVICE_VENDOR,
              DEVICE_NAME,
              DEVICE_SERIAL,
              FIRMWARE_VERSION
    );    
}

// R,AC,DC
void handlerSenseMode(char *out) {    
    sprintf( out, "%d|%s\n", uchModeId, pszModeDesc[ uchModeId ] );
}

// V,mV
void handlerVRange(char *out) {    
   // sprintf( out, "%d|%s|%d\r\n", uchRangeId, volRanges[ uchRangeId ].label, volRanges[ uchRangeId ].scale );        
}

// kohm, Mohm
void handlerRRange(char *out) {    
 //   sprintf( out, "%d|%s|%d\r\n", uchRangeId, resRanges[ uchRangeId ].label, resRanges[ uchRangeId ].scale );            
}

void handlerMeasureResistance (char *out) {
}

void handlerSenseResistanceRange (char *out) {
}
void handlerSenseVoltageRange (char *out) {
}

int getNumericDisplay( void ) {
    char s[16];
    sprintf( s, "%05X", ulRawMeterData &0x1FFFF );        
    return atoi( s );
}
    
void handlerMeasureVoltage (char *out) { 
    if ( uchModeId != 4 /*DC*/ && uchModeId != 2 /*AC*/) {
        strcpy ( out, "error\n" );            
        return;
    }
    char sign = ' ';
    if ( uchModeId == 4 /*DC*/){
        sign = uchPolarity == 1 ? '+' : '-';
    }    
    float v = ((float)getNumericDisplay()) / volRangeInfo[ uchRangeId ].scale;
    sprintf( 
        out, 
        volRangeInfo[ uchRangeId ].format, 
        sign,
        v
    );            
}


//------------------------------------------------------------------------
// odsyła znak i pięć cyferek wyświetlacza
void handlerDisplay(char *out) {
    char sign = ' ';
    if ( uchModeId == 4 /*DC*/){
        sign = uchPolarity == 1 ? '+' : '-';
    }
    sprintf( out, "%c%05X\r\n", sign, ulRawMeterData &0x1FFFF );    
} 

// 32 bity hex
void handlerRaw(char *out) {
    sprintf( out, "%08X\n", ulRawMeterData );    
}


// rozpoznanie i wykoannie polecenia SCPI
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


// :) żywcem zerżnięte z dawnego Arduino
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
     int cntr = 0;
     
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
       
        printf( "10 waiting for connection [%04d]\n", cntr++ );
	
        if ( ( clientSocket = accept( serverSocket, (struct sockaddr *)&clientAddress, (socklen_t*)&clientAddressLen) ) < 0 ) {	        
            printf ( "02 error on accept incoming connection: %s\n", strerror (errno) );                     
	        exit (1);
	    }
        
        printf ( "03 begin session\n" );                     
        
        //getpeername( clientSocket , (struct sockaddr*)&clientAddress , (socklen_t*)&clientAddressLen );   
        printf("Host connected , ip %s , port %d \n" , inet_ntoa(clientAddress.sin_addr) , ntohs(clientAddress.sin_port));           
        
	    while( 1 ){	
            
    	    bzero( commandBuffer, sizeof( commandBuffer ) );
	        bzero( responseBuffer, sizeof( responseBuffer ) );	  	
	
        	if ( ( n = read( clientSocket, commandBuffer, sizeof( commandBuffer ) - 1 ) ) == 0 ){    
                close( clientSocket );            
                printf ( "33 end session\n" );                                     
                break;
	        }
	        else {
                printf ( "04 process session\n" );                                             

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
     } // of server while
     return 0; 
}

// ----------- obce z sieci ----------------
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
