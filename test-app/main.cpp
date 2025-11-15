#include "phase01/phase01_test.h"
#include "phase02/phase02_test.h"
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

        std::cout << "\n";
        std::cout << "=================================================\n";
        std::cout << "All phases completed successfully!\n";
        std::cout << "=================================================\n";
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
