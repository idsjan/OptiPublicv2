#include "pch.h"
#include "scene.h"
#include "tests.h"

// ============================================================
// Simpele test-hulpmiddelen
// ============================================================
// We gebruiken geen Google Test, want het VulOptiTest-project
// geeft build-problemen. In plaats daarvan gebruiken we een
// simpele macro die checkt of een conditie waar is, en als het
// faalt print welke test en op welke regel het fout ging.
//
// Hoe het werkt:
//   CHECK(1 + 1 == 2)  ->  als het false is, print het de 
//                           regel en markeer de test als gefaald

static int tests_passed = 0;
static int tests_failed = 0;

// __LINE__ is een ingebouwde C++ macro die het regelnummer geeft
// #expr zet de code om naar een string, bijv. CHECK(x == 5) -> "x == 5"
#define CHECK(expr) \
    if (expr) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        std::cout << "  FAIL: " << #expr << " (regel " << __LINE__ << ")" << std::endl; \
    }


// ============================================================
// Test 1: Sorteeralgoritme (unit test)
// ============================================================
// Test of Scene::sort() correct sorteert van laag naar hoog.
// Dit is een unit test: we testen 1 functie in isolatie.

static void test_sort(const Scene& scene)
{
    std::cout << "[Test 1] Sorteeralgoritme" << std::endl;

    // Happy flow: ongesorteerde lijst
    {
        std::vector<int> input = { 500, 100, 900, 300, 700 };
        std::vector<int> result = scene.sort(input);
        std::vector<int> expected = { 100, 300, 500, 700, 900 };
        CHECK(result == expected);
    }

    // Al gesorteerde lijst (moet hetzelfde blijven)
    {
        std::vector<int> input = { 100, 200, 300 };
        std::vector<int> result = scene.sort(input);
        CHECK(result == input);
    }

    // Omgekeerd gesorteerd (worst case voor insertion sort)
    {
        std::vector<int> input = { 900, 700, 500, 300, 100 };
        std::vector<int> result = scene.sort(input);
        std::vector<int> expected = { 100, 300, 500, 700, 900 };
        CHECK(result == expected);
    }

    // Dubbele waarden (meerdere heroes kunnen dezelfde health hebben)
    {
        std::vector<int> input = { 500, 500, 300, 300, 100 };
        std::vector<int> result = scene.sort(input);
        std::vector<int> expected = { 100, 300, 300, 500, 500 };
        CHECK(result == expected);
    }

    // Edge case: lege lijst
    {
        std::vector<int> input = {};
        std::vector<int> result = scene.sort(input);
        CHECK(result.empty());
    }

    // Edge case: 1 element
    {
        std::vector<int> input = { 42 };
        std::vector<int> result = scene.sort(input);
        CHECK(result.size() == 1);
        CHECK(result[0] == 42);
    }
}


// ============================================================
// Test 2: Convex Hull (unit test)
// ============================================================
// Test of Shield::convex_hull() de juiste buitenste punten vindt.
// Shield heeft een default constructor, dus we kunnen gewoon een
// leeg Shield-object aanmaken en de convex_hull functie aanroepen.

// Hulpfunctie: check of een punt in een vector zit
static bool bevat_punt(const std::vector<glm::vec2>& punten, const glm::vec2& zoek)
{
    for (const auto& p : punten)
    {
        // Gebruik een kleine marge omdat floats niet exact vergelijkbaar zijn
        if (glm::abs(p.x - zoek.x) < 0.01f && glm::abs(p.y - zoek.y) < 0.01f)
        {
            return true;
        }
    }
    return false;
}

