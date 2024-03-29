/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "denoising.h"

#include "vp8/common/reconinter.h"
#include "vpx/vpx_integer.h"
#include "vpx_mem/vpx_mem.h"
#include "vp8_rtcd.h"

static const unsigned int NOISE_MOTION_THRESHOLD = 25 * 25;
/* SSE_DIFF_THRESHOLD is selected as ~95% confidence assuming
 * var(noise) ~= 100.
 */
static const unsigned int SSE_DIFF_THRESHOLD = 16 * 16 * 20;
static const unsigned int SSE_THRESHOLD = 16 * 16 * 40;
static const unsigned int SSE_THRESHOLD_HIGH = 16 * 16 * 60;

/*
 * The filter function was modified to reduce the computational complexity.
 * Step 1:
 * Instead of applying tap coefficients for each pixel, we calculated the
 * pixel adjustments vs. pixel diff value ahead of time.
 *     adjustment = filtered_value - current_raw
 *                = (filter_coefficient * diff + 128) >> 8
 * where
 *     filter_coefficient = (255 << 8) / (256 + ((absdiff * 330) >> 3));
 *     filter_coefficient += filter_coefficient /
 *                           (3 + motion_magnitude_adjustment);
 *     filter_coefficient is clamped to 0 ~ 255.
 *
 * Step 2:
 * The adjustment vs. diff curve becomes flat very quick when diff increases.
 * This allowed us to use only several levels to approximate the curve without
 * changing the filtering algorithm too much.
 * The adjustments were further corrected by checking the motion magnitude.
 * The levels used are:
 * diff       adjustment w/o motion correction   adjustment w/ motion correction
 * [-255, -16]           -6                                   -7
 * [-15, -8]             -4                                   -5
 * [-7, -4]              -3                                   -4
 * [-3, 3]               diff                                 diff
 * [4, 7]                 3                                    4
 * [8, 15]                4                                    5
 * [16, 255]              6                                    7
 */

int vp8_denoiser_filter_c(unsigned char *mc_running_avg_y, int mc_avg_y_stride,
                          unsigned char *running_avg_y, int avg_y_stride,
                          unsigned char *sig, int sig_stride,
                          unsigned int motion_magnitude,
                          int increase_denoising)
{
    unsigned char *running_avg_y_start = running_avg_y;
    unsigned char *sig_start = sig;
    int sum_diff_thresh;
    int r, c;
    int sum_diff = 0;
    int adj_val[3] = {3, 4, 6};
    int shift_inc1 = 0;
    int shift_inc2 = 1;
    /* If motion_magnitude is small, making the denoiser more aggressive by
     * increasing the adjustment for each level. Add another increment for
     * blocks that are labeled for increase denoising. */
    if (motion_magnitude <= MOTION_MAGNITUDE_THRESHOLD)
    {
      if (increase_denoising) {
        shift_inc1 = 1;
        shift_inc2 = 2;
      }
      adj_val[0] += shift_inc2;
      adj_val[1] += shift_inc2;
      adj_val[2] += shift_inc2;
    }

    for (r = 0; r < 16; ++r)
    {
        for (c = 0; c < 16; ++c)
        {
            int diff = 0;
            int adjustment = 0;
            int absdiff = 0;

            diff = mc_running_avg_y[c] - sig[c];
            absdiff = abs(diff);

            // When |diff| <= |3 + shift_inc1|, use pixel value from
            // last denoised raw.
            if (absdiff <= 3 + shift_inc1)
            {
                running_avg_y[c] = mc_running_avg_y[c];
                sum_diff += diff;
            }
            else
            {
                if (absdiff >= 4 && absdiff <= 7)
                    adjustment = adj_val[0];
                else if (absdiff >= 8 && absdiff <= 15)
                    adjustment = adj_val[1];
                else
                    adjustment = adj_val[2];

                if (diff > 0)
                {
                    if ((sig[c] + adjustment) > 255)
                        running_avg_y[c] = 255;
                    else
                        running_avg_y[c] = sig[c] + adjustment;

                    sum_diff += adjustment;
                }
                else
                {
                    if ((sig[c] - adjustment) < 0)
                        running_avg_y[c] = 0;
                    else
                        running_avg_y[c] = sig[c] - adjustment;

                    sum_diff -= adjustment;
                }
            }
        }

        /* Update pointers for next iteration. */
        sig += sig_stride;
        mc_running_avg_y += mc_avg_y_stride;
        running_avg_y += avg_y_stride;
    }

    sum_diff_thresh= SUM_DIFF_THRESHOLD;
    if (increase_denoising) sum_diff_thresh = SUM_DIFF_THRESHOLD_HIGH;
    if (abs(sum_diff) > sum_diff_thresh) {
      // Before returning to copy the block (i.e., apply no denoising), check
      // if we can still apply some (weaker) temporal filtering to this block,
      // that would otherwise not be denoised at all. Simplest is to apply
      // an additional adjustment to running_avg_y to bring it closer to sig.
      // The adjustment is capped by a maximum delta, and chosen such that
      // in most cases the resulting sum_diff will be within the
      // accceptable range given by sum_diff_thresh.

      // The delta is set by the excess of absolute pixel diff over threshold.
      int delta = ((abs(sum_diff) - sum_diff_thresh) >> 8) + 1;
      // Only apply the adjustment for max delta up to 3.
      if (delta < 4) {
        sig -= sig_stride * 16;
        mc_running_avg_y -= mc_avg_y_stride * 16;
        running_avg_y -= avg_y_stride * 16;
        for (r = 0; r < 16; ++r) {
          for (c = 0; c < 16; ++c) {
            int diff = mc_running_avg_y[c] - sig[c];
            int adjustment = abs(diff);
            if (adjustment > delta)
              adjustment = delta;
            if (diff > 0) {
              // Bring denoised signal down.
              if (running_avg_y[c] - adjustment < 0)
                running_avg_y[c] = 0;
              else
                running_avg_y[c] = running_avg_y[c] - adjustment;
              sum_diff -= adjustment;
            } else if (diff < 0) {
              // Bring denoised signal up.
              if (running_avg_y[c] + adjustment > 255)
                running_avg_y[c] = 255;
              else
                running_avg_y[c] = running_avg_y[c] + adjustment;
              sum_diff += adjustment;
            }
          }
          // TODO(marpan): Check here if abs(sum_diff) has gone below the
          // threshold sum_diff_thresh, and if so, we can exit the row loop.
          sig += sig_stride;
          mc_running_avg_y += mc_avg_y_stride;
          running_avg_y += avg_y_stride;
        }
        if (abs(sum_diff) > sum_diff_thresh)
          return COPY_BLOCK;
      } else {
        return COPY_BLOCK;
      }
    }

    vp8_copy_mem16x16(running_avg_y_start, avg_y_stride, sig_start, sig_stride);
    return FILTER_BLOCK;
}

