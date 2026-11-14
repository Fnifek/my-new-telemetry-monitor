#pragma once

#include "ctm/types.hpp"

#include <fstream>
#include <string>

namespace ctm {

// Простейшая CSV-выгрузка для графиков и внешнего анализа.
// Формат: time_s,raw,filtered,seq.
class CsvWriter {
public:
    explicit CsvWriter(const std::string& path);
    ~CsvWriter();

    void write(const FilteredSample& sample);
    bool good() const noexcept { return out_.good(); }
    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    std::ofstream out_;
    bool has_first_ = false;
    TimePoint first_{};
};

} // namespace ctm
