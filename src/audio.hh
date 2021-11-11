#ifndef RAYBOY_AUDIO_HH
#define RAYBOY_AUDIO_HH
#include "soloud.h"
#include <stdint.h>
#include <vector>
#include <atomic>
#include <unordered_map>

class transformable;
class audio
{
public:
    audio();
    ~audio();

    uint32_t get_samplerate();
    uint32_t get_buffer_size();
    void update();
    void set_listener(transformable* listener = nullptr);
    SoLoud::handle add_source(
        SoLoud::AudioSource& source,
        transformable* transformable = nullptr,
        float volume = 1.0f
    );
    void remove_source(SoLoud::handle h);

private:
    transformable* listener;
    std::unordered_map<SoLoud::handle, transformable*> sources;
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