int vp8_denoiser_allocate(VP8_DENOISER *denoiser, int width, int height)
{
    int i;
    assert(denoiser);

    for (i = 0; i < MAX_REF_FRAMES; i++)
    {
        denoiser->yv12_running_avg[i].flags = 0;

        if (vp8_yv12_alloc_frame_buffer(&(denoiser->yv12_running_avg[i]), width,
                                        height, VP8BORDERINPIXELS)
            < 0)
        {
            vp8_denoiser_free(denoiser);
            return 1;
        }
        vpx_memset(denoiser->yv12_running_avg[i].buffer_alloc, 0,
                   denoiser->yv12_running_avg[i].frame_size);

    }
    denoiser->yv12_mc_running_avg.flags = 0;

    if (vp8_yv12_alloc_frame_buffer(&(denoiser->yv12_mc_running_avg), width,
                                   height, VP8BORDERINPIXELS) < 0)
    {
        vp8_denoiser_free(denoiser);
        return 1;
    }

    vpx_memset(denoiser->yv12_mc_running_avg.buffer_alloc, 0,
               denoiser->yv12_mc_running_avg.frame_size);
    return 0;
}

void vp8_denoiser_free(VP8_DENOISER *denoiser)
{
    int i;
    assert(denoiser);

    for (i = 0; i < MAX_REF_FRAMES ; i++)
    {
        vp8_yv12_de_alloc_frame_buffer(&denoiser->yv12_running_avg[i]);
    }
    vp8_yv12_de_alloc_frame_buffer(&denoiser->yv12_mc_running_avg);
}


