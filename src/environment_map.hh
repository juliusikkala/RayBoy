#ifndef RAYBOY_ENVIRONMENT_MAP_HH
#define RAYBOY_ENVIRONMENT_MAP_HH
#include "transformable.hh"

class texture;
class environment_map:
    public ptr_component,
    public dependency_components<transformable>
{
public:
    environment_map(
        const texture* radiance = nullptr,
        const texture* irradiance = nullptr
    );

    void set_radiance(const texture* radiance = nullptr);
    const texture* get_radiance() const;

    void set_irradiance(const texture* irradiance = nullptr);
    const texture* get_irradiance() const;

    void set_exposure(float exposure = 1.0f);
    float get_exposure() const;

private:
    const texture* radiance;
    const texture* irradiance;
    float exposure;
};

#endif
