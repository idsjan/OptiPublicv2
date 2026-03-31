#pragma once

// Voert alle tests uit en print de resultaten naar de console.
// Wordt aangeroepen vanuit main() nadat de Scene is aangemaakt.
// Geeft true terug als alle tests slagen, false als er minstens 1 faalt.
bool run_all_tests(const Scene& scene);
