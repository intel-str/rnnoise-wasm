#include <stdio.h>
#include <string.h>
#include "rnnoise.h"
#include <stdlib.h>
#include <stdint.h>
#include <emscripten.h>
// #define EMSCRIPTEN_KEEPALVE
// #define USE_WAV_MP3_LIBRARIES

#define _min(a, b)                    (((a) < (b)) ? (a) : (b))
#define _max(a, b)                    (((a) > (b)) ? (a) : (b))
#define _clamp(x, lo, hi)             (_max((lo), _min((hi), (x))))

#ifdef USE_WAV_MP3_LIBRARIES
#define DR_MP3_IMPLEMENTATION

#include "dr_mp3.h"

#define DR_WAV_IMPLEMENTATION

#include "dr_wav.h"
#endif //USE_WAV_MP3_LIBRARIES

#if   defined(__APPLE__)
# include <mach/mach_time.h>
#elif defined(_WIN32)
# define WIN32_LEAN_AND_MEAN

# include <windows.h>

#else // __linux

# include <time.h>

# ifndef  CLOCK_MONOTONIC //_RAW
#  define CLOCK_MONOTONIC CLOCK_REALTIME
# endif
#endif

static
uint64_t nanotimer() {
    static int ever = 0;
#if defined(__APPLE__)
    static mach_timebase_info_data_t frequency;
    if (!ever) {
        if (mach_timebase_info(&frequency) != KERN_SUCCESS) {
            return 0;
        }
        ever = 1;
    }
    return  (mach_absolute_time() * frequency.numer / frequency.denom);
#elif defined(_WIN32)
    static LARGE_INTEGER frequency;
    if (!ever) {
        QueryPerformanceFrequency(&frequency);
        ever = 1;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (t.QuadPart * (uint64_t) 1e9) / frequency.QuadPart;
#else // __linux
    struct timespec t = {0};
    if (!ever) {
        if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
            return 0;
        }
        ever = 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec * (uint64_t) 1e9) + t.tv_nsec;
#endif
}

static double now() {
    static uint64_t epoch = 0;
    if (!epoch) {
        epoch = nanotimer();
    }
    return (nanotimer() - epoch) / 1e9;
}

static double calcElapsed(double start, double end) {
    double took = -start;
    return took + end;
}

#ifdef USE_WAV_MP3_LIBRARIES

void wavWrite_s16(char *filename, float *buffer, int sampleRate, uint32_t totalSampleCount, uint32_t channels) {
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = channels;
    format.sampleRate = (drwav_uint32) sampleRate;
    format.bitsPerSample = 16;
    short *buffer_16=  (short*) buffer;
    for (int32_t i = 0; i < totalSampleCount; ++i) {
        buffer_16[i] = drwav_clamp(buffer[i], -32768, 32767);
    }
    drwav *pWav = drwav_open_file_write(filename, &format);
    if (pWav) {
        drwav_uint64 samplesWritten = drwav_write(pWav, totalSampleCount, buffer);
        drwav_uninit(pWav);
        if (samplesWritten != totalSampleCount) {
            fprintf(stderr, "write file [%s] error.\n", filename);
            exit(1);
        }
    }
}
 
void wavWrite_f32(char *filename, float *buffer, int sampleRate, uint32_t totalSampleCount, uint32_t channels) {
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = channels;
    format.sampleRate = (drwav_uint32) sampleRate;
    format.bitsPerSample = 32;
    for (int32_t i = 0; i < totalSampleCount; ++i) {
        buffer[i] = drwav_clamp(buffer[i], -32768, 32767) * (1.0f / 32768.0f);
    }
    drwav *pWav = drwav_open_file_write(filename, &format);
    if (pWav) {
        drwav_uint64 samplesWritten = drwav_write(pWav, totalSampleCount, buffer);
        drwav_uninit(pWav);
        if (samplesWritten != totalSampleCount) {
            fprintf(stderr, "write file [%s] error.\n", filename);
            exit(1);
        }
    }
}

