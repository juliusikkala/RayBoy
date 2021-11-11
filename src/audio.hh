#ifndef RAYBOY_AUDIO_HH
#define RAYBOY_AUDIO_HH
#include "soloud.h"
#include <stdint.h>
#include <vector>
#include <atomic>

class audio
{
public:
    audio();
    ~audio();

    uint32_t get_samplerate();
    uint32_t get_buffer_size();
    SoLoud::Soloud& get_soloud();

private:
    SoLoud::Soloud soloud;
};

// Assumes that reader and writer are in different threads, but there is only
// one of each. The ring buffer cannot be resized after creation. Assuming
// atomic_size_t is lockless, the algorithm is lockless.
class audio_ring_buffer
{
public:
    audio_ring_buffer(size_t sample_count, size_t channels);

    void pop(float* stream, size_t sample_count);
    void push(int16_t left, int16_t right);
    size_t get_unread_sample_count() const;

private:
    size_t read_head;
    size_t write_head;
    std::atomic_size_t unread_samples;
    size_t sample_count;
    size_t channels;
    std::vector<float> buffer;
};

#endif
