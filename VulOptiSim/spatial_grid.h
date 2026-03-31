#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cmath>

/// <summary>
/// Uniform spatial grid voor brede-fase collision detection.
///
/// Het speelveld wordt opgedeeld in vierkante cellen van cell_size x cell_size.
/// Elke hero wordt in een cel geplaatst op basis van zijn positie.
/// Bij collision checks hoeven we dan alleen heroes in dezelfde en aangrenzende
/// cellen te controleren, in plaats van alle heroes.
///
/// Tijdscomplexiteit:
///   Insert: O(1) per hero, O(n) totaal
///   Query:  O(k) waarbij k = aantal heroes in de gecheckte cellen
///   Totaal: O(n + n*k) in plaats van O(n^2) brute-force
/// </summary>
struct SpatialGrid
{
    float cell_size;
    std::unordered_map<int64_t, std::vector<size_t>> cells;

    SpatialGrid(float cell_size = 2.0f) : cell_size(cell_size) {}

    void clear()
    {
        cells.clear();
    }

    /// <summary>
    /// Voeg een index toe aan de cel die positie (x, y) bevat.
    /// </summary>
    void insert(size_t index, float x, float y)
    {
        cells[make_key(to_cell(x), to_cell(y))].push_back(index);
    }

    /// <summary>
    /// Geeft alle indices in de 3x3 buurtcellen rond positie (x, y).
    /// Gebruikt voor collision detection (kleine radius).
    /// </summary>
    void query_radius(float x, float y, std::vector<size_t>& result) const
    {
        result.clear();
        int cx = to_cell(x);
        int cy = to_cell(y);

        for (int dx = -1; dx <= 1; dx++)
        {
            for (int dy = -1; dy <= 1; dy++)
            {
                auto it = cells.find(make_key(cx + dx, cy + dy));
                if (it != cells.end())
                {
                    result.insert(result.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }

    /// <summary>
    /// Geeft alle indices in cellen die overlappen met de gegeven AABB (rechthoek).
    /// Gebruikt voor lightning en projectile explosions (groter gebied).
    /// Marge van 1 cel aan elke kant om randhero's niet te missen.
    /// </summary>
    void query_aabb(float min_x, float min_y, float max_x, float max_y,
                    std::vector<size_t>& result) const
    {
        result.clear();
        int cx_min = to_cell(min_x) - 1;
        int cy_min = to_cell(min_y) - 1;
        int cx_max = to_cell(max_x) + 1;
        int cy_max = to_cell(max_y) + 1;

        for (int cx = cx_min; cx <= cx_max; cx++)
        {
            for (int cy = cy_min; cy <= cy_max; cy++)
            {
                auto it = cells.find(make_key(cx, cy));
                if (it != cells.end())
                {
                    result.insert(result.end(), it->second.begin(), it->second.end());
                }
            }
        }
    }

private:
    int to_cell(float coord) const
    {
        return static_cast<int>(std::floor(coord / cell_size));
    }

    /// <summary>
    /// Combineert twee cel-coordinaten tot een unieke 64-bit sleutel.
    /// Bovenste 32 bits = x-cel, onderste 32 bits = y-cel.
    /// </summary>
    int64_t make_key(int cx, int cy) const
    {
        return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
    }
};
