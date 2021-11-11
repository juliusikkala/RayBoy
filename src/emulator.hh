#ifndef RAYBOY_EMULATOR_HH
#define RAYBOY_EMULATOR_HH

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include "math.hh"
#include "audio.hh"
extern "C"
{
#include "gb.h"
#undef internal
}

class emulator_audio: public SoLoud::AudioSource
{
public:
    emulator_audio(uint32_t buffer_length, uint32_t samplerate);
    ~emulator_audio();

    void push_sample(GB_sample_t* sample);

    // -1: danger of underflowing soon, emulate enough samples ASAP
    // 0: all is well
    // 1: danger of overflowing soon, don't emulate more samples
    int get_sample_status() const;

    SoLoud::AudioSourceInstance* createInstance() override;

private:
    uint32_t buffer_length;
    audio_ring_buffer buf;
};

class emulator
{
public:
    emulator(audio& a);
    ~emulator();

    void reset();
    bool load_rom(const std::string& path);
    void load_sav(const std::string& path);
    void save_sav();
    void set_power(bool on);

    void set_button(GB_key_t button, bool pressed);
    void print_info();

    static uvec2 get_screen_size();

    void lock_framebuffer();
    uint32_t* get_framebuffer_data(bool faded);
    void unlock_framebuffer();

private:
    void worker_func();

    void init_gb();
    void deinit_gb();

    void age_framebuffer();

    static void push_audio_sample(GB_gameboy_t *gb, GB_sample_t* sample);
    static void handle_vblank(GB_gameboy_t *gb);

    uint64_t age_ticks;
    bool powered;
    bool destroy;
    GB_gameboy_t gb;
    audio* a;
    std::string rom, sav;
    emulator_audio audio_output;
    std::vector<uint32_t> active_framebuffer;
    std::vector<uint32_t> finished_framebuffer;
    std::vector<uint32_t> faded_framebuffer;

    // Yeah, I'm lazy like that...
    std::recursive_mutex mutex;
    std::thread worker;
};

#endif
