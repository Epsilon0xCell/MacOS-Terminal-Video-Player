# macos-terminal-video-high-quality

brew install ffmpeg


# Compile Command
```
g++ src/main.cpp src/video.cpp -Iinc/ -I/opt/homebrew/include -std=c++17 -O3 -L/opt/homebrew/lib -lavformat -lavcodec -lavutil -lswscale -o tvp
```
