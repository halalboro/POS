#include <iostream>
#include <cmath>
#include "fft2svm_0.h"
#include "hls_stream.h"

// Function to verify results
bool verify_output(float expected, float actual, float tolerance = 0.001) {
    return std::abs(expected - actual) < tolerance;
}

int main() {
    // Create input and output streams
    hls::stream<axis_t> fft_stream;
    hls::stream<axis_t> svm_stream;
    
    // Test case 1: Simple magnitude test
    std::cout << "Running Test 1: Simple magnitude calculation..." << std::endl;
    
    // Fill input stream with test data (16384 samples)
    for (int i = 0; i < 16384; i++) {
        axis_t fft_sample;
        
        // Create some sample FFT data
        // In real application, this would be complex FFT output
        fft_sample.data = static_cast<float>(i % 100); // Some repeating pattern
        fft_sample.keep = -1;
        fft_sample.strb = -1;
        fft_sample.last = (i == 16383);
        
        fft_stream.write(fft_sample);
    }
    
    // Run the FFT2SVM function
    fft2svm_0(fft_stream, svm_stream);
    
    // Check output (32 values)
    bool test_passed = true;
    for (int i = 0; i < 32; i++) {
        if (svm_stream.empty()) {
            std::cout << "Error: Output stream has too few values" << std::endl;
            test_passed = false;
            break;
        }
        
        axis_t out_sample = svm_stream.read();
        
        // Print output value
        std::cout << "SVM Input[" << i << "] = " << out_sample.data << std::endl;
        
        // Verify last signal on final value
        if (i == 31 && !out_sample.last) {
            std::cout << "Error: Last signal not set on final value" << std::endl;
            test_passed = false;
        }
    }
    
    // Make sure we consumed all output
    if (!svm_stream.empty()) {
        std::cout << "Error: Output stream has too many values" << std::endl;
        test_passed = false;
    }
    
    // Test case 2: Zero input
    std::cout << "\nRunning Test 2: Zero input..." << std::endl;
    
    // Fill input stream with zeros
    for (int i = 0; i < 16384; i++) {
        axis_t fft_sample;
        fft_sample.data = 0.0f;
        fft_sample.keep = -1;
        fft_sample.strb = -1;
        fft_sample.last = (i == 16383);
        fft_stream.write(fft_sample);
    }
    
    // Run the conversion
    fft2svm_0(fft_stream, svm_stream);
    
    // All outputs should be zero
    for (int i = 0; i < 32; i++) {
        axis_t out_sample = svm_stream.read();
        if (!verify_output(0.0f, out_sample.data)) {
            std::cout << "Error: Expected 0 but got " << out_sample.data 
                     << " at position " << i << std::endl;
            test_passed = false;
        }
    }
    
    // Print final result
    if (test_passed) {
        std::cout << "\nAll tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\nSome tests failed!" << std::endl;
        return 1;
    }
}