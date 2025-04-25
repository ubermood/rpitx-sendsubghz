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
#include <cctype>
#include <cmath>

bool running = true;

// Signal handler to allow graceful termination
void terminate(int num) {
	running = false;
	fprintf(stderr, "Caught signal %d - Terminating\n", num);
}

// Setup signal handlers for termination signals
void setup_signal_handlers() {
	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sa_handler = terminate;

	// Catch common termination signals
	sigaction(SIGINT, &sa, NULL);  // Ctrl+C
	sigaction(SIGTERM, &sa, NULL); // kill command
	sigaction(SIGHUP, &sa, NULL);  // Hangup
	sigaction(SIGPIPE, &sa, NULL); // Broken pipe (less likely here)
}

// Fatal error function
void FATAL_ERROR(const int exitcode, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "FATAL : ");
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(exitcode);
}

// Structure to hold parsed data
struct SubData {
	// Store sequences of pulses. Each inner vector is one burst.
	// If generated from protocol, there will likely be only one sequence.
	// If from RAW_Data lines, each line might become a sequence.
	std::vector<std::vector<ookbursttiming::SampleOOKTiming>> sequences;
	uint64_t frequency = 433920000; // Default frequency
};

// Helper function to convert hex string key to a vector of bits
std::vector<bool> hex_key_to_bits(const std::string& hexstr, int bitcount) {
	std::vector<bool> bits;
	std::string clean_hex;

	// Remove potential non-hex characters and spaces
	for (char c : hexstr) {
		if (isxdigit(c)) {
			clean_hex += c;
		}
	}

	if (clean_hex.empty()) {
		fprintf(stderr, "Warning: Key string '%s' resulted in empty hex data.\n", hexstr.c_str());
		return bits; // Return empty vector
	}

	// Ensure even number of hex digits for byte conversion
	if (clean_hex.length() % 2 != 0) {
		fprintf(stderr, "Warning: Hex key '%s' has odd number of digits. Ignoring last digit.\n", clean_hex.c_str());
		clean_hex.pop_back();
	}

	for (size_t i = 0; i < clean_hex.length(); i += 2) {
		std::string byteStr = clean_hex.substr(i, 2);

		try {
			uint8_t byte = (uint8_t)std::stoul(byteStr, nullptr, 16);
			for (int b = 7; b >= 0; --b) {
				if ((int)bits.size() < bitcount) { // Only add up to bitcount
					bits.push_back((byte >> b) & 1);
				} else {
					break; // Stop if we reached the desired bit count
				}
			}
		} catch (const std::invalid_argument& ia) {
			fprintf(stderr, "Warning: Invalid hex byte string '%s' in key. Skipping.\n", byteStr.c_str());
		} catch (const std::out_of_range& oor) {
			fprintf(stderr, "Warning: Hex byte string '%s' out of range. Skipping.\n", byteStr.c_str());
		}

		if ((int)bits.size() >= bitcount)
			break; // Stop outer loop too
	}

	// Check if we got the expected number of bits
	if ((int)bits.size() != bitcount) {
		fprintf(stderr, "Warning: Expected %d bits from key '%s', but got %zu bits.\n", bitcount, hexstr.c_str(), bits.size());
	}

	return bits;
}

// Protocol Specific Pulse Generation Functions

// Generate pulses for Princeton protocol
void generate_princeton_pulses(const std::vector<bool>& bits, uint64_t te, std::vector<ookbursttiming::SampleOOKTiming>& pulses) {
	if (te == 0) {
		fprintf(stderr, "Warning: TE is zero for Princeton, cannot generate pulses.\n");
		return;
	}

	// Simple OOK based on TE: 1 -> High(TE), Low(TE); 0 -> Low(TE), High(TE)
	// Note: Real Princeton might be different (e.g., tri-state bits), this matches the *original* code's logic.
	for (bool b : bits) {
		if (b) { // Represent '1'
			pulses.push_back({1, te}); // High pulse
			pulses.push_back({0, te}); // Low pulse
		} else { // Represent '0'
			pulses.push_back({0, te}); // Low pulse
			pulses.push_back({1, te}); // High pulse
		}
	}

	// Add a final low pulse often needed as a sync/stop gap
	pulses.push_back({0, te * 30}); // Example: 30 * TE gap, adjust if needed
}

