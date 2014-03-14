//
//  IIRFilter.cpp
//  touchkeys
//
//  Created by Andrew McPherson on 03/02/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#include "IIRFilter.h"
#include <cmath>

// These are static functions to design IIR filters specifically for floating-point datatypes.
// vector<double> and be converted to another type at the end if needed.

void designFirstOrderLowpass(std::vector<double>& bCoeffs, std::vector<double>& aCoeffs,
                                    double cutoffFrequency, double sampleFrequency) {
    bCoeffs.clear();
    aCoeffs.clear();
    
    double omega = tan(M_PI * cutoffFrequency / sampleFrequency);
    double n = 1.0 / (1.0 + omega);
    
    bCoeffs.push_back(omega * n);       // B0
    bCoeffs.push_back(omega * n);       // B1
    aCoeffs.push_back((omega - 1) * n); // A1
}

void designFirstOrderHighpass(std::vector<double>& bCoeffs, std::vector<double>& aCoeffs,
                             double cutoffFrequency, double sampleFrequency) {
    bCoeffs.clear();
    aCoeffs.clear();
    
    double omega = tan(M_PI * cutoffFrequency / sampleFrequency);
    double n = 1.0 / (1.0 + omega);
    
    bCoeffs.push_back(n);               // B0
    bCoeffs.push_back(-n);              // B1
    aCoeffs.push_back((omega - 1) * n); // A1
}

void designSecondOrderLowpass(std::vector<double>& bCoeffs, std::vector<double>& aCoeffs,
                             double cutoffFrequency, double q, double sampleFrequency) {
    bCoeffs.clear();
    aCoeffs.clear();
    
    double omega = tan(M_PI * cutoffFrequency / sampleFrequency);
    double n = 1.0 / (omega*omega + omega/q + 1.0);
    double b0 = n * omega * omega;
    
    bCoeffs.push_back(b0);       // B0
    bCoeffs.push_back(2.0 * b0); // B1
    bCoeffs.push_back(b0);       // B2
    aCoeffs.push_back(2.0 * n * (omega * omega - 1.0));   // A1
    aCoeffs.push_back(n * (omega * omega - omega / q + 1.0));
}

void designSecondOrderHighpass(std::vector<double>& bCoeffs, std::vector<double>& aCoeffs,
                              double cutoffFrequency, double q, double sampleFrequency) {
    bCoeffs.clear();
    aCoeffs.clear();
    
    double omega = tan(M_PI * cutoffFrequency / sampleFrequency);
    double n = 1.0 / (omega*omega + omega/q + 1.0);
    
    bCoeffs.push_back(n);        // B0
    bCoeffs.push_back(-2.0 * n); // B1
    bCoeffs.push_back(n);        // B2
    aCoeffs.push_back(2.0 * n * (omega * omega - 1.0));   // A1
    aCoeffs.push_back(n * (omega * omega - omega / q + 1.0));
}

void designSecondOrderBandpass(std::vector<double>& bCoeffs, std::vector<double>& aCoeffs,
                               double cutoffFrequency, double q, double sampleFrequency) {
    bCoeffs.clear();
    aCoeffs.clear();
    
    double omega = tan(M_PI * cutoffFrequency / sampleFrequency);
    double n = 1.0 / (omega*omega + omega/q + 1.0);
    double b0 = n * omega / q;
    bCoeffs.push_back(b0);       // B0
    bCoeffs.push_back(0.0);      // B1
    bCoeffs.push_back(-b0);      // B2
    aCoeffs.push_back(2.0 * n * (omega * omega - 1.0));   // A1
    aCoeffs.push_back(n * (omega * omega - omega / q + 1.0));
}