#include "environment_map.hh"

environment_map::environment_map(
    const texture* radiance,
    const texture* irradiance
): radiance(radiance), irradiance(irradiance), exposure(1.0f)
{
}

void environment_map::set_radiance(const texture* radiance)
{
    this->radiance = radiance;
}

const texture* environment_map::get_radiance() const
{
    return radiance;
}

void environment_map::set_irradiance(const texture* irradiance)
{
    this->irradiance = irradiance;
}

const texture* environment_map::get_irradiance() const
{
    return irradiance;
}

void environment_map::set_exposure(float exposure)
{
    this->exposure = exposure;
}

float environment_map::get_exposure() const
{
    return exposure;
}
