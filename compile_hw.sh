#!/bin/bash

# Parallel build script for hardware examples using tmux sessions
# This script builds each hardware example in separate tmux sessions

set -e  # Exit on any error

# Get the base directory from user input or use current directory
if [ -z "$1" ]; then
    echo "Usage: $0 <path_to_coyote_directory>"
    exit 1
fi

BASE_DIR="$1"

# Check if the base directory exists
if [ ! -d "$BASE_DIR" ]; then
    echo "Error: Base directory $BASE_DIR does not exist"
    exit 1
fi

# Check if tmux is installed
if ! command -v tmux &> /dev/null; then
    echo "Error: tmux is not installed. Please install tmux first."
    exit 1
fi

# Specify the hardware examples to build
examples=("audio" "digi_sign" "secure" "secure2" "speech" "baseline")

echo "Starting parallel builds for hardware examples in $BASE_DIR"
echo "=========================================================="

# Navigate to the examples_hw directory
cd "$BASE_DIR/examples_hw/"

# Loop through each specified example and create tmux sessions
for name in "${examples[@]}"; do
    session_name="build_${name}_hw"
    
    echo "Starting build for $name in tmux session: $session_name"
    
    # Kill any existing session with the same name
    tmux kill-session -t "$session_name" 2>/dev/null || true
    
    # Create new tmux session and run the build commands
    tmux new-session -d -s "$session_name" -c "$BASE_DIR/examples_hw/" bash -c "
        echo 'Building hardware example: $name'
        echo 'Session: $session_name'
        echo '=========================='
        
        # Clean up any existing build directory
        rm -rf '$name'
        
        # Create new build directory
        mkdir '$name'
        cd '$name'
        
        # Source Xilinx shell environment
        xilinx-shell
        
        # Configure with CMake
        echo 'Running cmake...'
        cmake ../ -DEXAMPLE='$name' -DFDEV_NAME=u280
        
        # Build the project
        echo 'Running make project...'
        make project
        
        # Generate bitstream (this takes a long time)
        echo 'Running make bitgen... (this may take several hours)'
        make bitgen
        
        echo 'Build completed for $name!'
        echo 'Press any key to exit this session'
        read -n 1
    "
    
    sleep 1  # Brief delay between session creation
done

echo ""
echo "All build sessions started successfully!"
echo ""
echo "To monitor the builds:"
echo "  tmux list-sessions                    # List all sessions"
echo "  tmux attach -t build_<example>_hw    # Attach to a specific build"
echo "  tmux attach -t build_audio_hw        # Example: attach to audio build"
echo ""
echo "Active build sessions:"
for name in "${examples[@]}"; do
    echo "  - build_${name}_hw"
done
echo ""
echo "Note: Bitstream generation can take several hours per example."
echo "You can safely close this terminal - the builds will continue in tmux."