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

std::istream* GetInputStream() {
    return In;
}

std::ostream* GetOutputStream() {
    return Out;
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

void output_double(double x, int64_t width, int64_t precision) {
    if (width > 0) {
        (*Out).width(static_cast<std::streamsize>(width));
    }
    if (precision >= 0) {
        (*Out).precision(static_cast<std::streamsize>(precision));
        (*Out).setf(std::ios::fixed);
    }
    (*Out) << x;
    if (width > 0) {
        (*Out).width(0);
    }
    if (precision >= 0) {
        (*Out).unsetf(std::ios::fixed);
        (*Out).precision(6); // reset to default
    }
}

void output_int64(int64_t x, int64_t width) {
    if (width > 0) {
        (*Out).width(static_cast<std::streamsize>(width));
    }
    (*Out) << x;
    if (width > 0) {
        (*Out).width(0);
    }
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
    std::unordered_map<int32_t, std::ifstream> ReadFiles;
    std::unordered_map<int32_t, std::ofstream> WriteFiles;
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
    ReadFiles.emplace(handle, std::move(fileStream));
    return handle;
}

int32_t file_open_for_write(const char* filename) {
    if (!filename) {
        return -1;
    }
    std::ofstream fileStream(filename, std::ios::binary);
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
    WriteFiles.emplace(handle, std::move(fileStream));
    return handle;
}

int32_t file_open_for_append(const char* filename) {
    if (!filename) {
        return -1;
    }
    std::ofstream fileStream(filename, std::ios::binary | std::ios::app);
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
    WriteFiles.emplace(handle, std::move(fileStream));
    return handle;
}

void file_close(int32_t fileHandle) {
    auto itr = ReadFiles.find(fileHandle);
    if (itr != ReadFiles.end()) {
        itr->second.close();
        ReadFiles.erase(itr);
        FreeFileHandles.push_front(fileHandle);
    }
    auto itw = WriteFiles.find(fileHandle);
    if (itw != WriteFiles.end()) {
        itw->second.close();
        WriteFiles.erase(itw);
        FreeFileHandles.push_front(fileHandle);
    }
}

bool file_has_more_data(int32_t fileHandle) {
    auto it = ReadFiles.find(fileHandle);
    if (it == ReadFiles.end()) {
        return false;
    }
    return !it->second.eof();
}

bool file_eof(int32_t fileHandle) {
    return !file_has_more_data(fileHandle);
}

void input_set_file(int32_t fileHandle) {
    auto it = ReadFiles.find(fileHandle);
    if (it != ReadFiles.end()) {
        SetInputStream(&it->second);
    }
}

void output_set_file(int32_t fileHandle) {
    auto it = WriteFiles.find(fileHandle);
    if (it != WriteFiles.end()) {
        SetOutputStream(&it->second);
    }
}

void input_reset_file() {
    SetInputStream(&std::cin);
}

void output_reset_file() {
    SetOutputStream(&std::cout);
}

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