// Generate pulses for EV1527 protocol
void generate_ev1527_pulses(const std::vector<bool>& bits, uint64_t te, std::vector<ookbursttiming::SampleOOKTiming>& pulses) {
	if (te == 0) {
		fprintf(stderr, "Warning: TE is zero for EV1527, cannot generate pulses.\n");
		return;
	}

	// Preamble/Sync pulse: High (1*TE), Low (31*TE) - common values
	pulses.push_back({1, te});
	pulses.push_back({0, 31 * te});

	// Data bits
	for (bool b : bits) {
		if (b) { // Represent '1': High (3*TE), Low (1*TE)
			pulses.push_back({1, 3 * te});
			pulses.push_back({0, 1 * te});
		} else { // Represent '0': High (1*TE), Low (3*TE)
			pulses.push_back({1, 1 * te});
			pulses.push_back({0, 3 * te});
		}
	}

	// Add a final low pulse to ensure the last bit is terminated correctly
	pulses.push_back({0, te});
}

// Generate pulses for KeeLoq protocol (Manchester encoding)
void generate_keeloq_pulses(const std::vector<bool>& bits, uint64_t te, std::vector<ookbursttiming::SampleOOKTiming>& pulses) {
	if (te == 0) {
		fprintf(stderr, "Warning: TE is zero for Keeloq, cannot generate pulses.\n");
		return;
	}

	fprintf(stderr, "Warning: Generating STATIC Keeloq signal based on provided bits.\n");
	fprintf(stderr, "         This is unlikely to work with actual Keeloq receivers due to rolling codes.\n");

	// Add a standard Keeloq preamble if known and desired
	// Example Preamble (Needs verification for specific Keeloq)
	//
	//for(int i=0; i<10; ++i)
	//  pulses.push_back({1, te}); // Short high pulses
	//
	//pulses.push_back({0, 10*te}); // Longer low gap

	// Data bits (Manchester encoding)
	for (bool b : bits) {
		if (b) { // Represent '1': High (TE), Low (TE)
			pulses.push_back({1, te});
			pulses.push_back({0, te});
		} else { // Represent '0': Low (TE), High (TE)
			pulses.push_back({0, te});
			pulses.push_back({1, te});
		}
	}

	// Add a final low pulse for termination
	pulses.push_back({0, te});
}

