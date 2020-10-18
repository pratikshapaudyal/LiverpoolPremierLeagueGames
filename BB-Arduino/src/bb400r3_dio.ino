
/*
    ASCII commands to support: (x=done)
 X    %aannttccff    Set device config: nn ignored, cc settable but ignored, ff bits 7 and 5 implemented, but 6 ignored
 X    $aa2    Read device config => !aa40ccff
 X    #aa1cdd Set the state of one digital output
 X    #aaAcdd Set the state of one digital output
 X    #aaBcdd Set the state of one digital output (upper 8 channels: always gives '?' response)
 X    @aa(Data) Set the state of all the digital outputs
 X    @aa     Reads the Digital I/O Status
 X    ~aaO(Data) Set module name, min 1 char, max 10 chars, else response is ?aa
 X    $aaM    Read module name (as set above)
 X    $aaM0 Read module type (constant)
 X    $aaF  Read firmware version
 X    $aaS1 Restore Factory Defaults
 X    $aaRS Reset module
 X    $aaLs Reads the Latched DI Status
 X    $aaCn Clears the Digital Input Counter
 X    $aa2    Reads the Module Configuration
 X    $aa6    Reads the Digital I/O Status
 X    #aan    Reads the Digital Input Counter: response is !aadddd or !aadddddddd
 X    $aaC    Clears the Latched DI Status
 X    ~aaXCn      Read debounce time of channel n
 X    ~aaXCntttt  Set debounce time of channel n to tttt milliseconds: tttt is in hex.

NB: No support for:
        Changing the ASCII device address (aa): it is hard-coded as 01
        Checksum on ASCII commands and responses
        Configuring the overload-detection blanking time.  May need to be higher for capacitive loads?
*/

#define FIRMWARE_VERSION_STRING "1.0.13"

/*#define ARDUINO_BOARD_PROTOTYPE*/

#include "DigitalInputOutput.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>

#ifdef ARDUINO_BOARD_PROTOTYPE
const int dout_pin[8] = { 2, 3, 4, 5, 6, 7, 8, 9 };
const int din_pin[8]  = { 2, 3, 4, 5, 6, 7, 8, 9 };
# else
const int dout_pin[8] = {  9, 10, 5, 4, 13,  6, 11, 12 };
const int din_pin[8]  = { A4, A5, 1, 0, A0, A1, A2, A3 };
#endif

const int commandRxMaxLen = 40; // limit memory used if a large number of bytes is received without a newline
const int moduleNameMaxLen = 10;



// offsets into EEPROM
enum { 
    eepaddr_modulename = 8, 
    eepaddr_config_ff = eepaddr_modulename + moduleNameMaxLen,
    eepaddr_config_cc,
    eepaddr_debouncetime, // 16 bytes here to store the debounce timer settings, 2 bytes per input channel
    eepaddr_next = eepaddr_debouncetime + 2*8, // if we add any more stored settings, start putting them at this offset
};

// bits in config_ff
enum {
    config_ff_mask_counter_risingedge = _BV(7),
    config_ff_mask_counter_32bit = _BV(5),
};

String commandRx;
String module_name;

DigitalInputOutput digital_io[8];
uint8_t config_ff, config_cc;

// using volatile keyword because these variables are changed inside interrupts
uint8_t count = 0;
// push notification flag
volatile bool push = false;

void initialise_settings(){       
    // set up inputs and outputs
    for (int i = 0; i < 8; i++) {
        digital_io[i].set_input_pin(din_pin[i]);
        digital_io[i].set_output_pin(dout_pin[i]);
        digital_io[i].push_flag = false;
    }
    // set initial state
    set_dout_all(0);    
    apply_settings_from_eeprom();
}

