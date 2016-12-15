/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "Layer.h"
#include "paddle/math/Matrix.h"
#include "paddle/math/BaseMatrix.h"

namespace paddle {
/**
 * @brief A layer for generate prior box locations and variances.
 * - Input: Two and only two input layer are accepted. The input layer must be
 *        be a data output layer and a convolution output layer.
 * - Output: The prior box locations and variances of the input data.
 * Reference:
 *    Wei Liu, Dragomir Anguelov, Dumitru Erhan, Christian Szegedy, Scott Reed,
 *    Cheng-Yang Fu, Alexander C. Berg. SSD: Single Shot MultiBox Detector
 */

class PriorBoxLayer : public Layer {
public:
  explicit PriorBoxLayer(const LayerConfig& config) : Layer(config) {}
  bool init(const LayerMap& layerMap, const ParameterMap& parameterMap);
  void forward(PassType passType);
  void backward(const UpdateCallback& callback) {}
  int numPriors_;
  std::vector<int> minSize_;
  std::vector<int> maxSize_;
  std::vector<float> aspectRatio_;
  std::vector<float> variance_;
  MatrixPtr buffer_;
};

bool PriorBoxLayer::init(const LayerMap& layerMap,
                         const ParameterMap& parameterMap) {
  Layer::init(layerMap, parameterMap);
  auto pbConf = config_.inputs(0).priorbox_conf();
  std::copy(pbConf.min_size().begin(),
            pbConf.min_size().end(),
            std::back_inserter(minSize_));
  std::copy(pbConf.max_size().begin(),
            pbConf.max_size().end(),
            std::back_inserter(maxSize_));
  std::copy(pbConf.aspect_ratio().begin(),
            pbConf.aspect_ratio().end(),
            std::back_inserter(aspectRatio_));
  std::copy(pbConf.variance().begin(),
            pbConf.variance().end(),
            std::back_inserter(variance_));
  // flip
  int inputRatioLength = aspectRatio_.size();
  for (int index = 0; index < inputRatioLength; index++)
    aspectRatio_.push_back(1 / aspectRatio_[index]);
  aspectRatio_.push_back(1.);
  numPriors_ = aspectRatio_.size();
  if (maxSize_.size() > 0) numPriors_++;
  return true;
}

void PriorBoxLayer::forward(PassType passType) {
  Layer::forward(passType);
  auto input = getInput(0);
  int layerWidth = input.getFrameWidth();
  int layerHeight = input.getFrameHeight();

  auto image = getInput(1);
  int imageWidth = image.getFrameWidth();
  int imageHeight = image.getFrameHeight();
  float stepW = static_cast<float>(imageWidth) / layerWidth;
  float stepH = static_cast<float>(imageHeight) / layerHeight;
  int dim = layerHeight * layerWidth * numPriors_ * 4;
  reserveOutput(1, dim * 2);
  // use a cpu buffer to compute
  Matrix::resizeOrCreate(buffer_, 1, dim * 2, false, false);
  auto* tmpPtr = buffer_->getData();

  int idx = 0;
  for (int h = 0; h < layerHeight; ++h) {
    for (int w = 0; w < layerWidth; ++w) {
      float centerX = (w + 0.5) * stepW;
      float centerY = (h + 0.5) * stepH;
      int minSize = 0;
      for (size_t s = 0; s < minSize_.size(); s++) {
        // first prior.
        minSize = minSize_[s];
        int boxWidth = minSize;
        int boxHeight = minSize;
        // xmin, ymin, xmax, ymax.
        tmpPtr[idx++] = (centerX - boxWidth / 2.) / imageWidth;
        tmpPtr[idx++] = (centerY - boxHeight / 2.) / imageHeight;
        tmpPtr[idx++] = (centerX + boxWidth / 2.) / imageWidth;
        tmpPtr[idx++] = (centerY + boxHeight / 2.) / imageHeight;

        if (maxSize_.size() > 0) {
          CHECK_EQ(minSize_.size(), maxSize_.size());
          // second prior.
          for (size_t s = 0; s < maxSize_.size(); s++) {
            int maxSize = maxSize_[s];
            boxWidth = boxHeight = sqrt(minSize * maxSize);
            tmpPtr[idx++] = (centerX - boxWidth / 2.) / imageWidth;
            tmpPtr[idx++] = (centerY - boxHeight / 2.) / imageHeight;
            tmpPtr[idx++] = (centerX + boxWidth / 2.) / imageWidth;
            tmpPtr[idx++] = (centerY + boxHeight / 2.) / imageHeight;
          }
        }
      }
      // rest of priors.
      for (size_t r = 0; r < aspectRatio_.size(); r++) {
        float ar = aspectRatio_[r];
        if (fabs(ar - 1.) < 1e-6) continue;
        float boxWidth = minSize * sqrt(ar);
        float boxHeight = minSize / sqrt(ar);
        tmpPtr[idx++] = (centerX - boxWidth / 2.) / imageWidth;
        tmpPtr[idx++] = (centerY - boxHeight / 2.) / imageHeight;
        tmpPtr[idx++] = (centerX + boxWidth / 2.) / imageWidth;
        tmpPtr[idx++] = (centerY + boxHeight / 2.) / imageHeight;
      }
    }
  }
  // clip the prior's coordidate such that it is within [0, 1]
  for (int d = 0; d < dim; ++d)
    tmpPtr[d] = std::min(std::max(tmpPtr[d], (float)0.), (float)1.);
  // set the variance.
  for (int h = 0; h < layerHeight; h++)
    for (int w = 0; w < layerWidth; w++)
      for (int i = 0; i < numPriors_; i++)
        for (int j = 0; j < 4; j++) tmpPtr[idx++] = variance_[j];
  MatrixPtr outV = getOutputValue();
  outV->copyFrom(buffer_->data_, dim * 2);
}
REGISTER_LAYER(priorbox, PriorBoxLayer);

}  // namespace paddle
