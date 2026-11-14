#include <iostream>
#include "FeatureExtractor.h"

int main() {
    FeatureExtractor a;
    for (int i = 1; i <= 5; ++i) a.add_sample(i);
    std::cout << "1..5   -> " << a.get_time_variance() << " (expect 2)\n";

    FeatureExtractor b;
    for (int i = 1; i <= 25; ++i) b.add_sample(i);
    std::cout << "1..25  -> " << b.get_time_variance() << " (expect 33.25)\n";
}