void setup() 
{
    {
        // disable JTAG port so that we get PF7:4 available
        uint8_t m = MCUCR;
        m = m | 0x80;
        MCUCR = m;
        MCUCR = m;
        MCUCR = m;
    }
    
    initialise_settings();

    // Timer0 is already set up for a 1ms period, and is used for millis() - we'll 
    // just also enable a just "Compare A" interrupt to go off somewhere within
    // the cycle, and call the SIGNAL(TIMER0_COMPA_vect) function below
    OCR0A = 0xAF;
    TIMSK0 |= _BV(OCIE0A);
        
    // start USB virtual serial port and wait for port to open:
    Serial.begin(921600);

    commandRx = String();
    commandRx.reserve(commandRxMaxLen+1);
}


 
// This interrupt handler routine is called once a millisecond
SIGNAL(TIMER0_COMPA_vect) 
{
//  int ttime = micros();
    count ++;
    for (int i = 0; i < 8; i++) 
    {
        digital_io[i].update();
        // send push notification every 100 ms if the push_flag is high
        // this is to reduce the number of messages flowing through the USB bus.
        if (count % 100 == 0 && digital_io[i].push_flag)
        {
          push = true;            
          digital_io[i].push_flag = false;
        }
    }    
//    int isr_time = micros() - ttime;
//    Serial.println(isr_time);
    
}

void set_dout_all(uint8_t setval) 
{
    // set the actual state of all the digital outputs (bit set => driven low)
    for (int i = 0; i < 8; i++) {
        digital_io[i].set_dout_state((setval & (1<<i)) != 0);
    }
}

void push_notify()
{    
    // 32 bit counts each need 10 characters, 16 bit counts need 5 characters
    // 96 characters required (10 * 8 for counts, 5 for IO states, 9 for '$' symbol, 1 for carriage return and 1 for null character)
    char buffer[96];
    uint32_t count[8];
    uint16_t dio_states = get_dio_states();

    for (int i = 0 ; i < 8; i++)
    {
      count[i] = digital_io[i].read_counter();
    }

    snprintf(buffer, sizeof buffer, "$%04X$%lu$%lu$%lu$%lu$%lu$%lu$%lu$%lu\r", dio_states, count[0], count[1], count[2], count[3], count[4], count[5], count[6], count[7]);
    Serial.print(buffer);
    push = false;
}

uint16_t get_dio_states()
{
    uint16_t dio_status = 0;
    
    for (int i = 7; i >= 0; i--) {
        dio_status = dio_status << 1;
        if (digital_io[i].get_dout_state())
            dio_status |= 0x0100;

        if (digital_io[i].get_din_state())
            dio_status |= 0x0001;
    }
    return dio_status;
}

void loop() 
{
    static char prevByte = 0;    
    // it is not good practice to have Serial.print() inside the interrupt service routine.
    // Therefore, push_notify is now inside the loop.
    if (push)
    {
      push_notify();
    }  
    if (Serial.available() == 0) {
        return;
    }
    char inByte = Serial.read();

    if ((inByte==13 && prevByte==10) || (inByte==10 && prevByte==13)) {
        // second byte of a CF,LF or LF, CR sequence - ignore it
        prevByte = 0; // don't ignore the next one
        return;
    } else if (inByte==8 || inByte==127) {
        // handle Delete (useful for terminal access)
        int cmdLen = commandRx.length();
        if (cmdLen > 0)
            commandRx.remove(cmdLen-1);
    } else if (inByte==0x1A) { // discard the whole command received so far
        // handle Escape (useful for terminal access)
        commandRx = String("");
    } else if (inByte==13 || inByte==10) {
        handle_received_command();
        commandRx = String("");
        commandRx.reserve(commandRxMaxLen+1);
    } else if (commandRx.length() > commandRxMaxLen) {
        commandRx = String("???");
    } else {
        commandRx += inByte;
    }
    prevByte = inByte;     
}

