#include "capture.h"
#include "circular_buffer.h"

#include <cstdarg>
#include <memory.h>
#include <memory>
#include <cmath>
#include <atomic>
#include <time.h>
#include "fft/soloud_fft.h"

#ifdef _IS_WIN_
#define CLOCK_REALTIME 0
// struct timespec { long long tv_sec; long tv_nsec; };    //header part
// Windows is not POSIX compliant. Implement this.
int clock_gettime(int, struct timespec *spec)      //C-file part
{  __int64 wintime; GetSystemTimeAsFileTime((FILETIME*)&wintime);
   wintime      -=116444736000000000i64;  //1jan1601 to 1jan1970
   spec->tv_sec  =wintime / 10000000i64;           //seconds
   spec->tv_nsec =wintime % 10000000i64 *100;      //nano-seconds
   return 0;
}
#endif

void getTime(struct timespec *time)
{
    if (clock_gettime(CLOCK_REALTIME, time) == -1)
    {
        perror("clock getTime");
        exit(EXIT_FAILURE);
    }
}

/// returns the elapsed time in seconds
double getElapsed(struct timespec since)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1)
    {
        perror("clock getTime");
        exit(EXIT_FAILURE);
    }
    return ((double)(now.tv_sec - since.tv_sec) +
            (double)(now.tv_nsec - since.tv_nsec) / 1.0e9L);
}

// 1024 means 1/(44100*2)*1024 = 0.0116 ms
#define BUFFER_SIZE 1024      // Buffer length
#define MOVING_AVERAGE_SIZE 4 // Moving average window size
float capturedBuffer[BUFFER_SIZE];
std::atomic<bool> is_silent{true};    // Initial state
bool delayed_silence_started = false;  // Whether the silence is delayed
std::atomic<float> energy_db{-100.0f}; // Current energy

/// the buffer used for capturing audio.
std::unique_ptr<CircularBuffer> circularBuffer;

// Function to convert energy to decibels
float energy_to_db(float energy)
{
    return 10.0f * log10f(energy + 1e-10f); // Add a small value to avoid log(0)
}

void calculateEnergy(float *captured, ma_uint32 frameCount)
{
    static float moving_average[MOVING_AVERAGE_SIZE] = {0}; // Moving average window
    static int average_index = 0;                           // Circular buffer index
    float sum = 0.0f;

    // Calculate the average energy of the current buffer
    for (int i = 0; i < frameCount; i++)
    {
        sum += captured[i] * captured[i];
    }
    float average_energy = sum / frameCount;

    // Update the moving average window
    moving_average[average_index] = average_energy;
    average_index = (average_index + 1) % MOVING_AVERAGE_SIZE; // Circular buffer cycle

    // Calculate the moving average
    float moving_average_sum = 0.0f;
    for (int i = 0; i < MOVING_AVERAGE_SIZE; i++)
    {
        moving_average_sum += moving_average[i];
    }
    float smoothed_energy = moving_average_sum / MOVING_AVERAGE_SIZE;

    // Convert energy to decibels
    energy_db = energy_to_db(smoothed_energy);
}

