#pragma once
#include <iostream>

namespace NQumir {
namespace NRuntime {

void SetOutputStream(std::ostream* os);
void SetInputStream(std::istream* is);
std::istream* GetInputStream();
std::ostream* GetOutputStream();

extern "C" {

double input_double();
int64_t input_int64();

void output_double(double x, int64_t width, int64_t precision);
void output_int64(int64_t x, int64_t width);
void output_string(const char* s);
void output_bool(int64_t b);
void output_symbol(int32_t s);

int32_t file_open_for_read(const char* filename);
void file_close(int32_t fileHandle);
bool file_has_more_data(int32_t fileHandle);

void input_set_file(int32_t fileHandle);
void input_reset_file();

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
