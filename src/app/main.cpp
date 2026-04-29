#include "Driver.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    try {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }

        Driver driver;
        return driver.run(argv[0], args);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
