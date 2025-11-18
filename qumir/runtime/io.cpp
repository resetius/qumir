#include "io.h"

#include <unordered_map>
#include <forward_list>
#include <fstream>

namespace NQumir {
namespace NRuntime {

namespace {

std::istream *In = &std::cin;
std::ostream *Out = &std::cout;

};

void SetOutputStream(std::ostream* os) {
    Out = os ? os : &std::cout;
}

void SetInputStream(std::istream* is) {
    In = is ? is : &std::cin;
}

extern "C" {

double input_double() {
    double x;
    (*In) >> x;
    return x;
}

int64_t input_int64() {
    int64_t x;
    (*In) >> x;
    return x;
}

void output_double(double x) {
    (*Out) << x;
}

void output_int64(int64_t x) {
    (*Out) << x;
}

void output_string(const char* s) {
    if (!s) {return;}
    (*Out) << s;
}

void output_bool(int64_t b) {
    (*Out) << (b ? "да" : "нет");
}

void output_symbol(int32_t s) {
    // convert unicode to utf-8
    if (s < 0x80) {
        (*Out) << static_cast<char>(s);
    } else if (s < 0x800) {
        (*Out) << static_cast<char>(0b11000000 | ((s >> 6) & 0b00011111));
        (*Out) << static_cast<char>(0b10000000 | (s & 0b00111111));
    } else if (s < 0x10000) {
        (*Out) << static_cast<char>(0b11100000 | ((s >> 12) & 0b00001111));
        (*Out) << static_cast<char>(0b10000000 | ((s >> 6) & 0b00111111));
        (*Out) << static_cast<char>(0b10000000 | (s & 0b00111111));
    } else if (s <= 0x10FFFF) {
        (*Out) << static_cast<char>(0b11110000 | ((s >> 18) & 0b00000111));
        (*Out) << static_cast<char>(0b10000000 | ((s >> 12) & 0b00111111));
        (*Out) << static_cast<char>(0b10000000 | ((s >> 6) & 0b00111111));
        (*Out) << static_cast<char>(0b10000000 | (s & 0b00111111));
    }
}

namespace {
    std::unordered_map<int32_t, std::ifstream> OpenFiles;
    std::forward_list<int32_t> FreeFileHandles;
    int32_t NextFileHandle = 1;
};

int32_t file_open_for_read(const char* filename) {
    if (!filename) {
        return -1;
    }
    std::ifstream fileStream(filename, std::ios::binary);
    if (!fileStream.is_open()) {
        return -1;
    }
    int32_t handle;
    if (!FreeFileHandles.empty()) {
        handle = FreeFileHandles.front();
        FreeFileHandles.pop_front();
    } else {
        handle = NextFileHandle++;
    }
    OpenFiles.emplace(handle, std::move(fileStream));
    return handle;
}

void file_close(int32_t fileHandle) {
    auto it = OpenFiles.find(fileHandle);
    if (it != OpenFiles.end()) {
        it->second.close();
        OpenFiles.erase(it);
        FreeFileHandles.push_front(fileHandle);
    }
}

bool file_has_more_data(int32_t fileHandle) {
    auto it = OpenFiles.find(fileHandle);
    if (it == OpenFiles.end()) {
        return false;
    }
    return !it->second.eof();
}

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
