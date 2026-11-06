// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/spectrum/spectrum_peaks.h"
#ifdef _WIN32
#define PEAK_EXPORT __declspec(dllexport)
#else
#define PEAK_EXPORT
#endif
extern "C" PEAK_EXPORT int compare_peaks(int n, const double* y, double relative, double absolute, int radius, int* indices, double* prominence, double* widths) {
  spectrum::PeakOptions o; o.relativeProminence=relative; o.minProminence=absolute; o.prominenceRadius=radius;
  try { const auto p=spectrum::findSpectrumPeaks(n,y,o); for(unsigned i=0;i<p.size();++i){indices[i]=p[i].index;prominence[i]=p[i].prominence;widths[i]=p[i].width;} return int(p.size()); } catch(...) {return -1;}
}