float *wavRead_f32(const char *filename, uint32_t *sampleRate, uint64_t *sampleCount, uint32_t *channels) {
    drwav_uint64 totalSampleCount = 0;
    float *input = drwav_open_file_and_read_pcm_frames_f32(filename, channels, sampleRate, &totalSampleCount);
    if (input == NULL) {
        drmp3_config pConfig;
        input = drmp3_open_file_and_read_f32(filename, &pConfig, &totalSampleCount);
        if (input != NULL) {
            *channels = pConfig.outputChannels;
            *sampleRate = pConfig.outputSampleRate;
        }
    }
    if (input == NULL) {
        fprintf(stderr, "read file [%s] error.\n", filename);
        exit(1);
    }
    *sampleCount = totalSampleCount * (*channels);
    for (int32_t i = 0; i < *sampleCount; ++i) {
        input[i] = input[i] * 32768.0f;
    }
    return input;
}

float *mem_wavRead_f32(const void* data, size_t dataSize, uint32_t *sampleRate, uint32_t *channels, uint64_t *sampleCount) {
    drwav_uint64 totalSampleCount = 0;
    float *input = drwav_open_memory_and_read_pcm_frames_f32(data, dataSize, channels, sampleRate, &totalSampleCount);
    
    *sampleCount = totalSampleCount * (*channels);
    
    for (int32_t i = 0; i < *sampleCount; ++i) {
        input[i] = input[i] * 32768.0f;
    }
    return input;
}



void splitpath(const char *path, char *drv, char *dir, char *name, char *ext) {
    const char *end;
    const char *p;
    const char *s;
    if (path[0] && path[1] == ':') {
        if (drv) {
            *drv++ = *path++;
            *drv++ = *path++;
            *drv = '\0';
        }
    } else if (drv)
        *drv = '\0';
    for (end = path; *end && *end != ':';)
        end++;
    for (p = end; p > path && *--p != '\\' && *p != '/';)
        if (*p == '.') {
            end = p;
            break;
        }
    if (ext)
        for (s = end; (*ext = *s++);)
            ext++;
    for (p = end; p > path;)
        if (*--p == '\\' || *p == '/') {
            p++;
            break;
        }
    if (name) {
        for (s = p; s < end;)
            *name++ = *s++;
        *name = '\0';
    }
    if (dir) {
        for (s = path; s < p;)
            *dir++ = *s++;
        *dir = '\0';
    }
}
#endif// USE_WAV_MP3_LIBRARIES


uint64_t Resample_f32(const float *input, float *output, int inSampleRate, int outSampleRate, uint64_t inputSize,
                      uint32_t channels
) {
    if (input == NULL)
        return 0;
    uint64_t outputSize = inputSize * outSampleRate / inSampleRate;
    if (output == NULL)
        return outputSize;
    double stepDist = ((double) inSampleRate / (double) outSampleRate);
    const uint64_t fixedFraction = (1LL << 32);
    const double normFixed = (1.0 / (1LL << 32));
    uint64_t step = ((uint64_t) (stepDist * fixedFraction + 0.5));
    uint64_t curOffset = 0;
    for (uint32_t i = 0; i < outputSize; i += 1) {
        for (uint32_t c = 0; c < channels; c += 1) {
            *output++ = (float) (input[c] + (input[c + channels] - input[c]) * (
                    (double) (curOffset >> 32) + ((curOffset & (fixedFraction - 1)) * normFixed)
            )
            );
        }
        curOffset += step;
        input += (curOffset >> 32) * channels;
        curOffset &= (fixedFraction - 1);
    }
    return outputSize;
}

