#include <Bounce2.h>
#include <Arduino.h>

class DigitalInputOutput
{
    // private member variables

    const uint8_t overload_blanking_ms = 5; // How many milliseconds to wait after switching an output line ON, before starting to check that the input signal is low
    
    int din_pin;
    int dout_pin;
    Bounce debouncer;
    uint16_t debounce_time;
    bool high_latched, low_latched;
    bool count_rising_edges;    
    bool dout_state;
    bool din_state;
    uint8_t dout_overload_timer;    
    uint32_t edge_count;
 

public:
    
    volatile bool push_flag;  
    DigitalInputOutput() 
    {
        din_pin = -1;
        debounce_time = 0;
        high_latched = 0;
        low_latched = 0;
        edge_count = 0;
        count_rising_edges = 1;
        debouncer = Bounce();
        dout_state = false;
        // beware powering on when in NPN
        din_state = false;
        dout_overload_timer = 0;
    }


    void set_input_pin(int pin)
    {
        din_pin = pin;
        pinMode(din_pin, INPUT);
        debouncer.attach(din_pin);
        debouncer.interval(debounce_time);
    }

    void set_output_pin(int pin)
    {
        dout_pin = pin;
        digitalWrite(dout_pin, LOW);
        #ifdef ARDUINO_BOARD_PROTOTYPE
            pinMode(dout_pin, INPUT);
        #else
            pinMode(dout_pin, OUTPUT);
        #endif
        dout_state = false;
        dout_overload_timer = 0;
    }

    void set_debounce_time(uint16_t new_debounce_time)
    {
        debounce_time = new_debounce_time;
        // allow for the fact that the timer is actually called at 976.5625Hz, not 1.000kHz
        if (debounce_time == 0)
            debouncer.interval(0);
        else
            debouncer.interval((uint16_t)(1.0 + 0.9765625 * (float) debounce_time));
    }
    
    uint16_t get_debounce_time()
    {
        return debounce_time;
    }

    void set_counter_edge(bool is_rising)
    {
        count_rising_edges = is_rising;
    }

    uint32_t read_counter() 
    {
        // disable the timer interrupt while we read the counter, to prevent it changing while we are reading it
        uint8_t timsk0_was = TIMSK0;
        TIMSK0 &= ~_BV(OCIE0A);
        uint32_t the_count = edge_count;
        TIMSK0 = timsk0_was;
        return the_count;
    }

    void reset_counter() 
    {
        // disable the timer interrupt while we read the counter, to prevent it changing while we are part-way through clearing it
        uint8_t timsk0_was = TIMSK0;
        TIMSK0 &= ~_BV(OCIE0A);
        edge_count = 0;
        TIMSK0 = timsk0_was;
    }
    
    bool get_din_state()
    {
        if (debounce_time == 0)
            return digitalRead(din_pin);
        else
            return debouncer.read();
    }
    
    bool get_dout_state()
    {
        return dout_state;
    }
    
    bool read_latch_status(bool high_not_low)
    {
        return high_not_low ? high_latched : low_latched;
    }
    
    void clear_latch_status()
    {
        high_latched = false;
        low_latched = false;
    }
    
    void set_dout_state(bool newstate) 
    {
        // when dout_state is 1, the DIO pin should be at a low voltage
        // although there may be a delay, after dout_state goes 0->1, before the input pin goes low
        // so we need to suppress the check during this time.  Use a counter which starts at (blanking time in ms)
        // and is decremented every 1 ms; once it reaches zero, checking is enabled

        if (newstate && !dout_state) {
            // dout_state is going from OFF to ON, so restart the overload blanking timer
            // NB here I'm relying on the fact that since dout_overload_timer is a unit8_t,
            //  it will be updated atomically.  If a larger size is used, there is a risk that
            //  the IRQ code will decrement the value in the middle of us setting it, so we 
            //  should disable interrupts while doing this assignment.
            dout_overload_timer = overload_blanking_ms;
                       
        }    
        if (newstate) {
            // output transistor ON, drive the signal low
    #ifdef ARDUINO_BOARD_PROTOTYPE
            pinMode(dout_pin, OUTPUT);
    #else
            digitalWrite(dout_pin, HIGH);
    #endif
          
        } else {
            // output transistor OFF, let the signal float
    #ifdef ARDUINO_BOARD_PROTOTYPE
            pinMode(dout_pin, INPUT);
    #else
            digitalWrite(dout_pin, LOW);
    #endif
        }
        
        if (newstate != dout_state)
        {
          push_flag = true;
        }
        dout_state = newstate;        
    }
    
    void update()
    {
        // debounce
        debouncer.update();
        
        // counter
        if (count_rising_edges) {
            if (debouncer.rose())
                edge_count ++;
        }
        else {
            if (debouncer.fell())
                edge_count ++;
        }
        
        // level latches
        bool din = debouncer.read();
        if (din)
            high_latched = 1;
        else
            low_latched = 1;

        if (din != din_state)
        {
          din_state = din;
          push_flag = true;
         
        }

        // overload detection
        if (dout_overload_timer > 0) {
            // still in blanking period
            dout_overload_timer --;
        } else {
            // blanking period has expired: check for overload dout_state
            if (dout_state && digitalRead(din_pin)) {
                // output is ON but connected input pin is High: this indicates
                // a driver overload.  Turn off the output to prevent oscillation
                set_dout_state(false);
            }
        }
    }
};