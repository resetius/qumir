#pragma once
#include <iostream>

namespace NQumir {
namespace NRuntime {

void SetOutputStream(std::ostream* os);
void SetInputStream(std::istream* is);

extern "C" {

double input_double();
int64_t input_int64();

void output_double(double x);
void output_int64(int64_t x);
void output_string(const char* s);
void output_bool(int64_t b);
void output_symbol(int32_t s);

int32_t file_open_for_read(const char* filename);
void file_close(int32_t fileHandle);
bool file_has_more_data(int32_t fileHandle);

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
