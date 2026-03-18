// ============================================================================
// pathfinding_benchmark.h
// 
// Add this file to your project (next to terrain.h, scene.h, etc.)
// Then call run_pathfinding_benchmark(terrain) from scene.cpp
// See instructions at the bottom of this file.
// ============================================================================
#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>

/// <summary>
/// Runs the pathfinding benchmark. Tests both BFS and A* on the same set of routes.
/// Prints results to the console AND writes them to a CSV file for easy use in your report.
/// </summary>
inline void run_pathfinding_benchmark(const Terrain& terrain)
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "  PATHFINDING BENCHMARK START" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // These are the test routes. They use the same target as the heroes in the game.
    // The target is tile (69, 160) scaled by tile_width.
    glm::vec2 target{ 69.f * terrain.tile_width, 160.f * terrain.tile_width };

    // Different start positions spread across the map to get varied results.
    // These are based on the spawn areas in scene.cpp.
    std::vector<glm::vec2> start_positions = {
        { 9.f * terrain.tile_width,  3.f * terrain.tile_width },   // Route 1: top-left area
        { 15.f * terrain.tile_width, 10.f * terrain.tile_width },  // Route 2
        { 25.f * terrain.tile_width, 5.f * terrain.tile_width },   // Route 3
        { 35.f * terrain.tile_width, 8.f * terrain.tile_width },   // Route 4
        { 50.f * terrain.tile_width, 3.f * terrain.tile_width },   // Route 5
        { 60.f * terrain.tile_width, 12.f * terrain.tile_width },  // Route 6
        { 80.f * terrain.tile_width, 6.f * terrain.tile_width },   // Route 7
        { 100.f * terrain.tile_width, 10.f * terrain.tile_width }, // Route 8
        { 120.f * terrain.tile_width, 5.f * terrain.tile_width },  // Route 9
        { 140.f * terrain.tile_width, 15.f * terrain.tile_width }, // Route 10
    };

    // How many times to repeat each measurement (for reliable averages)
    const int repeats = 5;

    // Open a CSV file to write results to
    std::ofstream csv_file("benchmark_results.csv");
    csv_file << "Route,Algorithm,Run,Time_us,Nodes_Visited,Path_Length" << std::endl;

    // Print table header to console
    std::cout << std::left
              << std::setw(8)  << "Route"
              << std::setw(10) << "Algo"
              << std::setw(12) << "Tijd (us)"
              << std::setw(18) << "Bezochte nodes"
              << std::setw(14) << "Padlengte"
              << std::endl;
    std::cout << std::string(62, '-') << std::endl;

    for (int route_index = 0; route_index < start_positions.size(); route_index++)
    {
        glm::vec2 start = start_positions[route_index];

        // Check if the start position is within the map bounds
        if (!terrain.in_bounds(start))
        {
            std::cout << "Route " << (route_index + 1) << ": start position out of bounds, skipping." << std::endl;
            continue;
        }

        // --- BFS measurements ---
        double bfs_total_time = 0.0;
        int bfs_nodes = 0;
        int bfs_path_length = 0;

        for (int r = 0; r < repeats; r++)
        {
            PathfindResult bfs_result = terrain.find_route_bfs_measured(start, target);
            bfs_total_time += bfs_result.time_microseconds;
            bfs_nodes = bfs_result.nodes_visited;         // Same every run
            bfs_path_length = bfs_result.path_length;     // Same every run

            // Write each individual run to CSV
            csv_file << (route_index + 1) << ",BFS," << (r + 1) << ","
                     << bfs_result.time_microseconds << ","
                     << bfs_result.nodes_visited << ","
                     << bfs_result.path_length << std::endl;
        }

        double bfs_avg_time = bfs_total_time / repeats;

        // Print BFS average to console
        std::cout << std::left
                  << std::setw(8)  << (route_index + 1)
                  << std::setw(10) << "BFS"
                  << std::setw(12) << std::fixed << std::setprecision(1) << bfs_avg_time
                  << std::setw(18) << bfs_nodes
                  << std::setw(14) << bfs_path_length
                  << std::endl;

        // --- A* measurements ---
        double astar_total_time = 0.0;
        int astar_nodes = 0;
        int astar_path_length = 0;

        for (int r = 0; r < repeats; r++)
        {
            PathfindResult astar_result = terrain.find_route_astar_measured(start, target);
            astar_total_time += astar_result.time_microseconds;
            astar_nodes = astar_result.nodes_visited;
            astar_path_length = astar_result.path_length;

            csv_file << (route_index + 1) << ",ASTAR," << (r + 1) << ","
                     << astar_result.time_microseconds << ","
                     << astar_result.nodes_visited << ","
                     << astar_result.path_length << std::endl;
        }

        double astar_avg_time = astar_total_time / repeats;

        // Print A* average to console
        std::cout << std::left
                  << std::setw(8)  << ""
                  << std::setw(10) << "A*"
                  << std::setw(12) << std::fixed << std::setprecision(1) << astar_avg_time
                  << std::setw(18) << astar_nodes
                  << std::setw(14) << astar_path_length
                  << std::endl;

        std::cout << std::string(62, '-') << std::endl;
    }

    csv_file.close();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  BENCHMARK COMPLETE" << std::endl;
    std::cout << "  Results saved to: benchmark_results.csv" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

// ============================================================================
// HOW TO USE:
//
// 1. Add this file to your project (put it next to your other .h files)
//
// 2. Add #include <chrono> to your pch.h (you need this for time measurement)
//
// 3. In scene.cpp, add this include at the top (after #include "scene.h"):
//        #include "pathfinding_benchmark.h"
//
// 4. In Scene::Scene() constructor, add this line AFTER spawn_heroes():
//        run_pathfinding_benchmark(terrain);
//
//    So it looks like:
//        spawn_heroes();
//        run_pathfinding_benchmark(terrain);  // <-- add this line
//        spawn_staves();
//
// 5. Build in RELEASE mode (not Debug!) and run the game.
//    The benchmark will run before the game starts and print results 
//    to the console. It also creates a benchmark_results.csv file.
//
// 6. After you've collected your data, you can remove or comment out
//    the run_pathfinding_benchmark line so the game starts normally.
// ============================================================================
