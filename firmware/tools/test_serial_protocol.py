import serial
import threading
import sys
import time
import argparse

def read_from_serial(ser):
    """Continuously reads lines from the serial port and prints them."""
    while True:
        try:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    # Clear the current prompt line, print the received message, and reprint the prompt
                    sys.stdout.write(f"\r\033[K[Board] {line}\n> ")
                    sys.stdout.flush()
            time.sleep(0.01)
        except serial.SerialException as e:
            print(f"\nSerial error: {e}")
            break
        except TypeError:
            # Serial port closed
            break

def main():
    parser = argparse.ArgumentParser(description="Test KMS Serial Protocol")
    parser.add_argument("port", help="The serial port to connect to (e.g., COM3 or /dev/ttyUSB0)")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=1)
        print(f"Connected to {args.port} at {args.baudrate} baud.")
        print("Type commands to send (e.g., 'GOTO:3', 'ACTUATE:1', 'BATT:?', 'STATUS:?'). Type 'exit' to quit.")
    except serial.SerialException as e:
        print(f"Failed to open port {args.port}: {e}")
        return

    # Start a background thread to read incoming messages
    reader_thread = threading.Thread(target=read_from_serial, args=(ser,), daemon=True)
    reader_thread.start()

    try:
        while True:
            # Get user input
            cmd = input("> ").strip()
            
            if cmd.lower() in ['exit', 'quit']:
                break
            
            if cmd:
                # Add newline as the protocol expects lines
                command_str = cmd + "\n"
                ser.write(command_str.encode('utf-8'))
                
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        ser.close()
        print("Serial port closed.")

if __name__ == "__main__":
    main()
