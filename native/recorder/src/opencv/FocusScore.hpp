#pragma once
#include <opencv2/opencv.hpp>

namespace focus
{

  // Tunables for the focus score.
  struct Options
  {
    int roiSize = 128;    // used only by center-point API
    bool useCLAHE = true; // local contrast normalization
    int lapKsize = 3;     // Laplacian kernel size (3 or 5 typical)
    double sigma0 = 0.0;  // multi-scale Laplacian sigmas
    double sigma1 = 1.0;
    double sigma2 = 2.0;
    double anisotropyWt = 0.15; // 0..0.3 typical; penalty for directional blur
  };

  // Build an ROI square around a center point, clamped to image bounds.
  cv::Rect roiFromCenter(const cv::Size &imgSize, cv::Point center, int roiSize);

  // Core focus score on a **rectangular ROI** (in pixels). Bigger = sharper.
  double scoreROI(const cv::Mat &imgBgrOrGray, const cv::Rect &roi, const Options &opt = {});

  // Convenience: focus score on a **square ROI** centered at `center`.
  inline double scoreAt(const cv::Mat &imgBgrOrGray, cv::Point center, const Options &opt = {})
  {
    return scoreROI(imgBgrOrGray, roiFromCenter(imgBgrOrGray.size(), center, opt.roiSize), opt);
  }

} // namespace focus