void apply_settings_from_eeprom() 
{
    // read configuration (cc and ff bytes in %aannttccff command) from EEPROM
    config_ff = (EEPROM.read(eepaddr_config_ff) ^ 0xff) & 0xa0; // only bits 7 and 5 settable
    config_cc = EEPROM.read(eepaddr_config_cc);
    if (config_cc < 3 || config_cc > 0x0A)
        config_cc = 0x0A;

    for (int i = 0; i < 8; i++) {
        digital_io[i].set_counter_edge((config_ff & config_ff_mask_counter_risingedge) != 0);
        uint16_t debounce_milliseconds;
        debounce_milliseconds = EEPROM.read(eepaddr_debouncetime + 2*i) | (EEPROM.read(eepaddr_debouncetime + 2*i + 1) << 8);
        // we invert the bytes from the EEPROM so that the reset value of all-bytes-0xff gives a zero debounce time instead of a huge one
        debounce_milliseconds = debounce_milliseconds ^ 0xffff;
        digital_io[i].set_debounce_time(debounce_milliseconds);
    }
    
    // read module name from EEPROM
    module_name = String();
    module_name.reserve(moduleNameMaxLen);
    for (unsigned int i = 0; i < moduleNameMaxLen; i++) {
        char c = EEPROM.read(eepaddr_modulename + i);
        if (c < 32 || c > 126)
            break;
        module_name.concat(c);
    }
    if (module_name.length() == 0)
        module_name = String("BB-400"); // default if nothing in EEPROM
}

void handle_received_command() 
{
    // first check that we have at least 3 chars received, and the 2nd and third match our device address
    if (commandRx.length()<3 || commandRx.charAt(1) != '0' || commandRx.charAt(2) != '1') { 
        // no response, it's not addressed to this module

    } else if (commandRx.charAt(0) == '+') {
//      if (commandRx.length() == 3)
        digital_io[0].push_flag = true;
        
    } else if (commandRx.charAt(0) == '%') {
        
        // -- %aannttccff : configure device
        if (commandRx.length()==11 && substring_is_valid_hex(commandRx, 3, 8)) 
            command_percent_aannttccff(commandRx);

        // -- unrecognised command
        else
            send_error_response_query();

    } else if (commandRx.charAt(0) == '$') {
        
        // -- $aaM : Read module name
        if (commandRx.length()==4 && commandRx.charAt(3) == 'M')
            command_dollar_M();
        
        // -- $aaM0 : Read module type
        else if (commandRx.length()==5 && commandRx.charAt(3) == 'M' && commandRx.charAt(4) == '0')
            command_dollar_M0(); 
        
        // -- $aaF : Read firmware version
        else if (commandRx.length()==4 && commandRx.charAt(3) == 'F')
            command_dollar_F(); 
        
        // -- $aaCn : read counter n
        else if (commandRx.length() == 5 && commandRx.charAt(3) == 'C' && substring_is_valid_hex(commandRx, 4, 1))
            command_dollar_Cn(commandRx); 
        
        // -- $aaL0, $aaL1 : Reads the Latched DI Status
        else if (commandRx.length()==5 && commandRx.charAt(3) == 'L'
            && (commandRx.charAt(4) == '0' || commandRx.charAt(4) == '1')
        )
            command_dollar_Lx(commandRx); 
        
        // -- $aaC : Clears the Latched DI Status
        else if (commandRx.length()==4 && commandRx.charAt(3) == 'C')
            command_dollar_C();
        
        // -- $aa2 : Read module configuration
        else if (commandRx.length()==4 && commandRx.charAt(3) == '2')
            command_dollar_2();
        
        // -- $aa6 : Reads the Digital I/O Status. Returns '!xxyy00' where xx is the output state, yy is the input state
        else if (commandRx.length()==4 && commandRx.charAt(3) == '6')
            command_dollar_6();

        // -- $aaS1 : Restore Factory Defaults
        else if (commandRx.length()==5 && commandRx.charAt(3) == 'S' && commandRx.charAt(4) == '1')
            command_dollar_S1();
        
        // -- $aaRS : Reset module
        else if (commandRx.length()==5 && commandRx.charAt(3) == 'R' && commandRx.charAt(4) == 'S') 
            command_dollar_RS();
        
        // -- unrecognised $ command
        else
            send_error_response_query();

    } else if (commandRx.charAt(0) == '@') {
        
        // -- @aa : Reads the Digital I/O Status. Returns '>xxyy' where xx is the output state, yy is the input state
        if (commandRx.length() == 3)
            command_at_nothing();
        
        // -- @aaxx : Sets the Digital output line state to xx
        else if (commandRx.length() == 5 && substring_is_valid_hex(commandRx, 3, 2))
            command_at_xx(commandRx);

        // -- @aaxxyy : Sets the Digital output line state to xx (yy is ignored)
        else if (commandRx.length() == 7 && substring_is_valid_hex(commandRx, 3, 4))
            command_at_xx(commandRx);
        
        // -- unrecognised command
        else 
            send_error_response_query();
        
    } else if (commandRx.charAt(0) == '#') {

        // -- #aan : read counter n
        if (commandRx.length() == 4 && substring_is_valid_hex(commandRx, 3, 1))
            command_hash_n(commandRx);

        // -- #aa1cdd, #aaAcdd, #aaBcdd : set a single digital output
        else if (commandRx.length() == 7
            && (commandRx.charAt(3) == '1' || commandRx.charAt(3) == 'A' || commandRx.charAt(3) == 'B')
            && substring_is_valid_hex(commandRx, 4, 1)
        )
            command_hash_xcdd(commandRx);        
        
        // -- unrecognised command
        else
            send_error_response_query();

    } else if (commandRx.charAt(0) == '~') {
        
        // -- ~aaO(Data) : Set module name, min 1 char, max moduleNameMaxLen chars, else response is ?aa
        if (commandRx.length()>4 && commandRx.length()<=4+moduleNameMaxLen && commandRx.charAt(3) == 'O')
            command_tilde_O(commandRx);

        // -- ~aaXCntttt :   Set debounce time of channel n to tttt milliseconds: tttt is in hex.
        else if (commandRx.length() == 10 && commandRx.charAt(3) == 'X' && commandRx.charAt(4) == 'C' && substring_is_valid_hex(commandRx, 5, 5))
            command_tilde_XCntttt(commandRx);
        
        // -- ~aaXCn :   Read debounce time of channel n
        else if (commandRx.length() == 6 && commandRx.charAt(3) == 'X' && commandRx.charAt(4) == 'C' && substring_is_valid_hex(commandRx, 5, 1))
            command_tilde_XCn(commandRx);
        
        // -- unrecognised command
        else 
            send_error_response_query();
    }

}