void print(const float* buffer, const unsigned size, char str) {
  printf("\n-------------%c-------------\n\n", str);
  for(int j=0; j<10; ++j) {
    printf("\n");
    for(int i = 0; i<size/10; ++i) printf("%f, ", *(buffer++));
  }
  printf("\n-------------------------\n\n");
}
EMSCRIPTEN_KEEPALIVE
void denoise_proc(float *input, uint32_t sampleRate, uint32_t channels, uint64_t sampleCount) {
    int flag = 0;
    uint32_t targetFrameSize = 480;
    uint32_t targetSampleRate = 48000;
    uint32_t perFrameSize = sampleRate / 100;
    float *frameBuffer = (float *) malloc(sizeof(*frameBuffer) * (channels + 1) * targetFrameSize);
    float *processBuffer = frameBuffer + targetFrameSize * channels;
    DenoiseState **sts = malloc(channels * sizeof(DenoiseState *));
    if (sts == NULL || frameBuffer == NULL) {
        if (sts)
            free(sts);
        if (frameBuffer)
            free(frameBuffer);
        fprintf(stderr, "malloc error.\n");
        return;
    }
    for (int i = 0; i < channels; i++) {
        sts[i] = rnnoise_create(NULL);
        if (sts[i] == NULL) {
            for (int x = 0; x < i; x++) {
                if (sts[x]) {
                    rnnoise_destroy(sts[x]);
                }
            }
            free(sts);
            free(frameBuffer);
            return;
        }
    }
    size_t frameStep = channels * perFrameSize;
    uint64_t frames = sampleCount / frameStep;
    uint64_t lastFrameSize = (sampleCount % frameStep) / channels;
    for (int i = 0; i < frames; ++i) {
        if(flag) print(input, 480, 'A');

        Resample_f32(input, frameBuffer, sampleRate, targetSampleRate,
                     perFrameSize, channels);
        if(flag) print(frameBuffer, 480, 'B');
        
        for (int c = 0; c < channels; c++) {
            for (int k = 0; k < targetFrameSize; k++)
                processBuffer[k] = frameBuffer[k * channels + c];
            rnnoise_process_frame(sts[c], processBuffer, processBuffer);
            for (int k = 0; k < targetFrameSize; k++)
                frameBuffer[k * channels + c] = processBuffer[k];
        }
        if(flag) print(frameBuffer, 480, 'C');

        Resample_f32(frameBuffer, input, targetSampleRate, sampleRate, targetFrameSize, channels);
        if(flag) { print(input, 480, 'D'); flag = 1; }

        input += frameStep;
    }
    if (lastFrameSize != 0) {
        memset(frameBuffer, 0, targetFrameSize * channels * sizeof(float));
        uint64_t lastReasmpleSize = Resample_f32(input, frameBuffer, sampleRate,
                                                 targetSampleRate,
                                                 lastFrameSize, channels);
        for (int c = 0; c < channels; c++) {
            for (int k = 0; k < targetFrameSize; k++)
                processBuffer[k] = frameBuffer[k * channels + c];
            rnnoise_process_frame(sts[c], processBuffer, processBuffer);
            for (int k = 0; k < targetFrameSize; k++)
                frameBuffer[k * channels + c] = processBuffer[k];
        }
        Resample_f32(frameBuffer, input, targetSampleRate, sampleRate, lastReasmpleSize,
                     channels);
    }
    for (int i = 0; i < channels; i++) {
        if (sts[i]) {
            rnnoise_destroy(sts[i]);
        }
    }
    free(sts);
    free(frameBuffer);
}

int result[2];
uint32_t sampleRate = 0;
uint32_t channels = 0;
uint64_t sampleCount = 0;
float* result_buffer;

#ifdef USE_WAV_MP3_LIBRARIES

EMSCRIPTEN_KEEPALIVE
float* rnnDenoiseMem(void* audioData, size_t audioSize) {

    float* buffer = mem_wavRead_f32(audioData, audioSize, &sampleRate, &channels, &sampleCount);

    // print(buffer, 100, 'W');

    if(buffer != NULL) {
        denoise_proc(buffer, sampleRate, channels, sampleCount);
        // result[0] = (int) buffer;
        result_buffer = buffer;
        result[1] = sampleCount;
    }
    return buffer;
}

#endif //USE_WAV_MP3_LIBRARIES

