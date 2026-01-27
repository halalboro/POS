/**
 * This file is part of the Coyote <https://github.com/fpgasystems/Coyote>
 *
 * MIT Licence
 * Copyright (c) 2021-2025, Systems Group, ETH Zurich
 * All rights reserved.
 */

#include <iostream>
#include <cstdlib>
#include <fstream>
#include <chrono>
#include <iomanip>

#include <boost/program_options.hpp>

#include "cThread.hpp"
#include "constants.hpp"

constexpr bool const IS_CLIENT = false;

// Read RoCE RX packet counter from sysfs
uint32_t read_roce_rx_counter() {
    std::ifstream f("/sys/kernel/coyote_sysfs_0/cyt_attr_nstats");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("ROCE RX pkgs") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                return std::stoul(line.substr(pos + 1));
            }
        }
    }
    return 0;
}

double run_bench(
    coyote::cThread &coyote_thread, coyote::rdmaSg &sg,
    int *mem, uint transfers, uint n_runs, bool operation
) {
    constexpr size_t HW_BUFFER_SIZE = 4 * 1024 * 1024;  // Must match main()
    size_t total_bytes = sg.len * transfers;
    size_t total_ints = total_bytes / sizeof(int);

    // Only clear up to buffer size to avoid overflow
    size_t clear_bytes = (total_bytes > HW_BUFFER_SIZE) ? HW_BUFFER_SIZE : total_bytes;
    memset(mem, 0, clear_bytes);

    coyote_thread.clearCompleted();

    // Expected number of packets (one packet per invoke on client)
    uint64_t expected_packets = (uint64_t)transfers * n_runs;

    // Read RX counter before sync
    uint32_t rx_start = read_roce_rx_counter();

    coyote_thread.connSync(IS_CLIENT);  // Sync - tells client we're ready

    // Start timing when client starts sending
    auto start = std::chrono::high_resolution_clock::now();

    // Wait for client to finish
    coyote_thread.connSync(IS_CLIENT);

    // Stop timing
    auto end = std::chrono::high_resolution_clock::now();

    // Read RX counter after benchmark
    uint32_t rx_end = read_roce_rx_counter();
    uint32_t rx_delta = rx_end - rx_start;

    double total_time_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double avg_time_ns = total_time_ns / n_runs;

    // Print RX packet stats
    std::cout << "  [RX] pkts=" << rx_delta << " expected=" << expected_packets;
    if (rx_delta > 0) {
        std::cout << " (" << (100.0 * rx_delta / expected_packets) << "%)";
    }
    std::cout << std::endl;

    // Verify data after benchmark
    if (operation) {
        int correct = 0;
        int non_zero = 0;
        for (size_t i = 0; i < total_ints && i < 20; i++) {
            if (mem[i] != 0) non_zero++;
            if (mem[i] == (int)i) correct++;
        }
        std::cout << "  [DATA] size=" << sg.len << " x" << transfers
                  << ": " << non_zero << "/20 non-zero, "
                  << correct << "/20 correct" << std::endl;
    }

    return avg_time_ns;
}

int main(int argc, char *argv[])  {
    bool operation;
    unsigned int min_size, max_size, n_runs;

    boost::program_options::options_description runtime_options("Coyote Perf RDMA Options");
    runtime_options.add_options()
        ("operation,o", boost::program_options::value<bool>(&operation)->default_value(false), "Benchmark operation: READ(0) or WRITE(1)")
        ("runs,r", boost::program_options::value<unsigned int>(&n_runs)->default_value(N_RUNS_DEFAULT), "Number of times to repeat the test")
        ("min_size,x", boost::program_options::value<unsigned int>(&min_size)->default_value(MIN_TRANSFER_SIZE_DEFAULT), "Starting (minimum) transfer size")
        ("max_size,X", boost::program_options::value<unsigned int>(&max_size)->default_value(MAX_TRANSFER_SIZE_DEFAULT), "Ending (maximum) transfer size");
    boost::program_options::variables_map command_line_arguments;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, runtime_options), command_line_arguments);
    boost::program_options::notify(command_line_arguments);

    HEADER("CLI PARAMETERS:");
    std::cout << "Benchmark operation: " << (operation ? "WRITE" : "READ") << std::endl;
    std::cout << "Number of test runs: " << n_runs << std::endl;
    std::cout << "Starting transfer size: " << min_size << std::endl;
    std::cout << "Ending transfer size: " << max_size << std::endl << std::endl;

    constexpr size_t HW_BUFFER_SIZE = 4 * 1024 * 1024;
    coyote::cThread coyote_thread(DEFAULT_VFPGA_ID, getpid());
    int *mem = (int *) coyote_thread.initRDMA(HW_BUFFER_SIZE, coyote::DEF_PORT);
    if (!mem) { throw std::runtime_error("Could not allocate memory; exiting..."); }

    HEADER("RDMA BENCHMARK: SERVER (measuring RX throughput)");
    unsigned int curr_size = min_size;
    while(curr_size <= max_size) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "SERVER: Testing size " << curr_size << " bytes" << std::endl;
        std::cout << "========================================" << std::endl;

        coyote::rdmaSg sg = { .len = curr_size };

        // Throughput test
        std::cout << "[THROUGHPUT TEST] " << N_THROUGHPUT_REPS << " transfers x " << n_runs << " runs" << std::endl;
        double throughput_time = run_bench(coyote_thread, sg, mem, N_THROUGHPUT_REPS, n_runs, operation);
        double throughput = ((double) N_THROUGHPUT_REPS * (double) curr_size) / (1024.0 * 1024.0 * throughput_time * 1e-9);
        std::cout << "  Size: " << std::setw(8) << curr_size << " B; ";
        std::cout << "Throughput: " << std::setw(10) << std::fixed << std::setprecision(2) << throughput << " MB/s" << std::endl;

        // Latency test
        std::cout << "[LATENCY TEST] " << N_LATENCY_REPS << " transfers x " << n_runs << " runs" << std::endl;
        double latency_time = run_bench(coyote_thread, sg, mem, N_LATENCY_REPS, n_runs, operation);
        std::cout << "  Size: " << std::setw(8) << curr_size << " B; ";
        std::cout << "Latency: " << std::setw(10) << std::fixed << std::setprecision(2) << latency_time / 1e3 << " us" << std::endl;

        curr_size *= 2;
    }

    coyote_thread.connSync(IS_CLIENT);
    return EXIT_SUCCESS;
}
