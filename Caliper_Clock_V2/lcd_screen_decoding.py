"""
ESP32 LCD Screen Decoding - GPIO Pin Toggle Controller
Toggles GPIO pins on and off once per second for LCD screen interface
"""

import machine
import time

class LCDScreenDecoding:
    def __init__(self, pin1=2, pin2=4):
        """
        Initialize the LCD screen decoding controller
        
        Args:
            pin1 (int): First GPIO pin number (default: 2)
            pin2 (int): Second GPIO pin number (default: 4)
        """
        self.pin1 = machine.Pin(pin1, machine.Pin.OUT)
        self.pin2 = machine.Pin(pin2, machine.Pin.OUT)
        
        # Initialize pins to low state
        self.pin1.off()
        self.pin2.off()
        
        self.state = False
        
        print(f"LCD Screen Decoding initialized")
        print(f"GPIO Pin 1: {pin1}")
        print(f"GPIO Pin 2: {pin2}")
    
    def toggle_pins(self):
        """Toggle both GPIO pins to opposite states"""
        if self.state:
            self.pin1.on()
            self.pin2.off()
            print("Pins toggled: Pin1=HIGH, Pin2=LOW")
        else:
            self.pin1.off()
            self.pin2.on()
            print("Pins toggled: Pin1=LOW, Pin2=HIGH")
        
        # Flip state for next iteration
        self.state = not self.state
    
    def run_continuous(self):
        """Run continuous toggle loop with 1 second intervals"""
        print("Starting continuous GPIO toggle (0.02 second intervals - 50 Hz frequency)")
        print("Press Ctrl+C to stop")
        
        try:
            while True:
                self.toggle_pins()
                time.sleep(0.25)  # 0.02 second delay (50 Hz frequency)
        except KeyboardInterrupt:
            print("\nStopping GPIO toggle")
            self.cleanup()
    
    def run_cycles(self, cycles=10):
        """
        Run a specific number of toggle cycles
        
        Args:
            cycles (int): Number of toggle cycles to run
        """
        print(f"Running {cycles} toggle cycles")
        
        for i in range(cycles):
            print(f"Cycle {i+1}/{cycles}")
            self.toggle_pins()
            time.sleep(0.2)  # 0.02 second delay (50 Hz frequency)
        
        self.cleanup()
    
    def cleanup(self):
        """Turn off both pins and cleanup"""
        self.pin1.off()
        self.pin2.off()
        print("GPIO pins turned off")

def main():
    """Main function to run the LCD screen decoding"""
    # Initialize with default pins (GPIO 2 and GPIO 4)
    # You can change these pin numbers as needed
    lcd_decoder = LCDScreenDecoding(pin1=2, pin2=4)
    
    # Option 1: Run continuously (uncomment to use)
    lcd_decoder.run_continuous()
    
    # Option 2: Run for specific number of cycles (uncomment to use)
    # lcd_decoder.run_cycles(20)

if __name__ == "__main__":
    main()
