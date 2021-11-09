#include "emulator.hh"
#include "io.hh"
#define TICKS_PER_SECOND 0x800000

namespace
{

uint32_t rgb_encode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b)
{
    return uint32_t(r)|(uint32_t(g)<<8)|(uint32_t(b)<<16)|0xFF000000;
}

void log_callback(GB_gameboy_t*, const char*, GB_log_attributes) {}

}

emulator::emulator()
: powered(false)
{
    active_framebuffer.resize(160*144, 0xFFFFFFFF);
    finished_framebuffer.resize(160*144, 0xFFFFFFFF);
    faded_framebuffer.resize(160*144, 0xFFFFFFFF);
}

emulator::~emulator()
{
    set_power(false);
}

void emulator::reset()
{
    if(powered)
    {
        GB_reset(&gb);
    }
    memset(active_framebuffer.data(), 0xFF, sizeof(uint32_t)*active_framebuffer.size());
    memset(finished_framebuffer.data(), 0xFF, sizeof(uint32_t)*finished_framebuffer.size());
    // Faded framebuffer is intentionally not cleared here!
    surplus_time = 0;
    surplus_ticks = 0;
    age_ticks = 0;
}

bool emulator::load_rom(const std::string& path)
{
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
    reset();
    GB_load_battery(&gb, path.c_str());
    sav = path;
}

void emulator::set_power(bool on)
{
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

void emulator::run(uint64_t us)
{
    uint64_t time = surplus_time + us*TICKS_PER_SECOND;
    uint64_t ticks_to_simulate = time/1000000;
    surplus_time = time%1000000;

    age_ticks = 0;

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
            while(ticks_to_simulate > 0)
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
            }
        }
        else
        {
            age_ticks += ticks_to_simulate;
        }
    }
    age_framebuffer();
}

void emulator::set_button(GB_key_t button, bool pressed)
{
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

uint32_t* emulator::get_framebuffer_data(bool faded)
{
    if(faded) return faded_framebuffer.data();
    else return finished_framebuffer.data();
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

    GB_set_sample_rate(&gb, 48000); //TODO: audio
    GB_set_interference_volume(&gb, 1.0f);
    GB_set_highpass_filter_mode(&gb, GB_HIGHPASS_ACCURATE);

    GB_set_rewind_length(&gb, 0);
    GB_set_rtc_mode(&gb, GB_RTC_MODE_SYNC_TO_HOST);
    GB_apu_set_sample_callback(&gb, push_audio_sample);

    surplus_time = 0;
    surplus_ticks = 0;
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
    // TODO
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
