#include "pch.h"
#include "scene.h"
#include "pathfinding_benchmark.h"
#include "spatial_grid.h"
#include <thread>
#include <mutex>

Scene::Scene(vulvox::Renderer& renderer) : renderer(&renderer)
{
    glfwGetCursorPos(this->renderer->get_window(), &prev_mouse_pos.x, &prev_mouse_pos.y);

    glm::vec3 camera_pos{ -28.2815380f, 305.485260f, -30.0800228f };
    glm::vec3 camera_up{ 0.338442326f, 0.869414926f, 0.359964609f };
    glm::vec3 camera_direction{ 0.595541596f, -0.494082689f, 0.633413374f };

    camera = Camera(camera_pos, camera_up, camera_direction, 100.f, 100.f);

    terrain = Terrain(TERRAIN_PATH);

    load_models_and_textures();

    spawn_heroes();

    spawn_staves();

    std::cout << "Scene loaded." << std::endl;
}

void Scene::load_models_and_textures() const
{
    std::vector<std::filesystem::path> texture_paths{
        CUBE_SEA_TEXTURE_PATH,
        CUBE_GRASS_FLOWER_TEXTURE_PATH,
        CUBE_CONCRETE_WALL_TEXTURE_PATH,
        CUBE_MOSS_TEXTURE_PATH };
    renderer->load_texture_array("texture_array_test", texture_paths);

    renderer->load_model("frieren-blob", FRIEREN_PATH);
    renderer->load_texture("frieren-blob", FRIEREN_TEXTURE_PATH);

    renderer->load_model("staff", STAFF_PATH);
    renderer->load_texture("staff", STAFF_TEXTURE_PATH);

    renderer->load_model("cube", CUBE_MODEL_PATH);
    renderer->load_texture("cube", CUBE_SEA_TEXTURE_PATH);

    std::vector<std::filesystem::path> shield_path{ SHIELD_TEXTURE_PATH };
    renderer->load_texture_array("shield", shield_path);

    renderer->load_texture_array("lightning", LIGHTNING_TEXTURE_PATHS);

    renderer->load_texture_array("fireball", FIREBALL_TEXTURE_PATHS);
}

void Scene::spawn_heroes()
{
    //run_pathfinding_benchmark(terrain); //De benchmarking, uncomment 
    Transform hero_transform;
    hero_transform.rotation = glm::quatLookAt(glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 1.f, 0.f));
    hero_transform.scale = glm::vec3(1.f);

    float spawn_offset = terrain.tile_width / 3.f;

    int start_areas = 10;
    float start_area_tile_offset = 12.f;
    float spawn_start_y = terrain.tile_width * 3.f;

    float start_corner_y = 9.f * terrain.tile_width;

    std::cout << "Spawning characters and calculating routes..." << std::endl;

    int spawn_count = 0;
    for (int s = 0; s < start_areas; s++)
    {
        float start_area_offset = static_cast<float>(s) * start_area_tile_offset * terrain.tile_width;

        for (int i = 0; i < 30; i++)
        {
            for (int j = 0; j < 30; j++)
            {
                float x = start_corner_y + start_area_offset + ((float)i * spawn_offset);
                float z = spawn_start_y + ((float)j * spawn_offset);
                float y = terrain.get_height(glm::vec2(x, z));

                hero_transform.position = glm::vec3(x, y, z);

                spawn_count++;

                heroes.emplace_back("frieren-blob", "frieren-blob", hero_transform, "Frieren" + std::to_string(spawn_count), 20.f);

                auto r = terrain.find_route(glm::uvec2(x, z), glm::uvec2(69 * terrain.tile_width, 160 * terrain.tile_width));
                heroes.back().set_route(r);
            }
        }
    }

    Log::get_instance()->add_log("Spawned %d characters.\n", spawn_count);
}

void Scene::spawn_staves()
{
    glm::vec2 spawn_start{ terrain.tile_width * 15.f,  terrain.tile_length * 48.f };
    float height = terrain.get_height(spawn_start) + 50.f;

    float spawn_offset_x = 12.f * terrain.tile_height;
    float spawn_offset_y = 40.f * terrain.tile_length;

    int spawn_count = 0;
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            spawn_count++;
            glm::vec3 position{ spawn_start.x + i * spawn_offset_x, height, spawn_start.y + j * spawn_offset_y };
            staves.emplace_back("Staff" + std::to_string(spawn_count), position, &terrain);
        }
    }

    Log::get_instance()->add_log("Spawned %d staves.\n", spawn_count);
}

size_t Scene::get_character_count() const
{
    return heroes.size();
}

size_t Scene::get_staff_count() const
{
    return staves.size();
}

