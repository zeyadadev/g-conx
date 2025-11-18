#include "phase01/phase01_test.h"
#include "phase02/phase02_test.h"
#include "phase03/phase03_test.h"
#include "phase04/phase04_test.h"
#include "phase05/phase05_test.h"
#include "phase06/phase06_test.h"
#include "phase07/phase07_test.h"
#include "phase08/phase08_test.h"
#include <iostream>
#include <cstring>

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --phase N    Run phase N test\n";
    std::cout << "  --all        Run all available phases\n";
    std::cout << "  --help       Show this help\n";
}

int main(int argc, char** argv) {
    std::cout << "Venus Plus Test Application\n";

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--phase") == 0) {
        if (argc < 3) {
            std::cerr << "Error: --phase requires phase number\n";
            return 1;
        }

        int phase = atoi(argv[2]);

        switch (phase) {
        case 1:
            return phase01::run_test();
        case 2:
            return phase02::run_test();
        case 3:
            return run_phase03_test() ? 0 : 1;
        case 4:
            return run_phase04_test() ? 0 : 1;
        case 5:
            return run_phase05_test() ? 0 : 1;
        case 6:
            return run_phase06_test() ? 0 : 1;
        case 7:
            return run_phase07_test() ? 0 : 1;
        case 8:
            return run_phase08_test() ? 0 : 1;
        default:
            std::cerr << "Error: Phase " << phase << " not implemented yet\n";
            return 1;
        }
    }

    if (strcmp(argv[1], "--all") == 0) {
        // Run all phases
        int result = phase01::run_test();
        if (result != 0) return result;

        result = phase02::run_test();
        if (result != 0) return result;

        if (!run_phase03_test()) return 1;
        if (!run_phase04_test()) return 1;
        if (!run_phase05_test()) return 1;
        if (!run_phase06_test()) return 1;
        if (!run_phase07_test()) return 1;
        if (!run_phase08_test()) return 1;

        std::cout << "\n";
        std::cout << "=================================================\n";
        std::cout << "All phases completed successfully!\n";
        std::cout << "=================================================\n";
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
