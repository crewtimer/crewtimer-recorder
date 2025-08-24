#include "FocusScore.hpp"
#include <algorithm>
#include <cmath>

namespace focus
{
  cv::Rect roiFromCenter(const cv::Size &s, cv::Point c, int roiSize)
  {
    roiSize = std::max(8, roiSize);
    int half = roiSize / 2;
    int x = std::clamp(c.x - half, 0, std::max(0, s.width - roiSize));
    int y = std::clamp(c.y - half, 0, std::max(0, s.height - roiSize));
    int w = std::min(roiSize, s.width - x);
    int h = std::min(roiSize, s.height - y);
    return {x, y, w, h};
  }

  static double lapVar(const cv::Mat &gray, int ksize, double sigma)
  {
    cv::Mat tmp;
    if (sigma > 0.0)
      cv::GaussianBlur(gray, tmp, cv::Size(0, 0), sigma);
    else
      tmp = gray;
    cv::Mat lap;
    cv::Laplacian(tmp, lap, CV_64F, ksize);
    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);
    return stddev[0] * stddev[0];
  }

  double scoreROI(const cv::Mat &imgBgrOrGray, const cv::Rect &roi, const Options &opt)
  {
    CV_Assert(!imgBgrOrGray.empty());
    CV_Assert(roi.x >= 0 && roi.y >= 0 &&
              roi.x + roi.width <= imgBgrOrGray.cols &&
              roi.y + roi.height <= imgBgrOrGray.rows);

    // Grayscale view
    cv::Mat gray;
    if (imgBgrOrGray.channels() == 1)
      gray = imgBgrOrGray(roi);
    else
    {
      cv::Mat patch = imgBgrOrGray(roi);
      cv::cvtColor(patch, gray, cv::COLOR_BGR2GRAY);
    }

    // Optional local contrast normalization for robustness across frames
    if (opt.useCLAHE)
    {
      static thread_local cv::Ptr<cv::CLAHE> clahe =
          cv::createCLAHE(2.0, cv::Size(8, 8));
      cv::Mat tmp;
      clahe->apply(gray, tmp);
      gray = std::move(tmp);
    }

    // Multi-scale Laplacian energy (weighted)
    const double L0 = lapVar(gray, opt.lapKsize, opt.sigma0);
    const double L1 = lapVar(gray, opt.lapKsize, opt.sigma1);
    const double L2 = lapVar(gray, opt.lapKsize, opt.sigma2);
    const double sharp = 0.50 * L0 + 0.35 * L1 + 0.15 * L2;

    // Directional-blur penalty via Sobel energy anisotropy
    cv::Mat gx, gy;
    cv::Sobel(gray, gx, CV_64F, 1, 0, 3);
    cv::Sobel(gray, gy, CV_64F, 0, 1, 3);
    const double Ex = cv::mean(gx.mul(gx))[0];
    const double Ey = cv::mean(gy.mul(gy))[0];
    const double anisotropy = std::abs(Ex - Ey) / (Ex + Ey + 1e-12); // 0..1
    const double penalty = opt.anisotropyWt * anisotropy;

    return sharp * (1.0 - penalty);
  }

} // namespace focus