void Scene::update(const float delta_time)
{
    handle_input(delta_time);

    if (follow_mode)
    {
        auto it = std::ranges::max_element(heroes,
            [](const Hero& a, const Hero& b) {
                return a.get_position().z < b.get_position().z;
            }
        );

        glm::vec3 new_camera_position{ -28.0f, 305.5f, it->get_position().z };
        camera.set_position(new_camera_position);

        glm::vec3 camera_to_furthest = it->get_position() - camera.get_position();
        camera.set_direction(camera_to_furthest);
    }

    renderer->set_view_matrix(camera.get_view_matrix());

    // Bepaal het aantal threads op basis van beschikbare CPU-cores
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    size_t hero_count = heroes.size();
    size_t chunk_size = hero_count / num_threads;

    // Gedeelde mutex voor alle hero-modificaties (push, take_damage, drain_mana)
    std::mutex hero_mutex;

    // ===== SPATIAL GRID OPBOUWEN =====
    // Elk hero-positie wordt in een cel geplaatst. Cellen zijn 2x2 eenheden groot.
    // Bij collision hoeven we daarna alleen heroes in dezelfde + buurcellen te checken.
    SpatialGrid grid(2.0f);
    for (size_t i = 0; i < hero_count; i++)
    {
        if (heroes[i].is_active())
        {
            glm::vec2 pos = heroes[i].get_position2d();
            grid.insert(i, pos.x, pos.y);
        }
    }

    // ===== COLLISION DETECTION (spatial grid + threaded) =====
    // Was: O(n^2) brute-force, 9000x9000 = 81 miljoen paren per frame
    // Nu:  O(n * k) met grid, k = ~10-20 heroes per buurtcel-groep
    {
        std::vector<std::thread> threads;

        for (unsigned int t = 0; t < num_threads; t++)
        {
            size_t start = t * chunk_size;
            size_t end = (t == num_threads - 1) ? hero_count : (t + 1) * chunk_size;

            threads.emplace_back([this, start, end, &hero_mutex, &grid]()
                {
                    std::vector<size_t> nearby; // Hergebruikt per iteratie (voorkomt 9000 allocaties)
                    for (size_t i = start; i < end; i++)
                    {
                        if (!heroes[i].is_active()) continue;

                        glm::vec2 pos_i = heroes[i].get_position2d();
                        float radius_i = heroes[i].get_collision_radius();

                        // Haal alleen heroes op in dezelfde en aangrenzende cellen
                        grid.query_radius(pos_i.x, pos_i.y, nearby);

                        for (size_t j : nearby)
                        {
                            if (i == j || !heroes[j].is_active()) continue;

                            if (circle_collision(pos_i, radius_i,
                                heroes[j].get_position2d(), heroes[j].get_collision_radius()))
                            {
                                glm::vec2 direction = heroes[j].get_position2d() - pos_i;

                                std::lock_guard<std::mutex> lock(hero_mutex);
                                heroes[j].push(glm::normalize(direction),
                                    radius_i - (glm::length(direction) / 2));
                            }
                        }
                    }
                });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }
    }

    // ===== HERO UPDATES (threaded, geen mutex nodig) =====
    {
        std::vector<std::thread> threads;

        for (unsigned int t = 0; t < num_threads; t++)
        {
            size_t start = t * chunk_size;
            size_t end = (t == num_threads - 1) ? hero_count : (t + 1) * chunk_size;

            threads.emplace_back([this, start, end, delta_time]()
                {
                    for (size_t i = start; i < end; i++)
                    {
                        heroes[i].update(delta_time, terrain);
                    }
                });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }
    }

    // ===== GRID HERBOUWEN na hero updates =====
    // Posities zijn veranderd, dus de grid moet opnieuw opgebouwd worden
    // voor lightning en projectile collision checks.
    grid.clear();
    for (size_t i = 0; i < hero_count; i++)
    {
        if (heroes[i].is_active())
        {
            glm::vec2 pos = heroes[i].get_position2d();
            grid.insert(i, pos.x, pos.y);
        }
    }

    shield = Shield{ "shield", heroes };

    for (auto& staff : staves)
    {
        staff.update(delta_time, heroes, active_lightning, projectiles);
    }

    // ===== LIGHTNING UPDATES (threaded + grid + mutex) =====
    {
        size_t lightning_count = active_lightning.size();

        if (lightning_count > 0)
        {
            std::vector<std::thread> threads;
            unsigned int actual_threads = std::min(num_threads, static_cast<unsigned int>(lightning_count));
            size_t lightning_chunk = lightning_count / actual_threads;

            for (unsigned int t = 0; t < actual_threads; t++)
            {
                size_t start = t * lightning_chunk;
                size_t end = (t == actual_threads - 1) ? lightning_count : (t + 1) * lightning_chunk;

                threads.emplace_back([this, start, end, delta_time, &grid, &hero_mutex]()
                    {
                        for (size_t i = start; i < end; i++)
                        {
                            active_lightning[i].update(delta_time, camera, heroes, grid, hero_mutex);
                        }
                    });
            }

            for (auto& thread : threads)
            {
                thread.join();
            }
        }
    }

    //Remove inactive lightning
    const auto [first_l, last_l] = std::ranges::remove_if(active_lightning, [](const Lightning& l) { return !l.is_active(); });
    active_lightning.erase(first_l, last_l);

    // ===== PROJECTILE UPDATES (threaded + grid + mutex) =====
    {
        size_t projectile_count = projectiles.size();

        if (projectile_count > 0)
        {
            std::vector<std::thread> threads;
            unsigned int actual_threads = std::min(num_threads, static_cast<unsigned int>(projectile_count));
            size_t proj_chunk = projectile_count / actual_threads;

            for (unsigned int t = 0; t < actual_threads; t++)
            {
                size_t start = t * proj_chunk;
                size_t end = (t == actual_threads - 1) ? projectile_count : (t + 1) * proj_chunk;

                threads.emplace_back([this, start, end, delta_time, &hero_mutex, &grid]()
                    {
                        for (size_t i = start; i < end; i++)
                        {
                            projectiles[i].update(delta_time, camera, shield, heroes, hero_mutex, grid);
                        }
                    });
            }

            for (auto& thread : threads)
            {
                thread.join();
            }
        }
    }

    //Remove inactive projectiles
    const auto [first_p, last_p] = std::ranges::remove_if(projectiles, [](const Projectile& p) { return !p.is_active(); });
    projectiles.erase(first_p, last_p);
}

