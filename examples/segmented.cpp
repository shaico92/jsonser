// A big data set as a segmented archive: a folder with manifest.json + segment files.
#include <chrono>
#include <iostream>

#include <jsonser/jsonser.hpp>

struct Sample {
    std::uint64_t t = 0;
    double value = 0;
    std::string sensor;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(t) & JSONSER_NVP(value) & JSONSER_NVP(sensor);
    }
};
JSONSER_CLASS_VERSION(Sample, 1)

int main(int argc, char** argv) {
    const std::uint64_t n = argc > 1 ? std::stoull(argv[1]) : 1000000;
    auto t0 = std::chrono::steady_clock::now();
    {
        jsonser::segmented_writer w("samples_archive", {16u << 20});  // 16 MB segments
        w.write("info", std::map<std::string, std::string>{{"site", "lab-3"}, {"unit", "celsius"}});
        auto s = w.stream<Sample>("samples");
        for (std::uint64_t i = 0; i < n; ++i) s.push({i, 20.0 + double(i % 1000) / 100, "s" + std::to_string(i % 8)});
    }  // stream and writer close themselves; manifest.json is written last
    auto t1 = std::chrono::steady_clock::now();

    jsonser::segmented_reader r("samples_archive");
    double sum = 0;
    for (Sample& s : r.stream<Sample>("samples")) sum += s.value;
    auto t2 = std::chrono::steady_clock::now();
    Sample one = r.read_at<Sample>("samples", n / 2);
    auto t3 = std::chrono::steady_clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cout << n << " samples in " << r.info().find("samples")->segments.size() << " segments\n"
              << "write " << ms(t0, t1) << " ms, read all " << ms(t1, t2) << " ms (mean " << sum / double(n)
              << "), read_at(" << n / 2 << ") " << ms(t2, t3) << " ms -> " << jsonser::to_json(one) << "\n";
}
