#include "audio.hh"
#include <cstdio>

audio::audio()
{
    soloud.init(
        0,
        SoLoud::Soloud::SDL2,
        48000,
        512
    );
}

audio::~audio()
{
    soloud.deinit();
}

uint32_t audio::get_samplerate()
{
    return soloud.getBackendSamplerate();
}

uint32_t audio::get_buffer_size()
{
    return soloud.getBackendBufferSize();
}

SoLoud::Soloud& audio::get_soloud()
{
    return soloud;
}

audio_ring_buffer::audio_ring_buffer(size_t sample_count, size_t channels)
: read_head(0), write_head(0), unread_samples(0), sample_count(sample_count),
  channels(channels), buffer(sample_count*channels, 0.0f)
{
}

void audio_ring_buffer::pop(float* stream, size_t sample_count)
{
    // The pop occurs in the order expected by SoLoud, so it's not interleaved.
    size_t i = 0;
    for(; i < sample_count && unread_samples > 0; ++i)
    {
        for(size_t j = 0; j < channels; ++j)
        {
            stream[i+sample_count*j] = buffer[read_head*channels+j];
        }
        read_head = (read_head+1) % this->sample_count;
        unread_samples--;
    }

    //if(i < sample_count)
    //    printf("Audio underflow!\n");

    // Fill the rest with zeroes if we underflowed.
    for(; i < sample_count; ++i)
    {
        for(size_t j = 0; j < channels; ++j)
        {
            stream[i+sample_count*j] = 0.0f;
        }
    }
}

void audio_ring_buffer::push(int16_t left, int16_t right)
{
    if(unread_samples == sample_count)
    {
        //printf("Audio overflow!\n");
        return; // overflow! Don't push more.
    }

    buffer[write_head*channels] = left/32768.0f;
    buffer[write_head*channels+1] = right/32768.0f;
    write_head = (write_head+1) % sample_count;
    unread_samples++;
}

size_t audio_ring_buffer::get_unread_sample_count() const
{
    return unread_samples;
}