void vp8_denoiser_denoise_mb(VP8_DENOISER *denoiser,
                             MACROBLOCK *x,
                             unsigned int best_sse,
                             unsigned int zero_mv_sse,
                             int recon_yoffset,
                             int recon_uvoffset)
{
    int mv_row;
    int mv_col;
    unsigned int motion_magnitude2;
    unsigned int sse_thresh;
    int sse_diff_thresh = 0;
    MV_REFERENCE_FRAME frame = x->best_reference_frame;
    MV_REFERENCE_FRAME zero_frame = x->best_zeromv_reference_frame;

    enum vp8_denoiser_decision decision = FILTER_BLOCK;

    if (zero_frame)
    {
        YV12_BUFFER_CONFIG *src = &denoiser->yv12_running_avg[frame];
        YV12_BUFFER_CONFIG *dst = &denoiser->yv12_mc_running_avg;
        YV12_BUFFER_CONFIG saved_pre,saved_dst;
        MB_MODE_INFO saved_mbmi;
        MACROBLOCKD *filter_xd = &x->e_mbd;
        MB_MODE_INFO *mbmi = &filter_xd->mode_info_context->mbmi;
        int sse_diff = zero_mv_sse - best_sse;

        saved_mbmi = *mbmi;

        /* Use the best MV for the compensation. */
        mbmi->ref_frame = x->best_reference_frame;
        mbmi->mode = x->best_sse_inter_mode;
        mbmi->mv = x->best_sse_mv;
        mbmi->need_to_clamp_mvs = x->need_to_clamp_best_mvs;
        mv_col = x->best_sse_mv.as_mv.col;
        mv_row = x->best_sse_mv.as_mv.row;
        // Bias to zero_mv if small amount of motion.
        // Note sse_diff_thresh is intialized to zero, so this ensures
        // we will always choose zero_mv for denoising if
        // zero_mv_see <= best_sse (i.e., sse_diff <= 0).
        if ((unsigned int)(mv_row * mv_row + mv_col * mv_col)
            <= NOISE_MOTION_THRESHOLD)
            sse_diff_thresh = (int)SSE_DIFF_THRESHOLD;

        if (frame == INTRA_FRAME ||
            sse_diff <= sse_diff_thresh)
        {
            /*
             * Handle intra blocks as referring to last frame with zero motion
             * and let the absolute pixel difference affect the filter factor.
             * Also consider small amount of motion as being random walk due
             * to noise, if it doesn't mean that we get a much bigger error.
             * Note that any changes to the mode info only affects the
             * denoising.
             */
            mbmi->ref_frame =
                    x->best_zeromv_reference_frame;

            src = &denoiser->yv12_running_avg[zero_frame];

            mbmi->mode = ZEROMV;
            mbmi->mv.as_int = 0;
            x->best_sse_inter_mode = ZEROMV;
            x->best_sse_mv.as_int = 0;
            best_sse = zero_mv_sse;
        }

        saved_pre = filter_xd->pre;
        saved_dst = filter_xd->dst;

        /* Compensate the running average. */
        filter_xd->pre.y_buffer = src->y_buffer + recon_yoffset;
        filter_xd->pre.u_buffer = src->u_buffer + recon_uvoffset;
        filter_xd->pre.v_buffer = src->v_buffer + recon_uvoffset;
        /* Write the compensated running average to the destination buffer. */
        filter_xd->dst.y_buffer = dst->y_buffer + recon_yoffset;
        filter_xd->dst.u_buffer = dst->u_buffer + recon_uvoffset;
        filter_xd->dst.v_buffer = dst->v_buffer + recon_uvoffset;

        if (!x->skip)
        {
            vp8_build_inter_predictors_mb(filter_xd);
        }
        else
        {
            vp8_build_inter16x16_predictors_mb(filter_xd,
                                               filter_xd->dst.y_buffer,
                                               filter_xd->dst.u_buffer,
                                               filter_xd->dst.v_buffer,
                                               filter_xd->dst.y_stride,
                                               filter_xd->dst.uv_stride);
        }
        filter_xd->pre = saved_pre;
        filter_xd->dst = saved_dst;
        *mbmi = saved_mbmi;

    }

    mv_row = x->best_sse_mv.as_mv.row;
    mv_col = x->best_sse_mv.as_mv.col;
    motion_magnitude2 = mv_row * mv_row + mv_col * mv_col;
    sse_thresh = SSE_THRESHOLD;
    if (x->increase_denoising) sse_thresh = SSE_THRESHOLD_HIGH;

    if (best_sse > sse_thresh || motion_magnitude2
           > 8 * NOISE_MOTION_THRESHOLD)
    {
        decision = COPY_BLOCK;
    }

    if (decision == FILTER_BLOCK)
    {
        unsigned char *mc_running_avg_y =
            denoiser->yv12_mc_running_avg.y_buffer + recon_yoffset;
        int mc_avg_y_stride = denoiser->yv12_mc_running_avg.y_stride;
        unsigned char *running_avg_y =
            denoiser->yv12_running_avg[INTRA_FRAME].y_buffer + recon_yoffset;
        int avg_y_stride = denoiser->yv12_running_avg[INTRA_FRAME].y_stride;

        /* Filter. */
        decision = vp8_denoiser_filter(mc_running_avg_y, mc_avg_y_stride,
                                         running_avg_y, avg_y_stride,
                                         x->thismb, 16, motion_magnitude2,
                                         x->increase_denoising);
    }
    if (decision == COPY_BLOCK)
    {
        /* No filtering of this block; it differs too much from the predictor,
         * or the motion vector magnitude is considered too big.
         */
        vp8_copy_mem16x16(
                x->thismb, 16,
                denoiser->yv12_running_avg[INTRA_FRAME].y_buffer + recon_yoffset,
                denoiser->yv12_running_avg[INTRA_FRAME].y_stride);
    }
}
