#include "emulator.hh"
#include "io.hh"
#include <algorithm>
#include <iostream>
#define TICKS_PER_SECOND 0x800000

namespace
{

class emulator_audio_instance: public SoLoud::AudioSourceInstance
{
public:
    emulator_audio_instance(audio_ring_buffer* buf)
    : buf(buf)
    {
    }

    unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize) override
    {
        buf->pop(aBuffer, aSamplesToRead);
        return aSamplesToRead;
    }

    bool hasEnded() override
    {
        // The fun never ends!
        return false;
    }

private:
    audio_ring_buffer* buf;
};

uint32_t rgb_encode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b)
{
    return uint32_t(r)|(uint32_t(g)<<8)|(uint32_t(b)<<16)|0xFF000000;
}

void log_callback(GB_gameboy_t*, const char*, GB_log_attributes) {}

}

emulator_audio::emulator_audio(uint32_t buffer_length, uint32_t samplerate)
: buf(buffer_length*4, 2), buffer_length(buffer_length)
{
    mBaseSamplerate = samplerate;
    mChannels = 2;
}

emulator_audio::~emulator_audio()
{
    stop();
}

void emulator_audio::push_sample(GB_sample_t* sample)
{
    buf.push(sample->left, sample->right);
}

int emulator_audio::get_sample_status() const
{
    // If we don't have enough samples for the next getAudio call, we're
    // kinda in trouble.
    size_t samples = buf.get_unread_sample_count();
    if(samples < buffer_length*2) return -1;
    if(samples > buffer_length*3) return 1;
    if(samples >= buffer_length*4-1) return 2;
    return 0;
}

SoLoud::AudioSourceInstance* emulator_audio::createInstance()
{
    return new emulator_audio_instance(&buf);
}

emulator::emulator(audio& a)
:   powered(false), destroy(false), a(&a),
    audio_output(SAMPLE_GRANULARITY, 48000),
    worker(&emulator::worker_func, this)
{
    a.get_soloud().playBackground(audio_output);
    active_framebuffer.resize(160*144, 0xFFFFFFFF);
    finished_framebuffer.resize(160*144, 0xFFFFFFFF);
    faded_framebuffer.resize(160*144, 0xFFFFFFFF);
}

emulator::~emulator()
{
    {
        std::unique_lock lock(mutex);
        destroy = true;
    }
    worker.join();
    set_power(false);
}

void emulator::reset()
{
    std::unique_lock lock(mutex);
    if(powered)
    {
        GB_reset(&gb);
    }
    memset(active_framebuffer.data(), 0xFF, sizeof(uint32_t)*active_framebuffer.size());
    memset(finished_framebuffer.data(), 0xFF, sizeof(uint32_t)*finished_framebuffer.size());
    // Faded framebuffer is intentionally not cleared here!
    age_ticks = 0;
}

bool emulator::load_rom(const std::string& path)
{
    std::unique_lock lock(mutex);
    reset();
    if(GB_load_rom(&gb, path.c_str()) == 0)
    {
        rom = path;
        return true;
    }
    else return false;
}

void emulator::load_sav(const std::string& path)
{
    std::unique_lock lock(mutex);
    reset();
    GB_load_battery(&gb, path.c_str());
    sav = path;
}

void emulator::set_power(bool on)
{
    std::unique_lock lock(mutex);
    if(powered == on)
        return;
    powered = on;
    if(on)
    {
        init_gb();
        if(rom.size()) load_rom(rom);
        if(sav.size()) load_sav(sav);
    }
    else deinit_gb();
}

void emulator::set_button(GB_key_t button, bool pressed)
{
    std::unique_lock lock(mutex);
    if(powered) GB_set_key_state(&gb, button, pressed);
}

void emulator::print_info()
{
    if(powered)
    {
        char title[17] = {};
        GB_get_rom_title(&gb, title);
        printf("%s\n", title);
    }
}

uvec2 emulator::get_screen_size()
{
    return uvec2(160, 144);
}

void emulator::lock_framebuffer()
{
    mutex.lock();
}

uint32_t* emulator::get_framebuffer_data(bool faded)
{
    if(faded)
    {
        age_framebuffer();
        return faded_framebuffer.data();
    }
    else return finished_framebuffer.data();
}