void command_percent_aannttccff(String &commandRx) 
{
    // -- %aannttccff : configure device
    uint8_t config_tt = substring_read_hex(commandRx, 5, 2);
    uint8_t new_config_cc = substring_read_hex(commandRx, 7, 2);
    if (config_tt != 0x40 || new_config_cc < 3 || new_config_cc > 0x0A) {
        send_error_response_query();
        return;
    }
    config_cc = new_config_cc;
    config_ff = substring_read_hex(commandRx, 9, 2) & 0xa0; // only bits 7 and 5 settable at the moment
    // make the changes immediately
    for (int i = 0; i < 8; i++) {
        digital_io[i].set_counter_edge((config_ff & config_ff_mask_counter_risingedge) != 0);
    }
    // save the changes in the EEPROM
    EEPROM.update(eepaddr_config_ff, config_ff ^ 0xff); // stored as inverse so that as-new state has these bits==0
    EEPROM.update(eepaddr_config_cc, config_cc);
    send_ok_response_pling();
}

void command_dollar_M()
{
    // -- $aaM : Read module name
    send_ok_response_header_pling();
    Serial.print(module_name + "\r");
}

void command_dollar_M0()
{
    // -- $aaM0 : Read module type
    Serial.print(F("!01BB-400\r")); 
}

void command_dollar_F()
{
    // -- $aaF : Read firmware version
    send_ok_response_header_pling();
    Serial.print(F(FIRMWARE_VERSION_STRING )); 
    Serial.print("\r");
}
           
void command_dollar_Cn(String &commandRx) 
{
    // -- $aaCn : reset counter n
    uint8_t chan = substring_read_hex(commandRx, 4, 1);
    if (chan < 8) {
        digital_io[chan].reset_counter();
        send_ok_response_pling();
        // send push notification when the counter is cleared
        digital_io[chan].push_flag = true;
        //The counter values are not updated on the BB-400 unitl the next push notification is received.
        // Maybe the push notification is sent before the counter is reset.
    }
    else
        send_error_response_query();
}

void command_dollar_Lx(String &commandRx)
{ 
    // -- $aaL0, $aaL1 : Reads the Latched DI Status
    bool high_not_low = commandRx.charAt(4) == '1';
    uint8_t latchdata = 0;
    for (int i = 7; i >= 0; i--) {
        latchdata = latchdata << 1;
        if (digital_io[i].read_latch_status(high_not_low))
            latchdata = latchdata | 1;
    }
    char buffer[9];
    snprintf(buffer, sizeof buffer, "!00%02X00\r", latchdata);
    Serial.print(buffer);
}

