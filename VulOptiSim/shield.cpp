#include "pch.h"
#include "shield.h"

Shield::Shield(const std::string& texture_array_name, const std::vector<Hero>& heroes)
    : texture_name(texture_array_name)
{
    //Gather all hero positions if they have mana left
    std::vector<glm::vec3> points;
    for (const auto& hero : heroes)
    {
        if (hero.get_mana() > 0 && hero.is_active())
        {
            points.emplace_back(hero.get_position());
        }
    }

    if (points.empty())
    {
        return;
    }

    std::vector<glm::vec2> points_2d{};
    points_2d.reserve(points.size() * 4);

    float lowest_point = points[0].y;
    float highest_point = points[0].y;
    for (auto& p : points)
    {
        //convert to 2d: 3d y-axis points up, so use z
        //create square around point, this way the shield fits around the character nicely
        points_2d.emplace_back(p.x + .5f, p.z + .5f);
        points_2d.emplace_back(p.x + .5f, p.z - .5f);
        points_2d.emplace_back(p.x - .5f, p.z + .5f);
        points_2d.emplace_back(p.x - .5f, p.z - .5f);

        if (p.y < lowest_point)
        {
            lowest_point = p.y;
        }

        if (p.y > highest_point)
        {
            highest_point = p.y;
        }
    }

    min_height = lowest_point;
    max_height = highest_point;

    convex_hull_points = convex_hull(points_2d);

    //Make the shield a bit larger so it doesn't clip the heroes
    grow_from_centroid();
}

void Shield::draw(vulvox::Renderer* renderer) const
{
    if (convex_hull_points.size() <= 1)
    {
        return;
    }

    std::vector<glm::mat4> transforms;
    transforms.reserve(convex_hull_points.size());

    std::vector<uint32_t> texture_indices(convex_hull_points.size(), 0);

    std::vector<glm::vec4> uvs;
    uvs.reserve(convex_hull_points.size());

    //Place the shield planes between each consecutive two points in the convex hull
    float shield_start = 0.f;
    for (size_t i = 0; i < convex_hull_points.size(); i++)
    {
        size_t next_index = (i + 1) % convex_hull_points.size();

        //Calculate shield plane position, rotation, and size
        //https://www.geogebra.org/3d/n2w444tn
        glm::vec2 position = convex_hull_points.at(i); //Position is based on bottom left corner
        glm::vec2 distance_vec = convex_hull_points.at(next_index) - convex_hull_points.at(i);

        //Get outward vector by calculating the perpendicular vector
        glm::vec2 line_vec = glm::normalize(convex_hull_points.at(next_index) - convex_hull_points.at(i));

        glm::mat4 rotation{ 1.0f };
        rotation[0] = glm::vec4(line_vec.x, 0.f, line_vec.y, 0.f); //right
        rotation[1] = glm::vec4(0.f, 1.f, 0.f, 0.f); //up
        rotation[2] = glm::vec4(line_vec.y, 0.f, -line_vec.x, 0.f); //forward

        float shield_length = glm::length(distance_vec);

        //Move the plane in between the two points
        glm::mat4 translate = glm::translate(glm::mat4(1.f), glm::vec3(position.x, min_height + ((max_height + shield_height) - min_height) / 2, position.y) + glm::vec3(distance_vec.x, 0.f, distance_vec.y) / 2.f);
        glm::mat4 scale = glm::scale(glm::mat4(1.f), glm::vec3(shield_length, (max_height + shield_height) - min_height, 1));
        glm::mat4 model_matrix = translate * rotation * scale;

        transforms.push_back(model_matrix);

        uvs.emplace_back(glm::vec4{ shield_start / shield_texture_scalar, 0.f, (shield_start + shield_length) / shield_texture_scalar, ((max_height + shield_height) - min_height) / shield_texture_scalar });

        shield_start += shield_length;
    }

    renderer->draw_planes(texture_name, transforms, texture_indices, uvs);
}