void Scene::draw()
{
    for (const auto& hero : heroes)
    {
        hero.draw(renderer);
    }

    terrain.draw(renderer);

    for (const auto& staff : staves)
    {
        staff.draw(renderer);
    }

    for (const auto& lightning : active_lightning)
    {
        lightning.register_draw(lightning_sprite_manager);
    }

    lightning_sprite_manager.draw(renderer);
    lightning_sprite_manager.reset();

    for (const auto& projectile : projectiles)
    {
        projectile.register_draw(projectile_sprite_manager);
    }

    projectile_sprite_manager.draw(renderer);
    projectile_sprite_manager.reset();

    shield.draw(renderer);

    show_health_values();
    show_mana_values();

    Log::get_instance()->draw("Log");

    show_controls();
}

void Scene::show_health_values() const
{
    std::vector<int> health_values;
    for (const auto& h : heroes)
    {
        health_values.push_back(h.get_health());
    }

    // Counting sort: O(n + k) i.p.v. merge sort O(n log n)
    // Mogelijk omdat health altijd tussen 0 en 1000 ligt
    health_values = counting_sort(health_values, 1000);

    ImGui::Begin("Heroes Health Bars");

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.5f, 0.f, 1.0f });
    for (const int& hp : health_values)
    {
        std::stringstream hp_text;
        hp_text << hp << "/" << 1000;
        ImGui::ProgressBar((float)hp / 1000, ImVec2(-FLT_MIN, 0.0f), hp_text.str().c_str());
    }
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    ImGui::End();
}

void Scene::show_mana_values() const
{
    std::vector<int> mana_values;
    for (const auto& s : heroes)
    {
        mana_values.push_back(s.get_mana());
    }

    // Counting sort: O(n + k) i.p.v. merge sort O(n log n)
    mana_values = counting_sort(mana_values, 1000);

    ImGui::Begin("Heroes Mana Bars");

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.f, 0.5f, 1.0f });
    for (const int& mana : mana_values)
    {
        std::stringstream mana_text;
        mana_text << mana << "/" << 1000;
        ImGui::ProgressBar((float)mana / 1000, ImVec2(-FLT_MIN, 0.0f), mana_text.str().c_str());
    }
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    ImGui::End();
}

/// <summary>
/// Counting sort: sorteert integers van laag naar hoog.
///
/// Werkt alleen als de waarden een bekende, beperkte range hebben (hier 0-1000).
/// Stap 1: Tel hoe vaak elke waarde voorkomt in een count-array.
/// Stap 2: Loop door de count-array en bouw de gesorteerde output op.
///
/// Tijdscomplexiteit: O(n + k), waarbij n = aantal elementen, k = max_value.
/// Ruimtecomplexiteit: O(n + k) voor count-array en output.
/// Vergelijking: merge sort is O(n log n). Bij n=9000, k=1000:
///   Counting sort: ~10.000 operaties
///   Merge sort:    ~117.000 operaties
/// </summary>
std::vector<int> Scene::counting_sort(const std::vector<int>& to_sort, int max_value) const
{
    // Stap 1: count-array aanmaken, index = waarde, element = aantal keer dat die waarde voorkomt
    std::vector<int> count(max_value + 1, 0);

    for (int value : to_sort)
    {
        if (value >= 0 && value <= max_value)
        {
            count[value]++;
        }
    }

    // Stap 2: gesorteerde output opbouwen door door de count-array te lopen
    std::vector<int> sorted;
    sorted.reserve(to_sort.size());

    for (int i = 0; i <= max_value; i++)
    {
        for (int j = 0; j < count[i]; j++)
        {
            sorted.push_back(i);
        }
    }

    return sorted;
}