void command_dollar_C()
{ 
    // -- $aaC : Clears the Latched DI Status
    for (int i = 7; i >= 0; i--) {
        digital_io[i].clear_latch_status();
    }
    send_ok_response_pling();
}

void command_dollar_2()
{ 
    // -- $aa2 : Read module configuration
    char buffer[9];
    send_ok_response_header_pling();
    snprintf(buffer, sizeof buffer, "40%02X%02X\r", config_cc, config_ff);
    Serial.print(buffer);
}

void command_dollar_S1()
{ 
    // -- $aaS1 : Restore Factory Defaults
    // when new, the EEPROM contains all FFs, so FF must be handled as a reset state
    EEPROM.update(eepaddr_modulename, 0xff);
    EEPROM.update(eepaddr_config_ff, 0xff);
    EEPROM.update(eepaddr_config_cc, 0xff);
    for (int i = 0; i < 8; i++) {
        EEPROM.update(eepaddr_debouncetime + 2*i, 0xff);
        EEPROM.update(eepaddr_debouncetime + 2*i + 1, 0xff);
    }
    apply_settings_from_eeprom();
    send_ok_response_pling();
}

void command_dollar_RS()
{ 
    // -- $aaRS : Reset module
    // Generate a full reset by enabling the system watchdog and letting it expire
    // This means the device disappears from USB for a while, which may be something we don't want...
    //MCUSR = 0;    // clear out any flags of prior resets.
    //wdt_enable(WDTO_15MS); // turn on the Watchdog and don't stroke it.
    //delay(1000);
    //while (1) {} // infinite loop, to wait for the Watchdog to expire
    // NB deliberately no response sent

    initialise_settings();
    
}


void command_at_nothing()
{
    // -- @aa : Reads the Digital I/O Status. Returns '>xxyy' where xx is the output state, yy is the input state
    uint16_t dio_status = 0;
    char buffer[7];
    for (int i = 7; i >= 0; i--) {
        dio_status = dio_status << 1;
        if (digital_io[i].get_dout_state())
            dio_status |= 0x0100;

        if (digital_io[i].get_din_state())
            dio_status |= 0x0001;
    }
    snprintf(buffer, sizeof buffer, ">%04X\r", dio_status);
    Serial.print(buffer);
}

void command_dollar_6()
{
    // -- $aa6 : Reads the Digital I/O Status. Returns '!xxyy00' where xx is the output state, yy is the input state
    uint16_t dio_status = 0;
    char buffer[9];
    for (int i = 7; i >= 0; i--) {
        dio_status = dio_status << 1;
        if (digital_io[i].get_dout_state())
            dio_status |= 0x0100;

        if (digital_io[i].get_din_state())
            dio_status |= 0x0001;
    }
    snprintf(buffer, sizeof buffer, "!%04X00\r", dio_status);
    Serial.print(buffer);
}

void command_at_xx(String &commandRx)
{
    // -- @aaxx : Sets the Digital output line state to xx
    set_dout_all(substring_read_hex(commandRx, 3, 2));
    send_ok_response_gt();
}

void command_hash_n(String &commandRx)
{
    // -- #aan : read counter n
    uint8_t chan = substring_read_hex(commandRx, 3, 1);
    if (chan < 8) {
        char buffer[12];
        send_ok_response_header_pling();
        if (config_ff & config_ff_mask_counter_32bit) {
            snprintf(buffer, sizeof buffer, "%010lu\r", digital_io[chan].read_counter());
        } else {
            uint16_t reduced_count = digital_io[chan].read_counter() & 0xffff;
            snprintf(buffer, sizeof buffer, "%05u\r", reduced_count);
        }
        Serial.print(buffer);
    }
    else
        send_error_response_query();
}

