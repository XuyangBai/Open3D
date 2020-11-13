// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2020 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/ml/pytorch/misc/NmsOpKernel.h"

#include "open3d/ml/impl/misc/Nms.h"
#include "open3d/ml/pytorch/TorchHelper.h"
#include "torch/script.h"

#define DIVUP(m, n) ((m) / (n) + ((m) % (n) > 0))

torch::Tensor NmsWithScoreCPU(torch::Tensor boxes,
                              torch::Tensor scores,
                              double nms_overlap_thresh) {
    torch::Tensor order =
            std::get<1>(torch::sort(scores, 0, /*descending=*/true));
    torch::Tensor boxes_sorted =
            torch::index_select(boxes, 0, order).contiguous();
    torch::Tensor keep = torch::zeros(
            {boxes.size(0)}, torch::TensorOptions().dtype(torch::kLong));

    CHECK_CONTIGUOUS(boxes_sorted);
    CHECK_CONTIGUOUS(keep);

    const int num_boxes = boxes_sorted.size(0);
    const int num_block_cols =
            DIVUP(num_boxes, open3d::ml::impl::NMS_BLOCK_SIZE);

    // Call kernel. Results will be saved in masks.
    std::vector<uint64_t> mask(num_boxes * num_block_cols);
    open3d::ml::impl::NmsCPUKernel(boxes_sorted.data_ptr<float>(), mask.data(),
                                   num_boxes, nms_overlap_thresh);

    // Write to keep.
    // remv_cpu has num_boxes bits in total. If the bit is 1, the corresponding
    // box will be removed.
    std::vector<uint64_t> remv_cpu(num_block_cols, 0);
    int64_t *keep_ptr = keep.data_ptr<int64_t>();
    int num_to_keep = 0;
    for (int i = 0; i < num_boxes; i++) {
        int block_col_idx = i / open3d::ml::impl::NMS_BLOCK_SIZE;
        int inner_block_col_idx =
                i % open3d::ml::impl::NMS_BLOCK_SIZE;  // threadIdx.x

        // Querying the i-th bit in remv_cpu, counted from the right.
        // - remv_cpu[block_col_idx]: the block bitmap containing the query
        // - 1ULL << inner_block_col_idx: the one-hot bitmap to extract i
        if (!(remv_cpu[block_col_idx] & (1ULL << inner_block_col_idx))) {
            // Keep the i-th box.
            keep_ptr[num_to_keep++] = i;

            // Any box that overlaps with the i-th box will be removed.
            uint64_t *p = mask.data() + i * num_block_cols;
            for (int j = block_col_idx; j < num_block_cols; j++) {
                remv_cpu[j] |= p[j];
            }
        }
    }

    torch::Tensor selected_keep = torch::slice(keep, 0, 0, num_to_keep);
    return torch::index_select(order, 0, selected_keep);
}

int64_t NmsCPU(torch::Tensor boxes,
               torch::Tensor keep,
               double nms_overlap_thresh) {
    CHECK_CONTIGUOUS(boxes);
    CHECK_CONTIGUOUS(keep);

    const int num_boxes = boxes.size(0);
    const int num_block_cols =
            DIVUP(num_boxes, open3d::ml::impl::NMS_BLOCK_SIZE);

    // Call kernel. Results will be saved in masks.
    std::vector<uint64_t> mask(num_boxes * num_block_cols);
    open3d::ml::impl::NmsCPUKernel(boxes.data_ptr<float>(), mask.data(),
                                   num_boxes, nms_overlap_thresh);

    // Write to keep.
    // remv_cpu has num_boxes bits in total. If the bit is 1, the corresponding
    // box will be removed.
    std::vector<uint64_t> remv_cpu(num_block_cols, 0);
    int64_t *keep_ptr = keep.data_ptr<int64_t>();
    int num_to_keep = 0;
    for (int i = 0; i < num_boxes; i++) {
        int block_col_idx = i / open3d::ml::impl::NMS_BLOCK_SIZE;
        int inner_block_col_idx =
                i % open3d::ml::impl::NMS_BLOCK_SIZE;  // threadIdx.x

        // Querying the i-th bit in remv_cpu, counted from the right.
        // - remv_cpu[block_col_idx]: the block bitmap containing the query
        // - 1ULL << inner_block_col_idx: the one-hot bitmap to extract i
        if (!(remv_cpu[block_col_idx] & (1ULL << inner_block_col_idx))) {
            // Keep the i-th box.
            keep_ptr[num_to_keep++] = i;

            // Any box that overlaps with the i-th box will be removed.
            uint64_t *p = mask.data() + i * num_block_cols;
            for (int j = block_col_idx; j < num_block_cols; j++) {
                remv_cpu[j] |= p[j];
            }
        }
    }

    return num_to_keep;
}