void detectSilence(Capture *userData)
{
    static struct timespec startSilence; // Start time of silence
    // Check if the signal is below the silence threshold
    if (energy_db < userData->silenceThresholdDb)
    {
        if (!is_silent.load() && !delayed_silence_started)
        {
            getTime(&startSilence);
            // Transition: Sound -> Silence
            is_silent = true;
        }
        else
        {
            double elapsed = getElapsed(startSilence);
            if (elapsed >= userData->silenceDuration && is_silent.load() && !delayed_silence_started)
            {
                printf("Silence started after %f s. Level in dB: %.2f\n", elapsed, energy_db.load());
                /// empty capturedBuffer
                if (circularBuffer && circularBuffer.get()->size() > BUFFER_SIZE)
                    circularBuffer.get()->pop(circularBuffer.get()->size());
                delayed_silence_started = true;
                if (nativeSilenceChangedCallback != nullptr)
                {
                    float energy_value = energy_db.load();
                    nativeSilenceChangedCallback(&delayed_silence_started, &energy_value);
                }
            }
        }
    }
    else
    {
        if (is_silent.load())
        {
            double elapsed = getElapsed(startSilence);
            if (elapsed >= userData->silenceDuration && delayed_silence_started)
            {
                // Transition: Silence -> Sound
                printf("Sound started after %f s. Level in dB: %.2f   %f %f %f\n", elapsed, energy_db.load(),
                       userData->silenceThresholdDb, userData->silenceDuration, userData->secondsOfAudioToWriteBefore);
                is_silent = false;
                delayed_silence_started = false;
                // Write all the circularBuffer data which contains the audio occurred before the silence ended.
                if (userData->isRecording && userData->secondsOfAudioToWriteBefore > 0 && circularBuffer)
                {
                    ma_uint32 frameCount = (unsigned int)(circularBuffer.get()->size());
                    auto data = circularBuffer.get()->pop(frameCount);
                    // printf("WRITE secondsOfAudioToWriteBefore buffer size: %u  frames: %u  frame got: %u\n",
                    //    circularBuffer.get()->size(), frameCount, data.size());
                    // The framCount in wav.write is one for all the channels.
                    userData->wav.write(data.data(), data.size() / userData->deviceConfig.capture.channels);
                }
                if (nativeSilenceChangedCallback != nullptr)
                {
                    float energy_value = energy_db.load();
                    nativeSilenceChangedCallback(&delayed_silence_started, &energy_value);
                }
            }

            /// Reset the clock if sound happens during the deley after a silence,
            if (elapsed < userData->silenceDuration && is_silent.load())
            {
                getTime(&startSilence);
                is_silent = false;
                delayed_silence_started = false;
            }
        }
    }
}

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    // Process the captured audio data as needed.
    float *captured = (float *)(pInput); // Assuming float format
    // Do something with the captured audio data...
    memcpy(capturedBuffer, captured, sizeof(float) * BUFFER_SIZE);

    Capture *userData = (Capture *)pDevice->pUserData;
    calculateEnergy(captured, frameCount);
    if (userData->isDetectingSilence)
    {
        detectSilence(userData);

        // Copy current buffer to circularBuffer
        if (delayed_silence_started && userData->isRecording && userData->secondsOfAudioToWriteBefore > 0) {
             std::vector<float> values(captured, captured + frameCount);
            circularBuffer.get()->push(values);
        }

        if (!delayed_silence_started && userData->isRecording && !userData->isRecordingPaused)
        {
            userData->wav.write(captured, frameCount);
        }
    }
    else
    {
        if (userData->isRecording && !userData->isRecordingPaused)
        {
            userData->wav.write(captured, frameCount);
        }
    }
}

// /////////////////////////////
// Capture class Implementation
// /////////////////////////////
float waveData[256];
Capture::Capture() : isDetectingSilence(false),
                     silenceThresholdDb(-40.0f),
                     silenceDuration(2.0f),
                     secondsOfAudioToWriteBefore(0.0f),
                     isRecording(false),
                     isRecordingPaused(false),
                     mInited(false)
{
    memset(waveData, 0, sizeof(float) * 256);
};

Capture::~Capture()
{
    dispose();
}

std::vector<CaptureDevice> Capture::listCaptureDevices()
{
    // printf("***************** LIST DEVICES START\n");
    std::vector<CaptureDevice> ret;
    if ((result = ma_context_init(NULL, 0, NULL, &context)) != MA_SUCCESS)
    {
        printf("Failed to initialize context %d\n", result);
        return ret;
    }

    if ((result = ma_context_get_devices(
             &context,
             &pPlaybackInfos,
             &playbackCount,
             &pCaptureInfos,
             &captureCount)) != MA_SUCCESS)
    {
        printf("Failed to get devices %d\n", result);
        return ret;
    }

    // Loop over each device info and do something with it. Here we just print
    // the name with their index. You may want
    // to give the user the opportunity to choose which device they'd prefer.
    for (ma_uint32 i = 0; i < captureCount; i++)
    {
        // printf("######%s %d - %s\n",
        //        pCaptureInfos[i].isDefault ? " X" : "-",
        //        i,
        //        pCaptureInfos[i].name);
        CaptureDevice cd;
        cd.name = strdup(pCaptureInfos[i].name);
        cd.isDefault = pCaptureInfos[i].isDefault;
        cd.id = i;
        ret.push_back(cd);
    }
    // printf("***************** LIST DEVICES END\n");
    return ret;
}

