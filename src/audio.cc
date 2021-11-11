#include "audio.hh"
#include "transformable.hh"
#include <cstdio>

audio::audio()
: listener(nullptr)
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

void audio::update()
{
    vec3 listener_pos = vec3(0,0,0);
    if(listener)
    {
        listener_pos = listener->get_global_position();
        vec3 at = listener->get_global_direction();
        vec3 up = listener->get_global_direction(vec3(0,1,0));
        listener_pos -= at*0.1f;// Move ears 10cm back from "eyes"
        soloud.set3dListenerParameters(
            listener_pos.x, listener_pos.y, listener_pos.z,
            at.x, at.y, at.z, 
            up.x, up.y, up.z,
            0.0f, 0.0f, 0.0f
        );
    }
    else
    {
        soloud.set3dListenerParameters(
            0, 0, 0,
            0, 0, -1,
            0, 1, 0,
            0, 0, 0
        );
    }

    for(const auto& pair: sources)
    {
        if(pair.second != nullptr)
        {
            vec3 pos = pair.second->get_global_position();
            soloud.set3dSourcePosition(
                pair.first, pos.x, pos.y, pos.z
            );
        }
    }
    soloud.update3dAudio();
}

void audio::set_listener(transformable* listener)
{
    this->listener = listener;
}

SoLoud::handle audio::add_source(
    SoLoud::AudioSource& source,
    transformable* transformable,
    float volume
){
    SoLoud::handle h;
    if(!transformable)
    {
        h = soloud.playBackground(source);
    }
    else
    {
        vec3 pos = transformable->get_global_position();
        h = soloud.play3d(source, pos.x, pos.y, pos.z);
    }
    soloud.setInaudibleBehavior(h, true, false);
    soloud.setVolume(h, volume);
    sources[h] = transformable;
    return h;
}

void audio::remove_source(SoLoud::handle h)
{
    if(sources.count(h))
    {
        soloud.stop(h);
        sources.erase(h);
    }
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
