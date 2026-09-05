#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
    constexpr uint32_t POLY = 0x82F63B78; // castagnoli polynomial
    std::array<uint32_t, 256> table{};
    std::array<std::array<uint32_t, 256>, 4> stride{};

    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (crc >> 1) ^ POLY : (crc >> 1);
        }
        table[i] = crc;
    }

    stride[0] = table;
    for (int s = 1; s < 4; ++s) {
        for (uint32_t i = 0; i < 256; ++i) {
            stride[s][i] =
                (stride[s - 1][i] >> 8) ^ table[stride[s - 1][i] & 0xFF];
        }
    }

    // Print base table
    std::cout << "static const uint32_t table_[256] = {\n  ";
    for (int i = 0; i < 256; ++i) {
        std::cout << "0x" << std::hex << std::setfill('0') << std::setw(8)
                  << table[i];
        if (i != 255)
            std::cout << ", ";
        if ((i + 1) % 4 == 0 && i != 255)
            std::cout << "\n  ";
    }
    std::cout << "\n};\n\n";

    // Print stride tables
    for (int s = 0; s < 4; ++s) {
        std::cout << "static const uint32_t stride" << s << "[256] = {\n  ";
        for (int i = 0; i < 256; ++i) {
            std::cout << "0x" << std::hex << std::setfill('0') << std::setw(8)
                      << stride[s][i];
            if (i != 255)
                std::cout << ", ";
            if ((i + 1) % 4 == 0 && i != 255)
                std::cout << "\n  ";
        }
        std::cout << "\n};\n\n";
    }

    return 0;
}