// Function to parse the Flipper SubGHz file
SubData parse_flipper_sub_file(const std::string& path) {
	std::ifstream infile(path);

	if (!infile.is_open())
		FATAL_ERROR(1, "Could not open file %s\n", path.c_str());

	std::string line;
	SubData result;
	result.sequences.clear(); // Ensure sequences list is empty

	// Temporary storage for protocol definition fields
	bool isProtocolFile = false;
	std::string protocol;
	std::string key;
	int bitcount = 0;
	uint64_t te = 0;

	bool processed_raw = false; // Flag to track if we added any RAW sequences

	while (std::getline(infile, line)) {
		// Trim leading/trailing whitespace
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);

		if (line.empty() || line[0] == '#') continue; // Skip empty lines and comments

		size_t colon_pos = line.find(':');
		if (colon_pos == std::string::npos) continue; // Skip lines without a colon

		std::string field_name = line.substr(0, colon_pos);
		std::string field_value = line.substr(colon_pos + 1);
		field_value.erase(0, field_value.find_first_not_of(" \t")); // Trim value
		field_value.erase(field_value.find_last_not_of(" \t") + 1);

		// --- Store field values ---
		if (field_name == "Frequency") {
			try { result.frequency = std::stoull(field_value); } catch (...) { /* Warn? */ }
		} else if (field_name == "Protocol") {
			protocol = field_value;
			isProtocolFile = true;
		} else if (field_name == "Key") {
			key = field_value;
		} else if (field_name == "Bit") {
			try { bitcount = std::stoi(field_value); } catch (...) { bitcount = 0; }
		} else if (field_name == "TE") {
			try { te = std::stoull(field_value); } catch (...) { te = 0; }
		} else if (field_name == "RAW_Data") {
			// Process this RAW line into a distinct pulse sequence
			std::vector<ookbursttiming::SampleOOKTiming> current_sequence_pulses;
			std::istringstream iss(field_value);

			int64_t raw_value;
			while (iss >> raw_value) {
				ookbursttiming::SampleOOKTiming sample;
				sample.value = (raw_value > 0) ? 1 : 0; // Positive=Pulse(1), Negative=Gap(0)
				sample.duration = static_cast<uint64_t>(std::abs(raw_value));

				if (sample.duration > 0) {
					current_sequence_pulses.push_back(sample);
				} else {
					fprintf(stderr, "Warning: RAW duration is zero (value: %ld). Skipping this entry.\n", raw_value);
				}
			}
			// Add this sequence to our list if it's not empty
			if (!current_sequence_pulses.empty()) {
				result.sequences.push_back(current_sequence_pulses);
				processed_raw = true; // Mark that we successfully processed RAW data
			}
		}
	} // End while loop reading file

	infile.close();

	// Data Processing Decision (After Reading File)

	// If we processed RAW data sequences, we prioritize them over any protocol definition found.
	if (processed_raw && isProtocolFile) {
		fprintf(stderr, "Warning: Both Protocol definition and RAW_Data found. Prioritizing %zu RAW sequence(s).\n", result.sequences.size());
		// Do nothing, result.sequences already holds the RAW data
	}
	// If no RAW data was processed, but a protocol was defined, try to generate pulses from it.
	else if (isProtocolFile && !processed_raw) {
		if (!protocol.empty() && !key.empty() && bitcount > 0 && te > 0) {
			printf("Processing Protocol: %s (Key: %s, Bits: %d, TE: %" PRIu64 "us)\n", protocol.c_str(), key.c_str(), bitcount, te);

			std::vector<bool> bits = hex_key_to_bits(key, bitcount);
			if (!bits.empty() && (int)bits.size() == bitcount) {
				std::vector<ookbursttiming::SampleOOKTiming> protocol_pulses;

				// Generate pulses based on the identified protocol
				if (protocol.find("Princeton") != std::string::npos) {
					generate_princeton_pulses(bits, te, protocol_pulses);
				} else if (protocol.find("EV1527") != std::string::npos) {
					if (bitcount != 24) fprintf(stderr, "Warning: EV1527 protocol usually has 24 bits, but %d were specified.\n", bitcount);
					generate_ev1527_pulses(bits, te, protocol_pulses);
				} else if (protocol.find("Keeloq") != std::string::npos) {
					if (bitcount != 66) fprintf(stderr, "Warning: Keeloq protocol often has 66 bits, but %d were specified.\n", bitcount);
					generate_keeloq_pulses(bits, te, protocol_pulses);
				}
				// *** ADD MORE 'else if' blocks here for other protocols ***
				else {
					fprintf(stderr, "Warning: Protocol '%s' is specified but not supported for generation.\n", protocol.c_str());
				}

				// If generation succeeded, add it as the single sequence
				if (!protocol_pulses.empty()) {
					result.sequences.push_back(protocol_pulses);
				} else {
					fprintf(stderr, "Warning: Failed to generate pulses for protocol %s.\n", protocol.c_str());
				}
			} else {
				fprintf(stderr, "Warning: Failed to get correct number of bits (%d) from key for protocol %s.\n", bitcount, protocol.c_str());
			}
		} else {
			fprintf(stderr, "Warning: Protocol specified, but Key, Bit count, or TE is missing/invalid. Cannot generate sequence.\n");
		}
	}

	// Final Checks & Cleanup
	if (result.sequences.empty()) {
		FATAL_ERROR(2, "No valid RAW_Data sequences or supported/valid Protocol data found/processed in %s\n", path.c_str());
	}

	// Clean up zero-duration pulses within each sequence
	for (auto& seq : result.sequences) {
		seq.erase(std::remove_if(seq.begin(), seq.end(),
			[](const ookbursttiming::SampleOOKTiming& p){ return p.duration == 0; }),
			seq.end());
	}
	
	// Remove any sequences that might have become entirely empty after cleanup
	result.sequences.erase(std::remove_if(result.sequences.begin(), result.sequences.end(),
				[](const std::vector<ookbursttiming::SampleOOKTiming>& seq){ return seq.empty(); }),
			result.sequences.end());

	// Final check again after cleanup
	if (result.sequences.empty()) {
		FATAL_ERROR(2, "Pulse data became empty after processing/cleanup in %s\n", path.c_str());
	}

	return result;
}

// Print usage instructions
void print_usage(const char *progname) {
	printf("Usage: %s [options] <file.sub>\n", progname);
	printf("Options:\n"
			"  -f freq      Override frequency in Hz (e.g., 433920000) (default: from file or 433.92MHz)\n"
			"  -r count     Repeat message this many times (default: 1)\n"
			"  -p pause_us  Microseconds pause between repeats (default: 10000)\n"
			"  -d           Dry run (parse and print info, but don't transmit)\n"
			"  -h           Show this help\n");
	printf("\nSupported Protocols for Generation:\n");
	printf("  - Princeton (Basic OOK implementation)\n");
	printf("  - EV1527 (Fixed Code - Common timings)\n");
	printf("  - Keeloq (STATIC Manchester encoding - Rolling code will likely fail!)\n");
	printf("\nIf RAW_Data is present, it will be used if protocol generation is not supported or fails.\n");
}