/// <summary>
/// Berekent de convex hull van een set 2D-punten met Graham Scan.
/// 
/// Stap 1: Vind het laagste punt (ankerpunt).
/// Stap 2: Sorteer alle andere punten op hoek ten opzichte van het ankerpunt.
/// Stap 3: Loop door de gesorteerde punten en bouw de hull op.
///         Bij elke stap: als het nieuwe punt een rechtse bocht maakt (clockwise),
///         verwijder het vorige punt van de hull. Herhaal tot er een linkse bocht is.
///
/// Was: Jarvis March O(n * h), waarbij h = aantal hullpunten.
/// Nu:  Graham Scan O(n log n), onafhankelijk van het aantal hullpunten.
/// </summary>
std::vector<glm::vec2> Shield::convex_hull(std::vector<glm::vec2> all_points) const
{
    // Verwijder bijna-duplicaten (zelfde logica als voorheen)
    all_points.erase(std::ranges::unique(all_points, [](const glm::vec2& a, const glm::vec2& b)
        {
            return glm::abs(a.x - b.x) < 0.001f && glm::abs(a.y - b.y) < 0.001f;
        }).begin(), all_points.end());

    if (all_points.size() < 3) return all_points;

    // Stap 1: Vind het laagste punt (laagste y, bij gelijk de meest linkse x)
    // Dit punt ligt gegarandeerd op de convex hull
    int lowest_index = 0;
    for (int i = 1; i < static_cast<int>(all_points.size()); i++)
    {
        if (all_points[i].y < all_points[lowest_index].y ||
            (all_points[i].y == all_points[lowest_index].y && all_points[i].x < all_points[lowest_index].x))
        {
            lowest_index = i;
        }
    }

    // Zet het ankerpunt vooraan in de vector
    std::swap(all_points[0], all_points[lowest_index]);
    glm::vec2 anchor = all_points[0];

    // Stap 2: Sorteer de rest op polaire hoek t.o.v. het ankerpunt
    // atan2 geeft de hoek in radialen. Bij gelijke hoek: dichtste punt eerst.
    std::sort(all_points.begin() + 1, all_points.end(),
        [&anchor](const glm::vec2& a, const glm::vec2& b)
        {
            // Kruisproduct bepaalt welk punt een grotere hoek heeft t.o.v. anchor
            float cross = (a.x - anchor.x) * (b.y - anchor.y)
                - (a.y - anchor.y) * (b.x - anchor.x);

            if (cross != 0.0f)
                return cross > 0.0f;

            // Zelfde hoek: dichtstbijzijnde punt eerst
            return glm::length2(a - anchor) < glm::length2(b - anchor);
        });

    // Stap 3: Bouw de hull op met een stack-achtige vector
    // Voor elk nieuw punt: verwijder punten van de stack zolang ze een
    // clockwise (rechtse) bocht maken. Dat betekent dat ze BINNEN de hull liggen.
    std::vector<glm::vec2> hull;
    hull.push_back(all_points[0]);
    hull.push_back(all_points[1]);

    for (int i = 2; i < static_cast<int>(all_points.size()); i++)
    {
        while (hull.size() > 1)
        {
            glm::vec2 top = hull.back();
            glm::vec2 second = hull[hull.size() - 2];

            // Kruisproduct: positief = linkse bocht (counter-clockwise), op de hull
            //               nul of negatief = rechtse bocht of collineair, niet op de hull
            float cross = (top.x - second.x) * (all_points[i].y - second.y)
                - (top.y - second.y) * (all_points[i].x - second.x);

            if (cross <= 0.0f)
                hull.pop_back(); // Verwijder punt dat binnen de hull ligt
            else
                break; // Linkse bocht gevonden, stop met verwijderen
        }
        hull.push_back(all_points[i]);
    }

    return hull;
}

bool Shield::intersects(const glm::vec2& circle_center, float radius) const
{
    if (convex_hull_points.size() < 2)
    {
        return false;
    }

    for (size_t i = 0; i < convex_hull_points.size(); i++)
    {
        //See: https://www.geogebra.org/calculator/ccfyg8bh

        glm::vec2 A = convex_hull_points[i];
        glm::vec2 B = convex_hull_points[(i + 1) % convex_hull_points.size()];

        //Compute the closest point on segment AB to the circle's center
        glm::vec2 AB = B - A;
        float t = glm::dot(circle_center - A, AB) / glm::dot(AB, AB);
        t = glm::clamp(t, 0.0f, 1.0f); //Clamp t to [0, 1] so we stay on the segment

        glm::vec2 closest_point = A + t * AB;

        // Check if the closest point is within the circle
        if (glm::distance2(circle_center, closest_point) <= radius * radius)
        {
            return true;
        }
    }

    return false; //No intersection
}

void Shield::absorb(std::vector<Hero>& heroes, glm::vec2 point) const
{
    std::vector<Hero*> closest_heroes;
    std::vector<float> closest_distances(n_to_sustain, std::numeric_limits<float>::max());

    for (auto& hero : heroes)
    {
        float distance_squared = glm::length2(hero.get_position2d() - point);

        //If we haven't filled the closest list yet, add this hero
        if (closest_heroes.size() < n_to_sustain)
        {
            closest_heroes.push_back(&hero);
            closest_distances[closest_heroes.size() - 1] = distance_squared;
        }
        else
        {
            //Check if this hero is closer than any in the current closest list
            size_t farthest = 0;
            for (size_t i = 1; i < closest_heroes.size(); i++)
            {
                if (closest_distances[farthest] < closest_distances[i])
                {
                    farthest = i;
                }
            }

            if (distance_squared < closest_distances[farthest])
            {
                //Replace the farthest hero with the current closer hero
                closest_heroes[farthest] = &hero;
                closest_distances[farthest] = distance_squared;
            }
        }
    }

    for (auto& hero : closest_heroes)
    {
        hero->drain_mana(mana_cost);
    }
}

/// <summary>
/// Calculate the center (centroid) of a set of points.
/// </summary>
glm::vec2 Shield::calculate_centroid()
{
    glm::vec2 sum = std::accumulate(convex_hull_points.begin(), convex_hull_points.end(), glm::vec2(0.0f, 0.0f));
    return sum / static_cast<float>(convex_hull_points.size());
}

void Shield::grow_from_centroid()
{
    //Push out the convex hull away from its centroid by a unit vector
    glm::vec2 centroid = calculate_centroid();

    for (auto& convex_point : convex_hull_points)
    {
        glm::vec2 grow_vector = glm::normalize(convex_point - centroid);
        convex_point += 2.f * grow_vector;
    }
}