void emulator::unlock_framebuffer()
{
    mutex.unlock();
}

void emulator::worker_func()
{
    auto start = std::chrono::high_resolution_clock::now();
    auto delta = start-start;
    uint64_t surplus_time = 0;
    uint64_t surplus_ticks = 0;
    uint64_t delta_us = 0;
    auto target_delta = std::chrono::microseconds(1000);
    while(true)
    {
        {
            std::unique_lock lock(mutex);
            if(destroy)
                break;

            auto end = std::chrono::high_resolution_clock::now();
            delta = end - start;
            delta_us = std::chrono::round<std::chrono::microseconds>(delta).count();
            start = end;

            uint64_t time = surplus_time + delta_us*TICKS_PER_SECOND;

            uint64_t ticks_to_simulate = time/1000000;
            surplus_time = time%1000000;

            if(surplus_ticks >= ticks_to_simulate)
            {
                surplus_ticks -= ticks_to_simulate;
            }
            else
            {
                ticks_to_simulate -= surplus_ticks;
                surplus_ticks = 0;

                if(this->powered && rom.size())
                {
                    int status = audio_output.get_sample_status();

                    int clock_check_counter = 0;
                    while(status <= 0 && (ticks_to_simulate > 0 || status < 0))
                    {
                        uint8_t ticks = GB_run(&gb);
                        age_ticks += ticks;
                        if(ticks < ticks_to_simulate)
                        {
                            ticks_to_simulate -= ticks;
                        }
                        if(ticks >= ticks_to_simulate)
                        {
                            surplus_ticks = ticks - ticks_to_simulate;
                            ticks_to_simulate = 0;
                        }
                        status = audio_output.get_sample_status();
                        if(++clock_check_counter > 64)
                        {
                            if(std::chrono::high_resolution_clock::now() - start > target_delta)
                                break;
                            clock_check_counter = 0;
                        }
                    }
                }
                else
                {
                    age_ticks += ticks_to_simulate;
                }
            }
        }

        auto local_end = std::chrono::high_resolution_clock::now();
        auto local_delta = local_end - start;
        if(local_delta < target_delta)
        {
            // Aim to spend a millisecond each loop.
            std::this_thread::sleep_for(target_delta-local_delta);
        }
    }
}

void emulator::init_gb()
{
    GB_init(&gb, GB_MODEL_CGB_E);
    GB_set_user_data(&gb, this);

    GB_load_boot_rom(&gb, get_readonly_path("data/cgb_boot.bin").c_str());
    GB_set_vblank_callback(&gb, handle_vblank);
    GB_set_pixels_output(&gb, active_framebuffer.data());
    GB_set_rgb_encode_callback(&gb, rgb_encode);
    GB_set_rumble_mode(&gb, GB_RUMBLE_DISABLED);
    GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_DISABLED);
    GB_set_light_temperature(&gb, 0.0f);
    GB_set_palette(&gb, &GB_PALETTE_GREY);
    GB_set_log_callback(&gb, log_callback);

    GB_set_sample_rate(&gb, 48000);
    GB_set_interference_volume(&gb, 1.0f);
    GB_set_highpass_filter_mode(&gb, GB_HIGHPASS_ACCURATE);

    GB_set_rtc_mode(&gb, GB_RTC_MODE_SYNC_TO_HOST);
    GB_apu_set_sample_callback(&gb, push_audio_sample);

    powered = true;
}

void emulator::deinit_gb()
{
    GB_free(&gb);
    powered = false;
}

void emulator::age_framebuffer()
{
    age_ticks = 0;
}

void emulator::push_audio_sample(GB_gameboy_t *gb, GB_sample_t* sample)
{
    emulator& self = *(emulator*)GB_get_user_data(gb);
    self.audio_output.push_sample(sample);
}

void emulator::handle_vblank(GB_gameboy_t *gb)
{
    emulator& self = *(emulator*)GB_get_user_data(gb);

    self.age_framebuffer();
    memcpy(
        self.finished_framebuffer.data(),
        self.active_framebuffer.data(),
        self.active_framebuffer.size()*sizeof(uint32_t)
    );
}