EMSCRIPTEN_KEEPALIVE
float* rnnDenoise_rawmem(const float* audioData, const uint32_t in_sampleRate, const uint32_t in_channels, const uint32_t in_sampleCount) {

    sampleCount = in_sampleCount;
    sampleRate = in_sampleRate;
    channels = in_channels;

    float* buffer = (float *) malloc(sizeof(float) * sampleCount);

    // print(buffer, 100, 'B');
    for(int i = 0; i < sampleCount; ++i) {
        buffer[i] = audioData[i] * 32768.0f;
    }

    // print(buffer, 100, 'B');
    if(buffer != NULL) {

        denoise_proc(buffer, sampleRate, channels, sampleCount);

        // print(buffer, 100, 'A');
        //condition output
        for(int i = 0; i < sampleCount; ++i) {
            buffer[i] = _clamp(buffer[i], -32768, 32767) * (1.0f / 32768.0);
        }

        // print(buffer, 100, 'A');
        //Store output
        // result[0] = (int) buffer;
        result_buffer = buffer;
        result[1] = sampleCount;
    }
    return buffer;
}

double time_interval = 0;
EMSCRIPTEN_KEEPALIVE
float* rnnDenoise_rawmem_perf(const float* audioData, const uint32_t in_sampleRate, const uint32_t in_channels, const uint32_t in_sampleCount) {
    float *result;

    double startTime = now();
    result = rnnDenoise_rawmem_perf(audioData, in_sampleRate, in_channels, in_sampleCount);
    time_interval = calcElapsed(startTime, now());
    return result;
}
EMSCRIPTEN_KEEPALIVE 
double get_rnnDenoise_rawmem_time() { return time_interval;}


EMSCRIPTEN_KEEPALIVE 
int getResultPointer() { return result[0];}
EMSCRIPTEN_KEEPALIVE 
int getResultSize() { return result[1];}
EMSCRIPTEN_KEEPALIVE 
int getsampleRate() { return sampleRate;}
EMSCRIPTEN_KEEPALIVE 
int getchannels() { return channels;}
EMSCRIPTEN_KEEPALIVE 
int getsampleCount() { return (int) sampleCount;}
EMSCRIPTEN_KEEPALIVE 
void freeBuffer() { free(result_buffer);}

#ifdef USE_WAV_MP3_LIBRARIES

void rnnDeNoise(char *in_file, char *out_file) {

    FILE *fp = fopen(in_file, "rb");
    fseek(fp, 0, SEEK_END);
    size_t fSize = ftell(fp);
    rewind(fp);


    char audioData[fSize];
    int rStat = fread(audioData, sizeof(char), fSize, fp);
    if(rStat < 1) {printf("\nFileReadError: %d\n", rStat);}
    int audioDataSize = fSize;


    double startTime = now();
    float* buffer = rnnDenoiseMem(audioData, audioDataSize);
    double time_interval = calcElapsed(startTime, now());
    printf("Performance:\n\n");
    printf("AudioFile:\t\t%s\n", in_file);
    printf("SampleRate:\t\t%d\n", sampleRate);
    printf("DataSize (bytes):\t%ld\n", sampleCount);

    printf("Time interval (ms):\t%f\n ", (time_interval * 1000));
    printf("Kbps:\t\t\t%f\n ",(sampleCount)/(time_interval * 1000));
    if(buffer) {
        wavWrite_s16(out_file, buffer, sampleRate, (uint32_t) result[1], channels);
        free(buffer);
    }
}

#endif //USE_WAV_MP3_LIBRARIES 
 

int main(int argc, char **argv) {

#ifdef USE_WAV_MP3_LIBRARIES
    if(argc < 3) {
        printf("Please provide audio file to process. For Example \n");
        printf("./rnnoise file.wav outfile.wav\n");
        return -1;
    }

    char *inFile = argv[1];
    char *outFile = argv[2];

    rnnDeNoise(inFile, outFile);
#endif
    return 0;
}