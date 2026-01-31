#!/bin/bash

# Convert MP3 files from ./sounds to Opus in ./data, mirroring folder structure

find sounds -name "*.mp3" | while read -r mp3_file; do
    # Create mirrored path in data directory
    opus_file="data/${mp3_file#sounds/}"
    opus_file="${opus_file%.mp3}.opus"

    # Create parent directory
    mkdir -p "$(dirname "$opus_file")"

    echo "Converting: $mp3_file -> $opus_file"
    ffmpeg -i "$mp3_file" -c:a libopus -b:a 32k -ac 1 -ar 16000 "$opus_file" -y -loglevel error
done

echo "Done!"
