#include <unistd.h>
#include <librpitx/librpitx.h>
#include <signal.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <inttypes.h>
#include <getopt.h>
#include <algorithm>

bool running = true;
#define MIN_DURATION 10

void terminate(int num) {
    running = false;
    fprintf(stderr, "Caught signal - Terminating\n");
}

void setup_signal_handlers() {
    for (int i = 0; i < 64; i++) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = terminate;
        sigaction(i, &sa, NULL);
    }
}

void FATAL_ERROR(const int exitcode, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "FATAL : ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(exitcode);
}

struct SubData {
    std::vector<ookbursttiming::SampleOOKTiming> pulses;
    uint64_t frequency = 433920000; // default
};

std::vector<bool> hex_key_to_bits(const std::string& hexstr, int bitcount) {
    std::vector<bool> bits;
    std::string clean;
    for (char c : hexstr) {
        if (isxdigit(c)) clean += c;
    }

    for (size_t i = 0; i < clean.length(); i += 2) {
        std::string byteStr = clean.substr(i, 2);
        uint8_t byte = (uint8_t)std::stoi(byteStr, nullptr, 16);
        for (int b = 7; b >= 0; --b) {
            bits.push_back((byte >> b) & 1);
        }
    }

    if ((int)bits.size() > bitcount) bits.resize(bitcount);
    return bits;
}

SubData parse_flipper_sub_file(const std::string& path) {
    std::ifstream infile(path);
    if (!infile.is_open())
        FATAL_ERROR(1, "Could not open file %s\n", path.c_str());

    std::string line;
    SubData result;

    bool isProtocol = false;
    std::string protocol;
    std::string key;
    int bitcount = 0;
    int te = 0;

    while (std::getline(infile, line)) {
        if (line.find("Frequency:") == 0) {
            std::istringstream freq_line(line.substr(10));
            freq_line >> result.frequency;
        }
        if (line.find("Protocol:") == 0) {
            protocol = line.substr(9);
            isProtocol = true;
        }
        if (line.find("Key:") == 0) {
            key = line.substr(4);
        }
        if (line.find("Bit:") == 0) {
            bitcount = std::stoi(line.substr(4));
        }
        if (line.find("TE:") == 0) {
            te = std::stoi(line.substr(3));
        }

        if (!isProtocol && line.find("RAW_Data:") == 0) {
            std::istringstream iss(line.substr(9));
            int value;
            while (iss >> value) {
                ookbursttiming::SampleOOKTiming sample;
                sample.value = (result.pulses.size() % 2 == 0) ? 1 : 0;
                sample.duration = std::abs(value);
                result.pulses.push_back(sample);
            }
        }
    }

    if (isProtocol && protocol.find("Princeton") != std::string::npos) {
        if (key.empty() || te <= 0 || bitcount <= 0)
            FATAL_ERROR(2, "Invalid Princeton format.\n");

        std::vector<bool> bits = hex_key_to_bits(key, bitcount);
        for (bool b : bits) {
            if (b) {
                result.pulses.push_back({1, (uint64_t)te});
                result.pulses.push_back({0, (uint64_t)te});
	    } else {
                result.pulses.push_back({0, (uint64_t)te});
                result.pulses.push_back({1, (uint64_t)te});
	    }
        }
    }

    if (result.pulses.empty())
        FATAL_ERROR(2, "No valid RAW or Protocol data found in %s\n", path.c_str());

    return result;
}

void print_usage(const char *progname) {
    printf("Usage: %s [options] <file.sub>\n", progname);
    printf("Options:\n"
           "  -f freq     Override frequency in Hz (default or from file)\n"
           "  -r count    Repeat message this many times (default: 1)\n"
           "  -p pause    Microseconds pause between repeats (default: 1000)\n"
           "  -d          Dry run (don't transmit)\n"
           "  -h          Show this help\n");
}

int main(int argc, char* argv[]) {
    setup_signal_handlers();

    // Defaults
    int repeat = 1;
    int pause = 1000;
    bool dryrun = false;
    uint64_t override_freq = 0;

    // Parse options
    int opt;
    while ((opt = getopt(argc, argv, "f:r:p:dh")) != -1) {
        switch (opt) {
            case 'f': override_freq = std::strtoull(optarg, nullptr, 10); break;
            case 'r': repeat = atoi(optarg); break;
            case 'p': pause = atoi(optarg); break;
            case 'd': dryrun = true; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    std::string filepath = argv[optind];
    SubData sub = parse_flipper_sub_file(filepath);

    if (override_freq > 0) sub.frequency = override_freq;

    uint64_t total_duration = 0;
    for (const auto& m : sub.pulses)
        total_duration += m.duration;

    printf("Frequency: %" PRIu64 " Hz\n", sub.frequency);
    printf("Total duration: %" PRIu64 "us | Pulses: %zu\n", total_duration, sub.pulses.size());
    printf("Repeats: %d | Pause: %dus\n", repeat, pause);
    if (dryrun) printf("Dry-run mode: No transmission will occur\n");

    ookbursttiming sender(sub.frequency, total_duration);

    for (int i = 0; i < repeat && running; i++) {
        if (!dryrun) {
            sender.SendMessage(sub.pulses.data(), sub.pulses.size());
        } else {
            printf("Simulating transmission...\n");
            usleep(total_duration);
        }

        if (i < repeat - 1 && running)
            usleep(pause);
    }

    printf("Transmission complete.\n");
    return 0;
}