int main(int argc, char* argv[]) {
	setup_signal_handlers();

	// Defaults
	int repeat = 1;
	int pause_us = 10000; // Default pause between sequences/repeats
	bool dryrun = false;
	uint64_t override_freq = 0;

	// Parse options using getopt
	int opt;
	while ((opt = getopt(argc, argv, "f:r:p:dh")) != -1) {
		// ... (option parsing remains the same) ...
		switch (opt) {
			case 'f':
				try { override_freq = std::stoull(optarg); } catch (...) { /* Error */ return 1; }
				break;
			case 'r':
				repeat = atoi(optarg);
				if (repeat <= 0) { /* Error */ return 1; }
				break;
			case 'p':
				pause_us = atoi(optarg);
				if (pause_us < 0) { /* Error */ return 1; }
				break;
			case 'd': dryrun = true; break;
			case 'h': print_usage(argv[0]); return 0;
			default: print_usage(argv[0]); return 1;
		}
	}
	if (optind >= argc) { /* Error */ print_usage(argv[0]); return 1; }
	std::string filepath = argv[optind];

	// Parse the .sub file (gets potentially multiple sequences)
	SubData sub = parse_flipper_sub_file(filepath);

	// Override frequency if specified
	if (override_freq > 0) {
		printf("Overriding frequency to %" PRIu64 " Hz\n", override_freq);
		sub.frequency = override_freq;
	}

	// Print Summary
	printf("----------------------------------------\n");
	printf("File:        %s\n", filepath.c_str());
	printf("Frequency:   %" PRIu64 " Hz (%.3f MHz)\n", sub.frequency, sub.frequency / 1.0e6);
	printf("Sequences:   %zu (found in file)\n", sub.sequences.size()); // Number of sequences found
	if (!sub.sequences.empty()) {
		uint64_t total_pulses = 0;
		uint64_t min_dur = -1, max_dur = 0;
		for(const auto& seq : sub.sequences) {
			total_pulses += seq.size();
			uint64_t seq_dur = 0;
			for(const auto& p : seq) seq_dur += p.duration;
			if (seq_dur < min_dur) min_dur = seq_dur;
			if (seq_dur > max_dur) max_dur = seq_dur;
		}
		printf("Total Pulses: %" PRIu64 "\n", total_pulses);
		printf("Seq Duration: min ~%" PRIu64 " us, max ~%" PRIu64 " us\n", min_dur, max_dur);
	}
	printf("Repeats:     %d (of the entire set of sequences)\n", repeat);
	printf("Pause:       %d us (applied between sequences)\n", pause_us);
	printf("----------------------------------------\n");

	// Dry Run Logic
	if (dryrun) {
		printf("Dry-run mode enabled. Parsed sequences and durations:\n");

		for (size_t i = 0; i < sub.sequences.size(); ++i) {
			uint64_t seq_duration_us = 0;
			for (const auto& pulse : sub.sequences[i]) {
				seq_duration_us += pulse.duration;
			}
			printf("  Sequence %zu: %zu pulses, duration %" PRIu64 " us (%.3f ms)\n",
					i + 1, sub.sequences[i].size(), seq_duration_us, seq_duration_us / 1000.0);
			// Optionally print first few pulses of each sequence here
		}

		printf("Transmission would repeat this set %d times with %d us pause between each sequence.\n", repeat, pause_us);
		return 0; // Exit after printing info in dry run mode
	}

	// Transmission Setup
	printf("Initializing transmission on %.3f MHz...\n", sub.frequency / 1.0e6);
	ookbursttiming sender(sub.frequency, 1000000); // 1 second

	// Transmission Loop
	printf("Starting transmission...\n");
	for (int r = 0; r < repeat && running; ++r) {
		if (repeat > 1) {
			printf("--- Repetition %d/%d ---\n", r + 1, repeat);
		}
		for (size_t i = 0; i < sub.sequences.size() && running; ++i) {
			auto& current_sequence = sub.sequences[i];
			if (current_sequence.empty()) continue; // Should have been removed by parser, but check

			uint64_t current_duration_us = 0;
			for (const auto& pulse : current_sequence) {
				current_duration_us += pulse.duration;
			}

			printf(" Sending Sequence %zu/%zu (Duration: %" PRIu64 " us)\n", i + 1, sub.sequences.size(), current_duration_us);

			// Send the current sequence
			sender.SendMessage(current_sequence.data(), current_sequence.size());

			// Pause after sending a sequence, *unless* it's the very last sequence of the very last repeat
			bool is_last_sequence_in_set = (i == sub.sequences.size() - 1);
			bool is_last_repeat = (r == repeat - 1);

			if (!(is_last_sequence_in_set && is_last_repeat) && running && pause_us > 0) {
				// printf(" Pausing for %d us...\n", pause_us); // Uncomment for verbose pause indication
				usleep(pause_us);
			}

			// Check running flag again after potential sleep, in case Ctrl+C was hit
			if (!running) {
				printf("Termination signal received during/after sequence %zu.\n", i + 1);
				break; // Break inner loop (sequences)
			}
		} // End loop over sequences

		if (!running) break; // Break outer loop (repeats)
	} // End loop over repeats

	// Completed
	if (running)
		printf("\nTransmission complete.\n");
	else
		printf("\nTransmission interrupted.\n");

	return 0;
}
