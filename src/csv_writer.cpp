#include "ctm/csv_writer.hpp"

#include <chrono>
#include <iomanip>

namespace ctm {

CsvWriter::CsvWriter(const std::string& path) : path_(path), out_(path) {
    out_ << "time_s,raw,filtered,seq\n";
}

CsvWriter::~CsvWriter() {
    if (out_) {
        out_.flush();
    }
}

void CsvWriter::write(const FilteredSample& sample) {
    if (!out_) {
        return;
    }
    if (!has_first_) {
        first_ = sample.timestamp;
        has_first_ = true;
    }
    const double t = std::chrono::duration<double>(sample.timestamp - first_).count();
    out_ << std::fixed << std::setprecision(4) << t << ','
         << std::setprecision(6) << sample.raw << ','
         << sample.filtered << ',' << sample.seq << '\n';
}

} // namespace ctm
