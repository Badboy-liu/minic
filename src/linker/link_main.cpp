#include "LinkerDriver.h"

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

        LinkerDriver driver;
        return driver.run(args);
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