void command_hash_xcdd(String &commandRx)      
{
    // -- #aa1cdd, #aaAcdd, #aaBcdd : set a single digital output
    uint8_t chan = substring_read_hex(commandRx, 4, 1);
    if (chan < 8 && commandRx.charAt(3) != 'B' && commandRx.charAt(5) == '0'
        && (commandRx.charAt(6) == '0' || commandRx.charAt(6) == '1')
    ) {
        digital_io[chan].set_dout_state(commandRx.charAt(6) == '1');
        send_ok_response_gt();
    }
    else {
        Serial.print(F("?\r")); 
    }
}

void command_tilde_O(String &commandRx)
{ 
    // -- ~aaO(Data) : Set module name, min 1 char, max moduleNameMaxLen chars, else response is ?aa
    // check the new name given is valid
    bool new_name_valid = 1;
    bool has_nonspace_char = 0;
    for (unsigned int i = 4; i < commandRx.length(); i++) {
        char c = commandRx.charAt(i);
        if (c < 32 || c > 126 || c == '#' || c == '%' || c == '$' || c == '~') {
            new_name_valid = 0;
            break;
        }
        if (c != ' ')
            has_nonspace_char = 1;
    }

    if (new_name_valid && has_nonspace_char) {
        module_name = commandRx.substring(4);
        save_module_name(); // write it to EEPROM
        send_ok_response_pling();
    }
    else {
        send_error_response_query();
    }
}

void command_tilde_XCntttt(String &commandRx)
{
    // -- ~aaXCntttt : Set debounce time of channel n to tttt milliseconds: tttt is in hex.
    uint8_t chan = substring_read_hex(commandRx, 5, 1);
    uint16_t debounce_milliseconds = substring_read_hex(commandRx, 6, 4);
    if (chan < 8 && debounce_milliseconds <= 4000) {
        digital_io[chan].set_debounce_time(debounce_milliseconds);
        // we invert the bytes in the EEPROM so that the reset value of all-bytes-0xff gives a zero debounce time instead of a huge one
        debounce_milliseconds = debounce_milliseconds ^ 0xffff;
        EEPROM.update(eepaddr_debouncetime + 2*chan, debounce_milliseconds & 0xff);
        EEPROM.update(eepaddr_debouncetime + 2*chan + 1, debounce_milliseconds >> 8);
        
        send_ok_response_pling();
    }
    else
        send_error_response_query();
}

void command_tilde_XCn(String &commandRx)
{
    // -- ~aaXCn : Read debounce time of channel n
    uint8_t chan = substring_read_hex(commandRx, 5, 1);
    if (chan < 8) {
        char buffer[8];
        send_ok_response_header_pling();
        snprintf(buffer, sizeof buffer, "%04X\r", digital_io[chan].get_debounce_time());
        Serial.print(buffer);
    }
    else
        send_error_response_query();
}

void send_ok_response_header_pling() 
{
    Serial.print(F("!01")); 
}

void send_ok_response_pling() 
{
    // https://en.wikipedia.org/wiki/Exclamation_mark#Slang_and_other_names_for_the_exclamation_mark
    Serial.print(F("!01\r")); 
}

void send_ok_response_gt() 
{
    Serial.print(F(">\r")); 
}

void send_error_response_query() 
{

    Serial.print(F("?01\r")); 
}

void save_module_name() {
    for (unsigned int i = 0; i < moduleNameMaxLen; i++) {
        if (i < module_name.length()) 
            EEPROM.update(eepaddr_modulename + i, module_name.charAt(i));
        else {
            EEPROM.update(eepaddr_modulename + i, 0);
            break;
        }
    }
}

boolean substring_is_valid_hex(String strin, int offset, int len) {
    int maxpos = strin.length();
    for (int i = offset; i<offset+len && i<maxpos; i++) {
        char c = strin.charAt(i);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') )) {
            return false;
        }
    }
    return true; 
}

uint32_t substring_read_hex(String strin, int offset, int len) {
    uint32_t result = 0;
    int maxpos = strin.length();
    for (int i = offset; i<offset+len && i<maxpos; i++) {
        char c = strin.charAt(i);
        if (c >= '0' && c <= '9') {
            result = (result << 4) | (c - '0');
        }
        else {
            result = (result << 4) | ((0x0A + c - 'A') & 0x0f);
        }
    }
    return result;
}