void Scene::merge(std::vector<int>& vec, int left, int mid, int right) const
{
    std::vector<int> left_half(vec.begin() + left, vec.begin() + mid + 1);
    std::vector<int> right_half(vec.begin() + mid + 1, vec.begin() + right + 1);

    int i = 0;
    int j = 0;
    int k = left;

    while (i < static_cast<int>(left_half.size()) && j < static_cast<int>(right_half.size()))
    {
        if (left_half[i] <= right_half[j])
        {
            vec[k] = left_half[i];
            i++;
        }
        else
        {
            vec[k] = right_half[j];
            j++;
        }
        k++;
    }

    while (i < static_cast<int>(left_half.size()))
    {
        vec[k] = left_half[i];
        i++;
        k++;
    }

    while (j < static_cast<int>(right_half.size()))
    {
        vec[k] = right_half[j];
        j++;
        k++;
    }
}

void Scene::merge_sort(std::vector<int>& vec, int left, int right) const
{
    if (left >= right) return;

    int mid = left + (right - left) / 2;

    merge_sort(vec, left, mid);
    merge_sort(vec, mid + 1, right);
    merge(vec, left, mid, right);
}

std::vector<int> Scene::sort(const std::vector<int>& to_sort) const
{
    std::vector<int> sorted_list = to_sort;

    if (sorted_list.size() <= 1) return sorted_list;

    merge_sort(sorted_list, 0, static_cast<int>(sorted_list.size()) - 1);

    return sorted_list;
}

void Scene::handle_input(const float delta_time)
{
    if (glfwGetKey(renderer->get_window(), GLFW_KEY_TAB) == GLFW_PRESS) { follow_mode = !follow_mode; }

    if (!follow_mode)
    {
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_W) == GLFW_PRESS) { camera.move_forward(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_S) == GLFW_PRESS) { camera.move_backward(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_Q) == GLFW_PRESS) { camera.move_left(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_E) == GLFW_PRESS) { camera.move_right(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_A) == GLFW_PRESS) { camera.rotate_left(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_D) == GLFW_PRESS) { camera.rotate_right(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_SPACE) == GLFW_PRESS) { camera.move_up(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_Z) == GLFW_PRESS) { camera.move_down(delta_time); }

        glm::dvec2 mouse_pos;
        glfwGetCursorPos(renderer->get_window(), &mouse_pos.x, &mouse_pos.y);

        glm::dvec2 mouse_offset = mouse_pos - prev_mouse_pos;
        prev_mouse_pos = mouse_pos;

        mouse_offset.x *= delta_time;
        mouse_offset.y *= delta_time;

        if (glfwGetKey(renderer->get_window(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        {
            camera.update_direction(mouse_offset);
        }
    }
}

void Scene::show_controls()
{
    ImGui::Begin("Camera Controls Guide");

    ImGui::Text("Follow Mode: %s", follow_mode ? "Enabled" : "Disabled");
    ImGui::Separator();

    ImGui::Text("Movement Controls (When Follow Mode is Disabled):");
    ImGui::BulletText("[W] - Move Forward");
    ImGui::BulletText("[S] - Move Backward");
    ImGui::BulletText("[Q] - Move Left");
    ImGui::BulletText("[E] - Move Right");
    ImGui::BulletText("[A] - Rotate Left");
    ImGui::BulletText("[D] - Rotate Right");
    ImGui::BulletText("[SPACE] - Move Up");
    ImGui::BulletText("[Z] - Move Down");

    ImGui::Separator();

    ImGui::Text("Mouse Controls:");
    ImGui::BulletText("[Mouse + SHIFT] - Look Around");

    ImGui::Separator();

    ImGui::Text("Current Mouse Position:");
    ImGui::Text("X: %.2f, Y: %.2f", prev_mouse_pos.x, prev_mouse_pos.y);

    ImGui::Text("Camera Position:");
    glm::vec3 camera_pos = camera.get_position();
    ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", camera_pos.x, camera_pos.y, camera_pos.z);

    ImGui::Text("Camera Direction:");
    glm::vec3 camera_dir = camera.get_direction();
    ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", camera_dir.x, camera_dir.y, camera_dir.z);

    ImGui::Separator();

    ImGui::Text("Toggle Follow Mode: [TAB]");

    ImGui::End();
}