static void test_convex_hull()
{
    std::cout << "[Test 2] Convex Hull" << std::endl;

    Shield shield; // default constructor, we gebruiken alleen de convex_hull functie

    // Happy flow: driehoek (alle 3 punten moeten in de hull zitten)
    {
        std::vector<glm::vec2> input = {
            glm::vec2(0.f, 0.f),
            glm::vec2(4.f, 0.f),
            glm::vec2(2.f, 3.f)
        };
        std::vector<glm::vec2> hull = shield.convex_hull(input);

        CHECK(hull.size() == 3);
        CHECK(bevat_punt(hull, glm::vec2(0.f, 0.f)));
        CHECK(bevat_punt(hull, glm::vec2(4.f, 0.f)));
        CHECK(bevat_punt(hull, glm::vec2(2.f, 3.f)));
    }

    // Vierkant met binnenliggend punt (het binnenpunt mag NIET in de hull)
    {
        std::vector<glm::vec2> input = {
            glm::vec2(0.f, 0.f),
            glm::vec2(4.f, 0.f),
            glm::vec2(4.f, 4.f),
            glm::vec2(0.f, 4.f),
            glm::vec2(2.f, 2.f)  // dit punt ligt binnen het vierkant
        };
        std::vector<glm::vec2> hull = shield.convex_hull(input);

        CHECK(hull.size() == 4);
        CHECK(!bevat_punt(hull, glm::vec2(2.f, 2.f)));
    }

    // Edge case: twee punten
    {
        std::vector<glm::vec2> input = {
            glm::vec2(0.f, 0.f),
            glm::vec2(5.f, 5.f)
        };
        std::vector<glm::vec2> hull = shield.convex_hull(input);

        CHECK(hull.size() == 2);
    }
}


// ============================================================
// Test 3: Projectiel-schild interactie (integratietest)
// ============================================================
// Dit is GEEN unit test. Het test of meerdere klassen correct
// samenwerken: Shield bouwt een hull op uit hero-posities,
// detecteert een intersectie, en draineert mana van de
// dichtstbijzijnde heroes via absorb().
//
// We testen de keten: heroes -> shield opbouw -> intersectie -> mana drain

static void test_shield_absorb_integration()
{
    std::cout << "[Test 3] Projectiel-schild interactie (integratietest)" << std::endl;

    // --- Stap 1: Maak heroes aan op bekende posities ---
    // We plaatsen 3 heroes in een driehoek-formatie.
    // De y-as in 3D is hoogte, x en z zijn de grondvlak-coordinaten.

    Transform t1;
    t1.position = glm::vec3(0.f, 0.f, 0.f);

    Transform t2;
    t2.position = glm::vec3(10.f, 0.f, 0.f);

    Transform t3;
    t3.position = glm::vec3(5.f, 0.f, 10.f);

    // Hero constructor: (model, texture, transform, name, speed)
    // Model en texture mogen leeg zijn, we gaan niet tekenen
    std::vector<Hero> heroes;
    heroes.emplace_back("", "", t1, "Hero1", 1.f);
    heroes.emplace_back("", "", t2, "Hero2", 1.f);
    heroes.emplace_back("", "", t3, "Hero3", 1.f);

    // Check dat alle heroes starten met mana (standaard 1000)
    int start_mana = heroes[0].get_mana();
    CHECK(start_mana > 0);

    // --- Stap 2: Bouw een schild op basis van de heroes ---
    Shield shield("shield", heroes);

    // --- Stap 3: Test intersectie ---
    // Het midden van de driehoek (ongeveer (5, 3.3)) moet BINNEN het schild liggen
    glm::vec2 punt_binnen = glm::vec2(5.f, 3.f);
    CHECK(shield.intersects(punt_binnen, 1.f));

    // Een punt ver buiten de driehoek moet NIET intersecten
    glm::vec2 punt_buiten = glm::vec2(100.f, 100.f);
    CHECK(!shield.intersects(punt_buiten, 1.f));

    // --- Stap 4: Voer absorb uit en check mana ---
    // absorb() draineert mana_cost (100) van de n_to_sustain (10) dichtstbijzijnde heroes.
    // We hebben maar 3 heroes, dus alle 3 moeten geraakt worden.
    shield.absorb(heroes, punt_binnen);

    // Alle 3 de heroes moeten nu 100 mana minder hebben
    int verwachte_mana = start_mana - 100;
    CHECK(heroes[0].get_mana() == verwachte_mana);
    CHECK(heroes[1].get_mana() == verwachte_mana);
    CHECK(heroes[2].get_mana() == verwachte_mana);
}


// ============================================================
// Hoofdfunctie: voert alle tests uit
// ============================================================

bool run_all_tests(const Scene& scene)
{
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "        TESTS STARTEN" << std::endl;
    std::cout << "========================================" << std::endl;

    tests_passed = 0;
    tests_failed = 0;

    test_sort(scene);
    test_convex_hull();
    test_shield_absorb_integration();

    std::cout << "========================================" << std::endl;
    std::cout << "  Resultaat: " << tests_passed << " geslaagd, "
              << tests_failed << " gefaald" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    return tests_failed == 0;
}
