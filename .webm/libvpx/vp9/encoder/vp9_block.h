/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_ENCODER_VP9_BLOCK_H_
#define VP9_ENCODER_VP9_BLOCK_H_

#include "vp9/common/vp9_entropymv.h"
#include "vp9/common/vp9_entropy.h"
#include "vpx_ports/mem.h"
#include "vp9/common/vp9_onyxc_int.h"

#ifdef __cplusplus
extern "C" {
#endif

struct macroblock_plane {
  DECLARE_ALIGNED(16, int16_t, src_diff[64 * 64]);
  int16_t *qcoeff;
  int16_t *coeff;
  uint16_t *eobs;
  struct buf_2d src;

  // Quantizer setings
  int16_t *quant;
  int16_t *quant_shift;
  int16_t *zbin;
  int16_t *round;

  // Zbin Over Quant value
  int16_t zbin_extra;
};

/* The [2] dimension is for whether we skip the EOB node (i.e. if previous
 * coefficient in this block was zero) or not. */
typedef unsigned int vp9_coeff_cost[PLANE_TYPES][REF_TYPES][COEF_BANDS][2]
                                   [COEFF_CONTEXTS][ENTROPY_TOKENS];

typedef struct macroblock MACROBLOCK;
struct macroblock {
  struct macroblock_plane plane[MAX_MB_PLANE];

  MACROBLOCKD e_mbd;
  int skip_block;
  int select_txfm_size;
  int skip_recode;
  int skip_optimize;
  int q_index;

  int errorperbit;
  int sadperbit16;
  int sadperbit4;
  int rddiv;
  int rdmult;
  unsigned int mb_energy;

  int mv_best_ref_index[MAX_REF_FRAMES];
  unsigned int max_mv_context[MAX_REF_FRAMES];
  unsigned int source_variance;
  unsigned int pred_sse[MAX_REF_FRAMES];
  int pred_mv_sad[MAX_REF_FRAMES];

  int nmvjointcost[MV_JOINTS];
  int nmvcosts[2][MV_VALS];
  int *nmvcost[2];
  int nmvcosts_hp[2][MV_VALS];
  int *nmvcost_hp[2];
  int **mvcost;

  int nmvjointsadcost[MV_JOINTS];
  int nmvsadcosts[2][MV_VALS];
  int *nmvsadcost[2];
  int nmvsadcosts_hp[2][MV_VALS];
  int *nmvsadcost_hp[2];
  int **mvsadcost;

  // These define limits to motion vector components to prevent them
  // from extending outside the UMV borders
  int mv_col_min;
  int mv_col_max;
  int mv_row_min;
  int mv_row_max;

  uint8_t zcoeff_blk[TX_SIZES][256];
  int skip;

  int encode_breakout;

  int in_active_map;

  // note that token_costs is the cost when eob node is skipped
  vp9_coeff_cost token_costs[TX_SIZES];

  int in_static_area;

  int optimize;

  // indicate if it is in the rd search loop or encoding process
  int use_lp32x32fdct;
  int skip_encode;

  // Used to store sub partition's choices.
  MV pred_mv[MAX_REF_FRAMES];

  void (*fwd_txm4x4)(const int16_t *input, int16_t *output, int stride);
  void (*itxm_add)(const int16_t *input, uint8_t *dest, int stride, int eob);
};

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP9_ENCODER_VP9_BLOCK_H_
