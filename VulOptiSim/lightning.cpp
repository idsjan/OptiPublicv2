#include "pch.h"
#include "lightning.h"
#include "spatial_grid.h"

Lightning::Lightning() = default;

Lightning::Lightning(glm::vec3 position) : animation_timer("lightning", 0, 10, 0.25f), transform(position)
{
    transform.scale = glm::vec3(plane_size.x, plane_size.y, 1.f);
    transform.position.y += plane_size.y / 4.f;

    collision_box_min = transform.get_position2d() - glm::vec2(plane_size.x / 2, plane_size.y / 2);
    collision_box_max = transform.get_position2d() + glm::vec2(plane_size.x / 2, plane_size.y / 2);
}

void Lightning::update(const float delta_time, const Camera& camera, std::vector<Hero>& heroes,
    const SpatialGrid& grid, std::mutex& hero_mutex)
{
    if (active)
    {
        uptime += delta_time;
        if (uptime >= lifetime)
        {
            active = false;
            return;
        }

        rotate_to_camera(camera);
        animation_timer.update(delta_time);

        check_hits(heroes, grid, hero_mutex);
    }
}

void Lightning::register_draw(Sprite_Manager<Lightning>& sprite_manager) const
{
    if (active)
    {
        sprite_manager.register_draw(*this);
    }
}

/// <summary>
/// Checkt welke heroes geraakt worden door de bliksem.
/// Was: lineaire scan over alle 9000 heroes per bliksem per frame.
/// Nu:  spatial grid query op het AABB van de bliksem, checkt alleen heroes in de buurt.
/// Mutex beschermt hero.take_damage() omdat meerdere bliksems parallel draaien.
/// </summary>
void Lightning::check_hits(std::vector<Hero>& heroes, const SpatialGrid& grid,
    std::mutex& hero_mutex) const
{
    if (active)
    {
        // Vraag alleen heroes op in cellen die overlappen met de collision box
        std::vector<size_t> nearby;
        grid.query_aabb(collision_box_min.x, collision_box_min.y,
            collision_box_max.x, collision_box_max.y, nearby);

        for (size_t idx : nearby)
        {
            Hero& hero = heroes[idx];
            if (hero.is_active() && hero.collision(collision_box_min, collision_box_max))
            {
                std::lock_guard<std::mutex> lock(hero_mutex);
                Log::get_instance()->add_log("%s is hit by lightning!\n", hero.get_name());
                hero.take_damage(damage_per_frame);
            }
        }
    }
}

glm::mat4 Lightning::get_model_matrix() const
{
    return transform.get_matrix();
}

glm::uint32_t Lightning::get_texture_index() const
{
    return animation_timer.get_current_frame();
}

void Lightning::rotate_to_camera(const Camera& camera)
{
    glm::vec3 facing_direction = camera.get_position() - transform.position;
    facing_direction.y = 0.f;
    facing_direction = glm::normalize(facing_direction);

    float angle = atan2f(facing_direction.x, facing_direction.z);
    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
    transform.rotation = rotation;
}