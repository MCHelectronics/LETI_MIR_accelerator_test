#include <iostream>
#include <limits>

int main() {
    std::cout << "C++ standard: " << __cplusplus << '\n'
              << "Compiler: " << __VERSION__ << '\n'
              << "float bytes: " << sizeof(float) << '\n'
              << "IEEE-754 float: " << std::boolalpha
              << std::numeric_limits<float>::is_iec559 << '\n';
    return (__cplusplus >= 201703L && sizeof(float) == 4 &&
            std::numeric_limits<float>::is_iec559) ? 0 : 1;
}