CaptureErrors Capture::init(
    int deviceID,
    unsigned int sampleRate,
    unsigned int channels)
{
    if (mInited)
        return captureInitFailed;
    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.periodSizeInFrames = BUFFER_SIZE;
    if (deviceID != -1)
    {
        auto devices = listCaptureDevices();
        if (devices.size() == 0 || deviceID >= devices.size())
            return captureInitFailed;
        deviceConfig.capture.pDeviceID = &pCaptureInfos[deviceID].id;
    }
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = channels;
    deviceConfig.sampleRate = sampleRate;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = this;

    result = ma_device_init(NULL, &deviceConfig, &device);
    if (result != MA_SUCCESS)
    {
        printf("Failed to initialize capture device.\n");
        return captureInitFailed;
    }
    mInited = true;
    return captureNoError;
}

void Capture::dispose()
{
    mInited = false;
    wav.close();
    circularBuffer.reset();
    isRecording = false;

    ma_device_uninit(&device);
}

bool Capture::isInited()
{
    return mInited;
}

bool Capture::isDeviceStartedListen()
{
    ma_device_state result = ma_device_get_state(&device);
    return result == ma_device_state_started;
}

CaptureErrors Capture::startListen()
{
    if (!mInited)
        return captureNotInited;

    result = ma_device_start(&device);
    if (result != MA_SUCCESS)
    {
        ma_device_uninit(&device);
        printf("Failed to start device.\n");
        return failedToStartDevice;
    }
    return captureNoError;
}

void Capture::stopListen()
{
    ma_device_uninit(&device);
    mInited = false;
}

void Capture::setSilenceDetection(bool enable)
{
    this->isDetectingSilence = enable;
}

void Capture::setSilenceThresholdDb(float silenceThresholdDb)
{
    this->silenceThresholdDb = silenceThresholdDb;
}

void Capture::setSilenceDuration(float silenceDuration)
{
    this->silenceDuration = silenceDuration;
}

void Capture::setSecondsOfAudioToWriteBefore(float secondsOfAudioToWriteBefore)
{
    this->secondsOfAudioToWriteBefore = secondsOfAudioToWriteBefore;
    ma_uint32 frameCount = (ma_uint32)(secondsOfAudioToWriteBefore * deviceConfig.capture.channels * deviceConfig.sampleRate);
    frameCount = (frameCount >> 1) << 1;
    if (!circularBuffer)
        circularBuffer.reset();
    circularBuffer = std::make_unique<CircularBuffer>(frameCount);
}

CaptureErrors Capture::startRecording(const char *path)
{
    if (!mInited)
        return captureNotInited;
    CaptureErrors result = wav.init(path, deviceConfig);
    if (result != captureNoError)
        return result;
    setSecondsOfAudioToWriteBefore(secondsOfAudioToWriteBefore);
    isRecording = true;
    isRecordingPaused = false;
    return captureNoError;
}

void Capture::setPauseRecording(bool pause)
{
    if (!mInited || !isRecording)
        return;
    isRecordingPaused = pause;
}

void Capture::stopRecording()
{
    if (!mInited || !isRecording)
        return;
    wav.close();
    circularBuffer.reset();
    isRecording = false;
}

float *Capture::getWave()
{
    // int n = BUFFER_SIZE >> 8;
    for (int i = 0; i < 256; i++)
    {
        waveData[i] = (capturedBuffer[i * 4] +
                       capturedBuffer[i * 4 + 1] +
                       capturedBuffer[i * 4 + 2] +
                       capturedBuffer[i * 4 + 3]) /
                      4;
    }
    return waveData;
}

float Capture::getVolumeDb()
{
    return energy_db;
}
