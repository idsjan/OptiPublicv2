#include "pch.h"
#include "projectile.h"
#include "spatial_grid.h"

Projectile::Projectile()
{
}

Projectile::Projectile(glm::vec3 spawn_position, Hero* target) : target(target), transform(spawn_position), animation_timer("fireball", 0, 33, 0.1f)
{
    transform.scale = glm::vec3(10.f);
    direction = glm::normalize(target->get_position() - spawn_position);
}

void Projectile::update(const float delta_time, const Camera& camera, const Shield& shield,
    std::vector<Hero>& heroes, std::mutex& hero_mutex, const SpatialGrid& grid)
{
    if (active)
    {
        uptime += delta_time;
        if (uptime >= lifetime)
        {
            active = false;
            return;
        }

        if (target)
        {
            direction = glm::normalize(target->get_position() - transform.position);
        }

        transform.position += direction * speed * delta_time;

        rotate_to_camera(camera);
        animation_timer.update(delta_time);

        // Shield collision check (shield data is read-only op dit punt)
        if (shield.intersects(transform.get_position2d(), radius))
        {
            // Lock voor hero-modificaties binnen absorb (drain_mana)
            std::lock_guard<std::mutex> lock(hero_mutex);
            Log::get_instance()->add_log("The projectile hits the shield, draining mana.\n");
            shield.absorb(heroes, transform.get_position2d());
            active = false;
        }

        check_collisions(heroes, hero_mutex, grid);
    }
}

/// <summary>
/// Checkt of het projectiel een hero raakt.
/// Was: lineaire scan over alle 9000 heroes.
/// Nu:  spatial grid query rond de positie van het projectiel.
/// </summary>
void Projectile::check_collisions(std::vector<Hero>& heroes, std::mutex& hero_mutex,
    const SpatialGrid& grid)
{
    glm::vec2 pos2d = transform.get_position2d();
    std::vector<size_t> nearby;
    grid.query_radius(pos2d.x, pos2d.y, nearby);

    for (size_t idx : nearby)
    {
        if (heroes[idx].collision(transform.position, radius))
        {
            {
                std::lock_guard<std::mutex> lock(hero_mutex);
                Log::get_instance()->add_log("The projectile explodes near %s.\n", heroes[idx].get_name());
            }
            // Lock is hier vrijgegeven, explode pakt zijn eigen lock
            explode(heroes, hero_mutex, grid);
            break;
        }
    }
}

/// <summary>
/// Brengt schade toe aan alle heroes binnen de explosie-radius.
/// Gebruikt de spatial grid om alleen heroes in de buurt te checken.
/// </summary>
void Projectile::explode(std::vector<Hero>& heroes, std::mutex& hero_mutex,
    const SpatialGrid& grid)
{
    glm::vec2 pos2d = transform.get_position2d();
    std::vector<size_t> nearby;
    grid.query_aabb(pos2d.x - explosion_radius, pos2d.y - explosion_radius,
        pos2d.x + explosion_radius, pos2d.y + explosion_radius, nearby);

    std::lock_guard<std::mutex> lock(hero_mutex);
    for (size_t idx : nearby)
    {
        if (heroes[idx].collision(transform.position, explosion_radius))
        {
            heroes[idx].take_damage(damage);
        }
    }
    active = false;
}

void Projectile::register_draw(Sprite_Manager<Projectile>& sprite_manager) const
{
    if (active)
    {
        sprite_manager.register_draw(*this);
    }
}

glm::mat4 Projectile::get_model_matrix() const
{
    return transform.get_matrix();
}

glm::uint32_t Projectile::get_texture_index() const
{
    return animation_timer.get_current_frame();
}

void Projectile::rotate_to_camera(const Camera& camera)
{
    glm::vec3 projectile_direction = glm::normalize(direction);
    glm::vec3 rot_axis = glm::normalize(glm::cross(glm::vec3(0, 1, 0), projectile_direction));
    float angle = acosf(glm::dot(glm::vec3(0, 1, 0), projectile_direction));
    glm::mat4 rotate_to_target = glm::rotate(glm::mat4(1.0f), angle, rot_axis);

    glm::vec3 normal_vec = rotate_to_target * glm::vec4(0.f, 0.f, 1.f, 0.f);
    glm::vec3 camera_direction = glm::normalize(camera.get_position() - transform.position);
    float camera_angle = acosf(glm::dot(normal_vec, camera_direction));
    glm::mat4 rotate_to_camera = glm::rotate(glm::mat4(1.0f), camera_angle, projectile_direction);

    transform.rotation = rotate_to_camera * rotate_to_target;
}