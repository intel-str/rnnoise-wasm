# Rnnoise-wasm Demo
A web based audio noise cancellation demo using WebAssembly translated from Xiph/Rnnoise, a recurrent neural netork based noise reduction library in C++.
The C++ library and Wasm web-version accepts raw audio streams in Float32Arrays and applies denoising based on an included RNN model. Xiph/Rnnoise native has re training capabilities and option to provide custom models for de-noising. 

# Instructions for using the demo
WebAssembly modules and HTML/JS glue code is provided. The web demo makes use of WebAudio for capturing audio stream through the browser and decoding WAV audio files. Browser support for WebAudio expected.

## Pre requisites
1. Python 2.7 or above on the local machine and Python executable in Env path.
2. WebAudio and Wasm supported web-browser (preferrably Edge/Chrome/Firefox)

## Steps
1. Download / Clone contents of this repo to a local directory.
2. Navigate to the directory containing AudioStream.html.
3. Run `python -m SimpleHTTPServer` from a Terminal/Command prompt.
4. Navigate to Http://localhost:8080
5. Click on AudioStream.html
  * Click on 'Start Recording' and allow microphone access if prompted. This will record audio including ambient noise.
  * Click 'Stop Recording' once finished.
6. Alternatively Upload any single channel wav audio file for denoising pre recorded files.
7. Use 'Play Recording' or 'Play Denoised' buttons to playback recorded audio and denoised audio respectively.

# Instructions for modifying the demo

The C++ source of the demo is included in 'Examples' directory. 

To modify 
1. Replace contents of 'rnnoise/examples' with the code in 'Examples' directory. 
2. Follow instructions from in the submodule to build Native demo.
3. Use emscripten to build the Web demo.

Example: `emcc -g -O3  -s ALLOW_MEMORY_GROWTH=1 -s EXPORT_ALL=1  -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' -I ../include/ ../src/*.c ../examples/*.c -o rnn_denoise.js`
To optimize for worklets: `emcc -g -O3 -s BINARYEN_ASYNC_COMPILATION=0 -s SINGLE_FILE=1  -s ALLOW_MEMORY_GROWTH=1 -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' -I ../include/ ../src/*.c ../examples/*.c --post-js denoise-worklet.js -o denoise-wasm-worklet.js`
