# flutter_recorder

A low-level audio recorder plugin which uses miniaudio as backend and supporting all the platforms. It can detect silence and save to WAV audio file. Audio wave and FFT data can be get in real-time as for the volume level.

[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)

|Linux|Windows|Android|MacOS (under test)|iOS (under test)|web|
|:-:|:-:|:-:|:-:|:-:|:-:|
|💙|💙|💙|💙|💙|💙|

## Select features:
- Support all the platforms
- High performance using miniaudio C library with FFI
- Record audio in WAV format with pause support
- Choose recording device
- Silence catch detection via callback or Stream
- Set the threshold for what is considered "silence"
- Set the duration of silence after which the recorder pauses
- Set the amount of time to keep before the recording starts again
- Get volume, audio and FFT data in real-time

## Setup permissions
After setting up permission for your Android, MacOS, or iOS, in your app, you will need to ask for permission to use the microphone for example using [permission_handler](https://pub.dev/packages/permission_handler) plugin.
https://pub.dev/packages/permission_handler

#### Android
Add the permission in the `AndroidManifest.xml`.
```
<uses-permission android:name="android.permission.RECORD_AUDIO" />
```

#### MacOS, iOS
Add the permission in `Runner/Info.plist`.
```
<key>NSMicrophoneUsageDescription</key>
<string>Some message to describe why you need this permission</string>
```

#### Web
Add this in `web/index.html` under the `<head>` tag.
```
<script src="assets/packages/flutter_recorder/web/libflutter_recorder_plugin.js" defer></script>
```

## Usage
```dart
import 'package:permission_handler/permission_handler.dart';
[...]
/// If you are running on Android, MacOS or iOS, ask the permission to use the microphone:
if (defaultTargetPlatform == TargetPlatform.android ||
    defaultTargetPlatform == TargetPlatform.iOS ||
    defaultTargetPlatform == TargetPlatform.macOS) {
    Permission.microphone.request().isGranted.then((value) async {
    if (!value) {
        await [Permission.microphone].request();
    }
});

/// Initialize the capture device and start listening to it:
try {
    Recorder.instance.init();
    Recorder.instance.startListen();
} on Exception catch (e) {
    debugPrint('init() error: $e\n');
}
/// On Web platform it is better to initialize and wait the user to give
/// mic permission. Then use `startListen()` when it's needed.

//Start recording:
Recorder.instance.startRecording(completeFilePath: 'audioCompleteFilenameWithPath.wav`);

/// Stop recording:
Recorder.instance.stopRecording();
```
Using `Recorder.instance.listCaptureDevices()` you can have a list of available capture devices and then pass the optional `deviceID` to `Recorder.instance.init()`.

---
It is possible to detect silence and skip it while recording:
```dart
Recorder.instance.setSilenceDetection(
    enable: true,
    onSilenceChanged: (isSilent, decibel) {
        /// Here you can check if silence is changed.
        /// Or you can do the same thing with the Stream
        /// [Recorder.instance.silenceChangedEvents]
    },
);
/// the silence threshold in dB. A volume under this value is considered to be silence.
Recorder.instance.setSilenceThresholdDb(-27);
/// the value in seconds of silence after which silence is considered as such.
Recorder.instance.setSilenceDuration(0.5);
/// Set seconds of audio to write before starting recording again after silence.
Recorder.instance.setSecondsOfAudioToWriteBefore(0.0);
```
---
It is also possible to get audio data, FFT, and volume:
```dart
/// Get the current volume in dB in the [-100, 0] range.
double volume = Recorder.instance.getVolumeDb();
/// Return a 256 float array containing wave data in the range [-1.0, 1.0] not clamped.
Float32List waveAudio = Recorder.instance.getWave();
/// Return a 256 float array containing FFT data in the range [-1.0, 1.0] not clamped.
Float32List fftAudio = Recorder.instance.getFft();
```
![Image](https://github.com/alnitak/flutter_recorder/raw/main/images/audio_data.png)
