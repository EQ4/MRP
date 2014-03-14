/*
 *  Types.h
 *  keycontrol
 *
 *  Created by Andrew McPherson on 10/13/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef KEYCONTROL_TYPES_H
#define KEYCONTROL_TYPES_H

#include <limits>
#include <cstdlib>
#include <cmath>
#include <utility>

#undef FIXED_POINT_TIME

// The following template specializations give the "missing" values for each kind of data that can be used in a Node.
// If an unknown type is added, its "missing" value is whatever comes back from the default constructor.  Generally speaking, new
// types should be added to this list as they are used

template<typename T>
struct missing_value {
	static const T missing() { return T(); }
	static const bool isMissing(T val) { return val == missing(); }
};

template<> struct missing_value<short> { 
	static const short missing() { return std::numeric_limits<short>::max(); } 
	static const bool isMissing(short val) { return (val == missing()); }
};
template<> struct missing_value<unsigned short> { 
	static const unsigned short missing() { return std::numeric_limits<unsigned short>::max(); } 
	static const bool isMissing(unsigned short val) { return (val == missing()); }
};
template<> struct missing_value<int> {	
	static const int missing() { return std::numeric_limits<int>::max(); } 
	static const bool isMissing(int val) { return (val == missing()); }
};
template<> struct missing_value<unsigned int> { 
	static const unsigned int missing() { return std::numeric_limits<unsigned int>::max(); } 
	static const bool isMissing(unsigned int val) { return (val == missing()); }
};
template<> struct missing_value<long> {	
	static const long missing() { return std::numeric_limits<long>::max(); } 
	static const bool isMissing(long val) { return (val == missing()); }
};
template<> struct missing_value<unsigned long> { 
	static const unsigned long missing() { return std::numeric_limits<unsigned long>::max(); } 
	static const bool isMissing(unsigned long val) { return (val == missing()); }
};
template<> struct missing_value<long long> { 
	static const long long missing() { return std::numeric_limits<long long>::max(); } 
	static const bool isMissing(long long val) { return (val == missing()); }
};
template<> struct missing_value<unsigned long long> { 
	static const unsigned long long missing() { return std::numeric_limits<unsigned long long>::max(); }
	static const bool isMissing(unsigned long long val) { return (val == missing()); }
};
template<> struct missing_value<float> { 
	static const float missing() { return std::numeric_limits<float>::quiet_NaN(); } 
	static const bool isMissing(float val) { return std::isnan(val); }
};
template<> struct missing_value<double> { 
	static const double missing() { return std::numeric_limits<double>::quiet_NaN(); } 
	static const bool isMissing(double val) { return std::isnan(val);  }
};
template<typename T1, typename T2>
struct missing_value<std::pair<T1,T2> > {
	static const std::pair<T1,T2> missing() { return std::pair<T1,T2>(missing_value<T1>::missing(), missing_value<T2>::missing()); }
	static const bool isMissing(std::pair<T1,T2> val) {
		return missing_value<T1>::isMissing(val.first) && missing_value<T2>::isMissing(val.second);
	}
};


// Globally-defined types: these types must be shared by all active units

#ifdef FIXED_POINT_TIME
typedef unsigned long long timestamp_type;
typedef long long timestamp_diff_type;

#define timestamp_abs(x) std::llabs(x)
#define ptime_to_timestamp(x) (x).total_microseconds()
#define timestamp_to_ptime(x) microseconds(x)
#define microseconds_to_timestamp(x) (x)

#else /* Floating point time */
typedef double timestamp_type;
typedef double timestamp_diff_type;

#define timestamp_abs(x) std::fabs(x)
#define ptime_to_timestamp(x) ((timestamp_type)(x).total_microseconds()/1000000.0)
#define timestamp_to_ptime(x) microseconds(x*1000000.0)
#define microseconds_to_timestamp(x) ((double)x/1000000.0)

#endif /* FIXED_POINT_TIME */


#endif /* KEYCONTROL_TYPES_H */