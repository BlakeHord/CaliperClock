#!/bin/bash
# ESP32 Upload Script
# Make sure your ESP32 is connected via USB and has MicroPython firmware

# Configuration
ESP32_PORT=""
DEFAULT_FILE="lcd_driver.py"

# Available Python files in project
AVAILABLE_FILES=("lcd_driver.py" "lcd_screen_decoding.py")

# Auto-detect ESP32 port (common patterns)
echo "🔍 Detecting ESP32 port..."
for port in /dev/tty.SLAB_USBtoUART* /dev/tty.usbserial-* /dev/tty.wchusbserial* /dev/tty.USB*; do
    if [ -e "$port" ]; then
        ESP32_PORT="$port"
        echo "✅ Found ESP32 at: $ESP32_PORT"
        break
    fi
done

if [ -z "$ESP32_PORT" ]; then
    echo "❌ ESP32 not found. Available ports:"
    ls /dev/tty.* 2>/dev/null | grep -E "(USB|serial|SLAB)"
    echo ""
    echo "Manual usage: $0 /dev/tty.your_port_here"
    exit 1
fi

# Determine which file to upload and if we should run it
PYTHON_FILE="$DEFAULT_FILE"
RUN_AFTER_UPLOAD=false

# Check for command line arguments
if [ ! -z "$1" ]; then
    # Check for "run" command first
    if [[ "$1" == "run" ]]; then
        RUN_AFTER_UPLOAD=true
        # Second argument should be filename
        if [ ! -z "$2" ]; then
            PYTHON_FILE="$2"
        fi
        # Third argument could be port
        if [ ! -z "$3" ]; then
            ESP32_PORT="$3"
            echo "📌 Using manual port: $ESP32_PORT"
        fi
    elif [[ "$1" == /dev/* ]]; then
        # First argument is a port
        ESP32_PORT="$1"
        echo "📌 Using manual port: $ESP32_PORT"
        # Second argument might be file or "run"
        if [[ "$2" == "run" ]]; then
            RUN_AFTER_UPLOAD=true
            if [ ! -z "$3" ]; then
                PYTHON_FILE="$3"
            fi
        elif [ ! -z "$2" ]; then
            PYTHON_FILE="$2"
            # Third argument might be "run"
            if [[ "$3" == "run" ]]; then
                RUN_AFTER_UPLOAD=true
            fi
        fi
    else
        # First argument is filename
        PYTHON_FILE="$1"
        # Second argument might be port or "run"
        if [[ "$2" == "run" ]]; then
            RUN_AFTER_UPLOAD=true
        elif [[ "$2" == /dev/* ]]; then
            ESP32_PORT="$2"
            echo "📌 Using manual port: $ESP32_PORT"
            # Third argument might be "run"
            if [[ "$3" == "run" ]]; then
                RUN_AFTER_UPLOAD=true
            fi
        fi
    fi
fi

# Validate file exists
if [ ! -f "$PYTHON_FILE" ]; then
    echo "❌ File '$PYTHON_FILE' not found!"
    echo ""
    echo "📁 Available Python files:"
    for file in "${AVAILABLE_FILES[@]}"; do
        if [ -f "$file" ]; then
            echo "   ✅ $file"
        else
            echo "   ❌ $file (missing)"
        fi
    done
    echo ""
    echo "Usage examples:"
    echo "  $0                              # Upload $DEFAULT_FILE"
    echo "  $0 filename.py                  # Upload specific file"
    echo "  $0 run filename.py              # Upload and run specific file"
    echo "  $0 /dev/tty.port filename.py    # Use specific port and file"
    echo "  $0 filename.py /dev/tty.port    # Upload file to specific port"
    exit 1
fi

# Get file size for progress indicator
FILE_SIZE=$(wc -c < "$PYTHON_FILE" | tr -d ' ')
echo ""
echo "🚀 Uploading $PYTHON_FILE to ESP32..."
echo "Port: $ESP32_PORT"
echo "File size: ${FILE_SIZE} bytes"
echo ""

# Function to show progress spinner
show_progress() {
    local pid=$1
    local delay=0.1
    local spinstr='|/-\'
    local temp
    
    echo -n "📤 Uploading "
    while [ "$(ps a | awk '{print $1}' | grep $pid)" ]; do
        temp=${spinstr#?}
        printf " [%c]  " "$spinstr"
        local spinstr=$temp${spinstr%"$temp"}
        sleep $delay
        printf "\b\b\b\b\b\b"
    done
    printf "    \b\b\b\b"
    echo ""
}

# Upload the file with progress indicator
ampy --port "$ESP32_PORT" put "$PYTHON_FILE" &
UPLOAD_PID=$!

# Show progress while uploading
show_progress $UPLOAD_PID

# Wait for upload to complete and get exit status
wait $UPLOAD_PID
UPLOAD_RESULT=$?

if [ $UPLOAD_RESULT -eq 0 ]; then
    echo "✅ Upload successful!"
    echo ""
    echo "📋 Files on ESP32:"
    ampy --port "$ESP32_PORT" ls
    echo ""
    echo "▶️  To run: ampy --port $ESP32_PORT run $PYTHON_FILE"
    echo "🖥️  To enter REPL: screen $ESP32_PORT 115200"
    echo ""
    echo "🎯 Quick commands:"
    echo "   ./upload_to_esp32.sh                          # Upload $DEFAULT_FILE"
    echo "   ./upload_to_esp32.sh lcd_screen_decoding.py    # Upload GPIO toggle script"
    echo "   ./upload_to_esp32.sh run lcd_driver.py         # Upload and run LCD driver"
    echo "   ./upload_to_esp32.sh run lcd_screen_decoding.py # Upload and run GPIO toggle"
else
    echo "❌ Upload failed!"
    echo "💡 Try: esptool.py --port $ESP32_PORT chip_id"
fi

# Run immediately after upload if requested
if [ "$RUN_AFTER_UPLOAD" = true ]; then
    echo ""
    echo "🏃‍♂️ Running $PYTHON_FILE immediately..."
    ampy --port "$ESP32_PORT" run "$PYTHON_FILE"
fi
