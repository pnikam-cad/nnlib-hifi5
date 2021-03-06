/*******************************************************************************
* Copyright (c) 2018-2021 Cadence Design Systems, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to use this Software with Cadence processor cores only and
* not with any other processors and platforms, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

******************************************************************************/
#include "xa_nnlib_common.h"
#include "xa_nnlib_common_macros_hifi5.h"

#define MULTIPLYBYQUANTIZEDMULTIPLIER_X2(inp, multiplier, left_shift, right_shift) \
    inp = AE_SLAA32(inp, left_shift); \
    inp = AE_MULFP32X2RAS(inp, AE_MOVDA32(multiplier)); \
    inp = AE_SRAA32SYMS(inp, right_shift);

#define MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out, inp1, inp2, multiplier, l_shift, r_shift, out_off) \
  AE_MUL2P32X4S(inp1, inp2, inp1, inp2, l_shift, l_shift); \
  AE_MULF2P32X4RAS(inp1, inp2, inp1, inp2, AE_MOVDA32(multiplier), AE_MOVDA32(multiplier)); \
  inp1 = AE_SRAA32SYMS(inp1, r_shift); \
  inp2 = AE_SRAA32SYMS(inp2, r_shift); \
  out = AE_SAT16X4(inp1, inp2); \
  out = AE_ADD16S(AE_MOVDA16(out_off), out); \
  AE_MINMAX16(out, AE_ZERO16(), AE_MOVDA16(255)); 

#define PACK_32X2(dst1, src1, src2) \
  dst1 = AE_SEL8X8(AE_MOVINT8X8_FROMINT16X4(src1), AE_MOVINT8X8_FROMINT16X4(src2), AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(0x080a0c0e, 0x00020406)));

extern const long long g_sel_pattern[16];

const long long pre_loop_sel_pattern[16] = {
 0x00000000L, 0x00000000L,
 0x00eeddccL, 0xbbaa9988L,
 0x0000ddccL, 0xbbaa9988L,
 0x000000ccL, 0xbbaa9988L,
 0x00000000L, 0xbbaa9988L,
 0x00000000L, 0x00aa9988L,
 0x00000000L, 0x00009988L,
 0x00000000L, 0x00000088L
};

const long long post_loop_sel_pattern[16] = {
 0xffeeddccL, 0xbbaa9988L,
 0xff000000L, 0x00000000L,
 0xffee0000L, 0x00000000L,
 0xffeedd00L, 0x00000000L,
 0xffeeddccL, 0x00000000L,
 0xffeeddccL, 0xbb000000L,
 0xffeeddccL, 0xbbaa0000L,
 0xffeeddccL, 0xbbaa9900L
};

static inline void special_function_for_cols_mul_32
    (UWORD8*       p_out_0
    ,const UWORD8* p_mat1_
    ,const UWORD8* p_vec1_0
    ,const WORD32* p_bias_0
    ,WORD32        n_rows
    ,WORD32        n_vecs
    ,WORD32        cols
    ,WORD32        out_mul
    ,ae_int32x2    l_mul
    ,WORD32        r_shift
    ,WORD32        out_z_b
    ,WORD32        out_stride_
    ,WORD32        row_offset
    ,WORD32        vec_offset_
    ,WORD32        out_offset_
    )
{
    ae_int8x8 mat1_row0_0, mat1_row0_1, mat1_row0_2, mat1_row0_3;
    ae_int8x8 mat1_row1_0, mat1_row1_1, mat1_row1_2, mat1_row1_3;
    ae_int8x8 mat1_row2_0, mat1_row2_1, mat1_row2_2, mat1_row2_3;
    ae_int8x8 mat1_row3_0, mat1_row3_1, mat1_row3_2, mat1_row3_3;

    ae_int8x8 vec0_batch_0, vec0_batch_1, vec0_batch_2, vec0_batch_3; 
    ae_int8x8 vec1_batch_0, vec1_batch_1, vec1_batch_2, vec1_batch_3; 
    ae_int8x8 vec2_batch_0, vec2_batch_1, vec2_batch_2, vec2_batch_3; 
    ae_int8x8 vec3_batch_0, vec3_batch_1, vec3_batch_2, vec3_batch_3;

    ae_int32x4 *pt_bias;
    ae_valignx2 bias_a;
    ae_int32x2 d_bias0, d_bias1;

    pt_bias = (ae_int32x4 *)p_bias_0;
    bias_a = AE_LA128_PP(pt_bias);

    ae_int32x2 bias_buffer[4];

    int m_itr = 0, vec_itr = 0;
    for(m_itr = 0; m_itr < (n_rows & ~(4 - 1)); m_itr += 4)
    {
      /* Load bias values */
      AE_LA32X2X2_IP(d_bias0, d_bias1, bias_a, pt_bias);
      AE_S32X2X2_I(d_bias0, d_bias0, (ae_int32x4*)bias_buffer, 0);
      AE_S32X2X2_I(d_bias1, d_bias1, (ae_int32x4*)bias_buffer, 16);

      WORD8* p_dst_0 = (WORD8*)p_out_0 + (m_itr + 0) * out_stride_;

      for (vec_itr = 0; vec_itr < (n_vecs & ~(4 - 1)); vec_itr += 4)
      {
        ae_int8* p_vec_0  = (ae_int8 *)(p_vec1_0 + vec_itr * vec_offset_);
        ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1_[(m_itr + 0) * row_offset];

        ae_int32x2 acc_row0_vec0;
        ae_int32x2 acc_row1_vec0;
        ae_int32x2 acc_row0_vec1;
        ae_int32x2 acc_row1_vec1;
        ae_int32x2 acc_row0_vec2;
        ae_int32x2 acc_row1_vec2;
        ae_int32x2 acc_row0_vec3;
        ae_int32x2 acc_row1_vec3;

        /* Initialize accumulators with bias */
        AE_L32X2X2_I(acc_row0_vec0, acc_row0_vec1, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row0_vec2, acc_row0_vec3, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row1_vec0, acc_row1_vec1, (ae_int32x4*)bias_buffer, 16);
        AE_L32X2X2_I(acc_row1_vec2, acc_row1_vec3, (ae_int32x4*)bias_buffer, 16);

        int c_itr = 0;

#pragma loop_count min=2
        for(c_itr = 0; c_itr < cols>>5; c_itr++)
        {
          /* Load 4 rows */
          AE_L8X8X2_X(mat1_row0_0, mat1_row0_1, (ae_int8x16*)p_mat1_0, 0);
          AE_L8X8X2_X(mat1_row0_2, mat1_row0_3, (ae_int8x16*)p_mat1_0, 16);
          AE_L8X8X2_X(mat1_row1_0, mat1_row1_1, (ae_int8x16*)p_mat1_0, cols);
          AE_L8X8X2_X(mat1_row1_2, mat1_row1_3, (ae_int8x16*)p_mat1_0, cols+16);
          AE_L8X8X2_X(mat1_row2_0, mat1_row2_1, (ae_int8x16*)p_mat1_0, 2*cols);
          AE_L8X8X2_X(mat1_row2_2, mat1_row2_3, (ae_int8x16*)p_mat1_0, 2*cols+16);
          AE_L8X8X2_X(mat1_row3_0, mat1_row3_1, (ae_int8x16*)p_mat1_0, 3*cols);
          AE_L8X8X2_X(mat1_row3_2, mat1_row3_3, (ae_int8x16*)p_mat1_0, 3*cols+16);

          /* Load  4 vectors  */
          AE_L8X8X2_X(vec0_batch_0, vec0_batch_1, (ae_int8x16*)p_vec_0, 0);
          AE_L8X8X2_X(vec0_batch_2, vec0_batch_3, (ae_int8x16*)p_vec_0, 16);
          AE_L8X8X2_X(vec1_batch_0, vec1_batch_1, (ae_int8x16*)p_vec_0, cols);
          AE_L8X8X2_X(vec1_batch_2, vec1_batch_3, (ae_int8x16*)p_vec_0, cols+16);
          AE_L8X8X2_X(vec2_batch_0, vec2_batch_1, (ae_int8x16*)p_vec_0, 2*cols);
          AE_L8X8X2_X(vec2_batch_2, vec2_batch_3, (ae_int8x16*)p_vec_0, 2*cols+16);
          AE_L8X8X2_X(vec3_batch_0, vec3_batch_1, (ae_int8x16*)p_vec_0, 3*cols);
          AE_L8X8X2_X(vec3_batch_2, vec3_batch_3, (ae_int8x16*)p_vec_0, 3*cols+16);
          
          AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
          AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
          AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
          AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

          AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
          AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
          AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
          AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);

          AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec0_batch_2);
          AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec1_batch_2);
          AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec2_batch_2);
          AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec3_batch_2);

          AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec0_batch_3);
          AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec1_batch_3);
          AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec2_batch_3);
          AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec3_batch_3);

          p_mat1_0 = (ae_int8x8 *)((ae_int8 *)p_mat1_0 + 32);
          p_vec_0 += 32;
        }   

        /* Apply quantization */
        ae_int16x4 out_0, out_1, out_2, out_3;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_mul, l_mul, r_shift, out_z_b);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_mul, l_mul, r_shift, out_z_b);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_mul, l_mul, r_shift, out_z_b);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_mul, l_mul, r_shift, out_z_b);

        /* Store output */
        ae_int8x8 out32_0, out32_1; 
        PACK_32X2(out32_0, out_0, out_1);
        PACK_32X2(out32_1, out_2, out_3);

        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset_);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset_);
        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset_);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset_);
      }
      /*
        for (; vec_itr < vec_count; vec_itr++)
        {
        }
      */
    }
}

static inline void _xa_nn_dot_product_4_rows_4_vecs_aligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_0_1
    ,ae_int32x2* out_0_2
    ,ae_int32x2* out_0_3
    ,ae_int32x2* out_1_0
    ,ae_int32x2* out_1_1
    ,ae_int32x2* out_1_2
    ,ae_int32x2* out_1_3
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols
    ,WORD32      row_offset
    ,WORD32      vec_offset 
    ,WORD32      mat1_zero_bias
    )
{
    int c_itr = 0;

    ae_int8x8 neg_mat_bias = AE_MOVDA8((UWORD8)mat1_zero_bias);
    int rem_cols = cols & 15;
    int rem_g8 = ((rem_cols & 15) > 8)?1:0;
    ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8 + 1]));
    ae_int8x8 sel2 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8 + 1]));
  
    ae_int8x8 mat1_row0_0, mat1_row0_1;
    ae_int8x8 mat1_row1_0, mat1_row1_1;
    ae_int8x8 mat1_row2_0, mat1_row2_1;
    ae_int8x8 mat1_row3_0, mat1_row3_1;

    ae_int8x8 vec0_batch_0, vec0_batch_1; 
    ae_int8x8 vec1_batch_0, vec1_batch_1; 
    ae_int8x8 vec2_batch_0, vec2_batch_1; 
    ae_int8x8 vec3_batch_0, vec3_batch_1; 

    ae_int8x8* p_mat1_1 = (ae_int8x8*)((ae_int8*)p_mat1_0 + row_offset); 
    ae_int8x8* p_mat1_2 = (ae_int8x8*)((ae_int8*)p_mat1_1 + row_offset);
    ae_int8x8* p_mat1_3 = (ae_int8x8*)((ae_int8*)p_mat1_2 + row_offset);

    ae_int8* p_vec_1 = p_vec_0 + vec_offset; 
    ae_int8* p_vec_2 = p_vec_1 + vec_offset;
    ae_int8* p_vec_3 = p_vec_2 + vec_offset;

    ae_int32x2 acc_row0_vec0 = *out_0_0;
    ae_int32x2 acc_row0_vec1 = *out_0_1;
    ae_int32x2 acc_row0_vec2 = *out_0_2;
    ae_int32x2 acc_row0_vec3 = *out_0_3;
                       
    ae_int32x2 acc_row1_vec0 = *out_1_0;
    ae_int32x2 acc_row1_vec1 = *out_1_1;
    ae_int32x2 acc_row1_vec2 = *out_1_2;
    ae_int32x2 acc_row1_vec3 = *out_1_3;

    int cols_count = cols -(cols & 15);

    for(c_itr = 0; c_itr < cols_count>>4; c_itr++)
    {
        AE_L8X8X2_IP(vec0_batch_0, vec0_batch_1, (ae_int8x16 *)p_vec_0, 16);
        AE_L8X8X2_IP(vec1_batch_0, vec1_batch_1, (ae_int8x16 *)p_vec_1, 16);
        AE_L8X8X2_IP(vec2_batch_0, vec2_batch_1, (ae_int8x16 *)p_vec_2, 16);
        AE_L8X8X2_IP(vec3_batch_0, vec3_batch_1, (ae_int8x16 *)p_vec_3, 16);

        AE_L8X8X2_IP(mat1_row0_0, mat1_row0_1, (ae_int8x16 *)p_mat1_0, 16);
        AE_L8X8X2_IP(mat1_row1_0, mat1_row1_1, (ae_int8x16 *)p_mat1_1, 16);
        AE_L8X8X2_IP(mat1_row2_0, mat1_row2_1, (ae_int8x16 *)p_mat1_2, 16);
        AE_L8X8X2_IP(mat1_row3_0, mat1_row3_1, (ae_int8x16 *)p_mat1_3, 16);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);
    }  

    //Remainder loop for cols
    c_itr <<= 4;
    while(c_itr < cols)
    {
        AE_L8X8_IP(mat1_row0_0, p_mat1_0, 8);
        AE_L8X8_IP(mat1_row1_0, p_mat1_1, 8);
        AE_L8X8_IP(mat1_row2_0, p_mat1_2, 8);
        AE_L8X8_IP(mat1_row3_0, p_mat1_3, 8);

        AE_L8X8_IP(vec0_batch_0, (ae_int8x8 *)p_vec_0, 8);
        AE_L8X8_IP(vec1_batch_0, (ae_int8x8 *)p_vec_1, 8);
        AE_L8X8_IP(vec2_batch_0, (ae_int8x8 *)p_vec_2, 8);
        AE_L8X8_IP(vec3_batch_0, (ae_int8x8 *)p_vec_3, 8);

        mat1_row0_0 = AE_SEL8X8(mat1_row0_0, neg_mat_bias, sel1);
        mat1_row1_0 = AE_SEL8X8(mat1_row1_0, neg_mat_bias, sel1);
        mat1_row2_0 = AE_SEL8X8(mat1_row2_0, neg_mat_bias, sel1);
        mat1_row3_0 = AE_SEL8X8(mat1_row3_0, neg_mat_bias, sel1);
    
        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);
        c_itr += 8;
        sel1 = sel2;
    }

    *out_0_0 = acc_row0_vec0;
    *out_0_1 = acc_row0_vec1;
    *out_0_2 = acc_row0_vec2;
    *out_0_3 = acc_row0_vec3;

    *out_1_0 = acc_row1_vec0;
    *out_1_1 = acc_row1_vec1;
    *out_1_2 = acc_row1_vec2;
    *out_1_3 = acc_row1_vec3;
}

static inline void _xa_nn_dot_product_4_rows_1_vecs_aligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_1_0
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols
    ,WORD32      row_offset
    ,WORD32      vec1_zero_bias
    )
{
  int c_itr = 0;

  ae_int8x8 neg_vec_bias = AE_MOVDA8((UWORD8)vec1_zero_bias);
  int rem_cols = cols & 15;
  int rem_g8 = ((rem_cols & 15) > 8)?1:0;
  ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8 + 1]));
  ae_int8x8 sel2 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8 + 1]));
  
  ae_int8x8 mat1_row0_0, mat1_row0_1;
  ae_int8x8 mat1_row1_0, mat1_row1_1;
  ae_int8x8 mat1_row2_0, mat1_row2_1;
  ae_int8x8 mat1_row3_0, mat1_row3_1;
  ae_int8x8 vec0_batch_0, vec0_batch_1; 

  ae_int8x8 *p_mat1_1 = (ae_int8x8*)((WORD8 *)p_mat1_0 + row_offset); 
  ae_int8x8 *p_mat1_2 = (ae_int8x8*)((WORD8 *)p_mat1_1 + row_offset); 
  ae_int8x8 *p_mat1_3 = (ae_int8x8*)((WORD8 *)p_mat1_2 + row_offset); 

  ae_int32x2 acc_row0_vec0 = *out_0_0;
  ae_int32x2 acc_row1_vec0 = *out_1_0;

  int cols_count=cols-(cols&15);
  for(c_itr = 0; c_itr < cols_count>>4; c_itr++)
  {
      AE_L8X8X2_IP(vec0_batch_0, vec0_batch_1, (ae_int8x16 *)p_vec_0, 16);

      AE_L8X8X2_IP(mat1_row0_0, mat1_row0_1, (ae_int8x16 *)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row1_0, mat1_row1_1, (ae_int8x16 *)p_mat1_1, 16);
      AE_L8X8X2_IP(mat1_row2_0, mat1_row2_1, (ae_int8x16 *)p_mat1_2, 16);
      AE_L8X8X2_IP(mat1_row3_0, mat1_row3_1, (ae_int8x16 *)p_mat1_3, 16);

      AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);

      AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
  }

  //Remainder loop for cols
  c_itr <<= 4;
  while(c_itr < cols)
  {
      AE_L8X8_IP(mat1_row0_0, p_mat1_0, 8);
      AE_L8X8_IP(mat1_row1_0, p_mat1_1, 8);
      AE_L8X8_IP(mat1_row2_0, p_mat1_2, 8);
      AE_L8X8_IP(mat1_row3_0, p_mat1_3, 8);

      AE_L8X8_IP(vec0_batch_0, (ae_int8x8 *)p_vec_0, 8);
      vec0_batch_0 = AE_SEL8X8(vec0_batch_0, neg_vec_bias, sel1);

      AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
      c_itr += 8;
      sel1 = sel2;
  }

  *out_0_0 = acc_row0_vec0;
  *out_1_0 = acc_row1_vec0;
}

static inline void _xa_nn_dot_product_1_rows_1_vecs_aligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_1_0
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols1
    ,WORD32      vec1_zero_bias
    )
{
    int c_itr = 0;
    ae_int8x8 vec0_batch_0, vec0_batch_1; 
    ae_int8x8 mat1_row0_0, mat1_row0_1;

    ae_int32x2 acc_row0_vec0 = *out_0_0;
    ae_int32x2 acc_row0_vec1 = *out_1_0;

    ae_int8x8 neg_vec_bias = AE_MOVDA8((UWORD8)vec1_zero_bias);
    int rem_cols = cols1 & 15;
    int rem_g8 = ((rem_cols & 15) > 8)?1:0;
    ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8 + 1]));
    ae_int8x8 sel2 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8 + 1]));

    int cols_count = cols1 - (cols1 & 15);

    for(c_itr = 0; c_itr < cols_count >> 4; c_itr++)
    {
        AE_L8X8X2_IP(vec0_batch_0, vec0_batch_1, (ae_int8x16 *)p_vec_0, 16);

        AE_L8X8X2_IP(mat1_row0_0, mat1_row0_1, (ae_int8x16 *)p_mat1_0, 16);
      
        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row0_vec1 , mat1_row0_0 , mat1_row0_0 , mat1_row0_0 , mat1_row0_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row0_vec1 , mat1_row0_1 , mat1_row0_1 , mat1_row0_1 , mat1_row0_1 ,vec0_batch_1);

    }

    //Remainder loop for cols1
    c_itr <<= 4;
    while(c_itr < cols1)
    {
        AE_L8X8_IP(mat1_row0_0, p_mat1_0, 8);

        AE_L8X8_IP(vec0_batch_0, (ae_int8x8 *)p_vec_0, 8);
        vec0_batch_0 = AE_SEL8X8(vec0_batch_0, neg_vec_bias, sel1);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row0_vec1 , mat1_row0_0 , mat1_row0_0 , mat1_row0_0 , mat1_row0_0 ,vec0_batch_0);
        c_itr += 8;
        sel1 = sel2;
    }

    *out_0_0 = acc_row0_vec0;
    *out_1_0 = acc_row0_vec1;
}

static inline void _xa_nn_dot_product_4_rows_4_vecs_offset_aligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_0_1
    ,ae_int32x2* out_0_2
    ,ae_int32x2* out_0_3
    ,ae_int32x2* out_1_0
    ,ae_int32x2* out_1_1
    ,ae_int32x2* out_1_2
    ,ae_int32x2* out_1_3
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols
    ,WORD32      row_offset
    ,WORD32      vec_offset 
    ,WORD32      mat1_zero_bias
    )
{
  int pre_loop_count, loop_count, post_loop_count;
  int c_itr;

  ae_int8x8 neg_mat_bias = AE_MOVDA8((UWORD8)mat1_zero_bias);
  
  ae_int8x8 vec0_batch_0, vec0_batch_1; 
  ae_int8x8 vec1_batch_0, vec1_batch_1; 
  ae_int8x8 vec2_batch_0, vec2_batch_1; 
  ae_int8x8 vec3_batch_0, vec3_batch_1; 

  ae_int8x8 mat1_row0_0, mat1_row0_1;
  ae_int8x8 mat1_row1_0, mat1_row1_1;
  ae_int8x8 mat1_row2_0, mat1_row2_1;
  ae_int8x8 mat1_row3_0, mat1_row3_1;

  int align_offset = ((unsigned int)p_mat1_0 & 0x7);
  pre_loop_count = 8 - align_offset;
  ae_int8x8 pre_sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(pre_loop_sel_pattern[2 * (align_offset & 7)], pre_loop_sel_pattern[2 * (align_offset & 7) + 1])); 
  p_mat1_0 = (ae_int8x8 *)((ae_int8 *)p_mat1_0 - align_offset);
  //TODO: possible out of bound access
  p_vec_0 -= align_offset;

  pre_loop_count += 8; // 16 values loaded in preloop step
  loop_count = (cols < pre_loop_count)?0:(cols - pre_loop_count);
  post_loop_count = loop_count?(loop_count & 15):((cols + align_offset) & 15);
  loop_count >>= 4;
  int mask_start_end = ((cols + align_offset) < 16)?0:1;

  int rem_g8 = (post_loop_count > 8)?1:0;
  ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (post_loop_count & 7) * !rem_g8], post_loop_sel_pattern[2 * (post_loop_count & 7) * !rem_g8 + 1])); 
  ae_int8x8 sel2 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (post_loop_count & 7) * rem_g8], post_loop_sel_pattern[2 * (post_loop_count & 7) * rem_g8 + 1])); 

  ae_int8x8* p_mat1_1 = p_mat1_0 + row_offset; //next 8th row 
  ae_int8x8* p_mat1_2 = p_mat1_1 + row_offset; //next 8th row
  ae_int8x8* p_mat1_3 = p_mat1_2 + row_offset; //next 8th row 

  ae_int8* p_vec_1 = p_vec_0 + vec_offset; 
  ae_int8* p_vec_2 = p_vec_1 + vec_offset;
  ae_int8* p_vec_3 = p_vec_2 + vec_offset;

  ae_valignx2 alignx2_p_vec_0 = AE_LA128_PP(p_vec_0);
  ae_valignx2 alignx2_p_vec_1 = AE_LA128_PP(p_vec_1);
  ae_valignx2 alignx2_p_vec_2 = AE_LA128_PP(p_vec_2);
  ae_valignx2 alignx2_p_vec_3 = AE_LA128_PP(p_vec_3);

  ae_int32x2 acc_row0_vec0 = *out_0_0;
  ae_int32x2 acc_row0_vec1 = *out_0_1;
  ae_int32x2 acc_row0_vec2 = *out_0_2;
  ae_int32x2 acc_row0_vec3 = *out_0_3;
                       
  ae_int32x2 acc_row1_vec0 = *out_1_0;
  ae_int32x2 acc_row1_vec1 = *out_1_1;
  ae_int32x2 acc_row1_vec2 = *out_1_2;
  ae_int32x2 acc_row1_vec3 = *out_1_3;

  /* Pre loop computation */
  AE_L8X8_IP(mat1_row0_0, p_mat1_0, 8);
  AE_L8X8_IP(mat1_row0_1, p_mat1_0, 8);
  AE_L8X8_IP(mat1_row1_0, p_mat1_1, 8);
  AE_L8X8_IP(mat1_row1_1, p_mat1_1, 8);
  AE_L8X8_IP(mat1_row2_0, p_mat1_2, 8);
  AE_L8X8_IP(mat1_row2_1, p_mat1_2, 8);
  AE_L8X8_IP(mat1_row3_0, p_mat1_3, 8);
  AE_L8X8_IP(mat1_row3_1, p_mat1_3, 8);

  AE_LA8X8X2_IP(vec0_batch_0, vec0_batch_1, alignx2_p_vec_0, (ae_int8x16 *)p_vec_0);
  AE_LA8X8X2_IP(vec1_batch_0, vec1_batch_1, alignx2_p_vec_1, (ae_int8x16 *)p_vec_1);
  AE_LA8X8X2_IP(vec2_batch_0, vec2_batch_1, alignx2_p_vec_2, (ae_int8x16 *)p_vec_2);
  AE_LA8X8X2_IP(vec3_batch_0, vec3_batch_1, alignx2_p_vec_3, (ae_int8x16 *)p_vec_3);

  if(align_offset)
  {
    mat1_row0_0 = AE_SEL8X8(mat1_row0_0, neg_mat_bias, pre_sel1);
    mat1_row1_0 = AE_SEL8X8(mat1_row1_0, neg_mat_bias, pre_sel1);
    mat1_row2_0 = AE_SEL8X8(mat1_row2_0, neg_mat_bias, pre_sel1);
    mat1_row3_0 = AE_SEL8X8(mat1_row3_0, neg_mat_bias, pre_sel1);
  }

  if(mask_start_end)
  {
      AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
      AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
      AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
      AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

      AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
      AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
      AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
      AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);
  }

  for(c_itr = 0; c_itr < loop_count; c_itr++)
  {
    AE_L8X8_IP(mat1_row0_0, p_mat1_0, 8);
    AE_L8X8_IP(mat1_row0_1, p_mat1_0, 8);
    AE_L8X8_IP(mat1_row1_0, p_mat1_1, 8);
    AE_L8X8_IP(mat1_row1_1, p_mat1_1, 8);
    AE_L8X8_IP(mat1_row2_0, p_mat1_2, 8);
    AE_L8X8_IP(mat1_row2_1, p_mat1_2, 8);
    AE_L8X8_IP(mat1_row3_0, p_mat1_3, 8);
    AE_L8X8_IP(mat1_row3_1, p_mat1_3, 8);

    AE_LA8X8X2_IP(vec0_batch_0, vec0_batch_1, alignx2_p_vec_0, (ae_int8x16 *)p_vec_0);
    AE_LA8X8X2_IP(vec1_batch_0, vec1_batch_1, alignx2_p_vec_1, (ae_int8x16 *)p_vec_1);
    AE_LA8X8X2_IP(vec2_batch_0, vec2_batch_1, alignx2_p_vec_2, (ae_int8x16 *)p_vec_2);
    AE_LA8X8X2_IP(vec3_batch_0, vec3_batch_1, alignx2_p_vec_3, (ae_int8x16 *)p_vec_3);

    AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
    AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
    AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
    AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

    AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
    AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
    AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
    AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);
  }

  //Remainder loop for cols
  c_itr = 0;
  ae_valign align_p_vec_0 = AE_LA64_PP(p_vec_0);
  ae_valign align_p_vec_1 = AE_LA64_PP(p_vec_1);
  ae_valign align_p_vec_2 = AE_LA64_PP(p_vec_2);
  ae_valign align_p_vec_3 = AE_LA64_PP(p_vec_3);

  while(c_itr < post_loop_count)
  {
    if(mask_start_end)
    {
        AE_L8X8_IP(mat1_row0_0, p_mat1_0, 8);
        AE_L8X8_IP(mat1_row1_0, p_mat1_1, 8);
        AE_L8X8_IP(mat1_row2_0, p_mat1_2, 8);
        AE_L8X8_IP(mat1_row3_0, p_mat1_3, 8);

        AE_LA8X8_IP(vec0_batch_0, align_p_vec_0 ,(ae_int8x8*)p_vec_0);
        AE_LA8X8_IP(vec1_batch_0, align_p_vec_1 ,(ae_int8x8*)p_vec_1);
        AE_LA8X8_IP(vec2_batch_0, align_p_vec_2 ,(ae_int8x8*)p_vec_2);
        AE_LA8X8_IP(vec3_batch_0, align_p_vec_3 ,(ae_int8x8*)p_vec_3);
    }

    mat1_row0_0 = AE_SEL8X8(mat1_row0_0, neg_mat_bias, sel1);
    mat1_row1_0 = AE_SEL8X8(mat1_row1_0, neg_mat_bias, sel1);
    mat1_row2_0 = AE_SEL8X8(mat1_row2_0, neg_mat_bias, sel1);
    mat1_row3_0 = AE_SEL8X8(mat1_row3_0, neg_mat_bias, sel1);
    AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
    AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
    AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
    AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

    c_itr += 8;
    sel1 = sel2;
    if(!mask_start_end && (c_itr < post_loop_count))
    {
        mat1_row0_0 = mat1_row0_1;
        mat1_row1_0 = mat1_row1_1;
        mat1_row2_0 = mat1_row2_1;
        mat1_row3_0 = mat1_row3_1;
        vec0_batch_0 = vec0_batch_1;
        vec1_batch_0 = vec1_batch_1;
        vec2_batch_0 = vec2_batch_1;
        vec3_batch_0 = vec3_batch_1;
    }
  }

  *out_0_0 = acc_row0_vec0;
  *out_0_1 = acc_row0_vec1;
  *out_0_2 = acc_row0_vec2;
  *out_0_3 = acc_row0_vec3;
       
  *out_1_0 = acc_row1_vec0;
  *out_1_1 = acc_row1_vec1;
  *out_1_2 = acc_row1_vec2;
  *out_1_3 = acc_row1_vec3;
}

static inline void _xa_nn_dot_product_4_rows_1_vecs_offset_aligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_1_0
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols
    ,WORD32      row_offset
    ,WORD32      vec1_zero_bias
    )
{
  int c_itr = 0;

  ae_int8x8 neg_vec_bias = AE_MOVDA8((UWORD8)vec1_zero_bias);
  ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (cols & 7)], post_loop_sel_pattern[2 * (cols & 7) + 1]));
  
  ae_int8x8 mat1_row0_0;
  ae_int8x8 mat1_row1_0;
  ae_int8x8 mat1_row2_0;
  ae_int8x8 mat1_row3_0;
  ae_int8x8 vec0_batch_0; 
  ae_int8x8 align_p_vec0;

  ae_int8x8* p_mat1_1 = p_mat1_0 + row_offset; //next 8th row 
  ae_int8x8* p_mat1_2 = p_mat1_1 + row_offset; //next 8th row
  ae_int8x8* p_mat1_3 = p_mat1_2 + row_offset; //next 8th row 

  ae_valign align_p_mat1_0 = AE_LA64_PP(p_mat1_0);
  ae_valign align_p_mat1_1 = AE_LA64_PP(p_mat1_1);
  ae_valign align_p_mat1_2 = AE_LA64_PP(p_mat1_2);
  ae_valign align_p_mat1_3 = AE_LA64_PP(p_mat1_3);

  ae_int32x2 acc_row0_vec0 = *out_0_0;
  ae_int32x2 acc_row1_vec0 = *out_1_0;

  AE_SW_PRIME_64(p_vec_0, align_p_vec0);

  int cols_count=cols-(cols&7);
  for(c_itr = 0; c_itr < cols_count>>3; c_itr++)
  {
    AE_LA8X8_IP(mat1_row0_0, align_p_mat1_0, p_mat1_0);
    AE_LA8X8_IP(mat1_row1_0, align_p_mat1_1, p_mat1_1);
    AE_LA8X8_IP(mat1_row2_0, align_p_mat1_2, p_mat1_2);
    AE_LA8X8_IP(mat1_row3_0, align_p_mat1_3, p_mat1_3);
    
    AE_SW_LA8X8_IP(vec0_batch_0, align_p_vec0, p_vec_0);

    AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
  }

  //Remainder loop for cols
  if(cols_count!=cols)
  {
    AE_LA8X8_IP(mat1_row0_0, align_p_mat1_0, p_mat1_0);
    AE_LA8X8_IP(mat1_row1_0, align_p_mat1_1, p_mat1_1);
    AE_LA8X8_IP(mat1_row2_0, align_p_mat1_2, p_mat1_2);
    AE_LA8X8_IP(mat1_row3_0, align_p_mat1_3, p_mat1_3);

    AE_SW_LA8X8_IP(vec0_batch_0, align_p_vec0, p_vec_0);
    vec0_batch_0 = AE_SEL8X8(vec0_batch_0, neg_vec_bias, sel1);

    AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
  }

  *out_0_0 = acc_row0_vec0;
  *out_1_0 = acc_row1_vec0;
}

static inline void _xa_nn_dot_product_4_rows_4_vecs_unaligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_0_1
    ,ae_int32x2* out_0_2
    ,ae_int32x2* out_0_3
    ,ae_int32x2* out_1_0
    ,ae_int32x2* out_1_1
    ,ae_int32x2* out_1_2
    ,ae_int32x2* out_1_3
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols
    ,WORD32      row_offset
    ,WORD32      vec_offset 
    ,WORD32      mat1_zero_bias
    )
{
    int c_itr = 0;

    ae_int8x8 neg_mat_bias = AE_MOVDA8((UWORD8)mat1_zero_bias);
    int rem_cols = cols & 15;
    int rem_g8 = ((rem_cols & 15) > 8)?1:0;
    ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8 + 1])); \
    ae_int8x8 sel2 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8 + 1])); \
  
    ae_int8x8 mat1_row0_0, mat1_row0_1;
    ae_int8x8 mat1_row1_0, mat1_row1_1;
    ae_int8x8 mat1_row2_0, mat1_row2_1;
    ae_int8x8 mat1_row3_0, mat1_row3_1;

    ae_int8x8 vec0_batch_0, vec0_batch_1; 
    ae_int8x8 vec1_batch_0, vec1_batch_1; 
    ae_int8x8 vec2_batch_0, vec2_batch_1; 
    ae_int8x8 vec3_batch_0, vec3_batch_1; 
    ae_int8x8 align_p_mat1_0, align_p_mat1_1, align_p_mat1_2, align_p_mat1_3; 

    ae_int8x8* p_mat1_1 = (ae_int8x8*)((ae_int8*)p_mat1_0 + row_offset); 
    ae_int8x8* p_mat1_2 = (ae_int8x8*)((ae_int8*)p_mat1_1 + row_offset);
    ae_int8x8* p_mat1_3 = (ae_int8x8*)((ae_int8*)p_mat1_2 + row_offset);

    ae_int8* p_vec_1 = p_vec_0 + vec_offset; 
    ae_int8* p_vec_2 = p_vec_1 + vec_offset;
    ae_int8* p_vec_3 = p_vec_2 + vec_offset;

    ae_valign align_p_vec_0 = AE_LA64_PP(p_vec_0);
    ae_valign align_p_vec_1 = AE_LA64_PP(p_vec_1);
    ae_valign align_p_vec_2 = AE_LA64_PP(p_vec_2);
    ae_valign align_p_vec_3 = AE_LA64_PP(p_vec_3);

    ae_int32x2 acc_row0_vec0 = *out_0_0;
    ae_int32x2 acc_row0_vec1 = *out_0_1;
    ae_int32x2 acc_row0_vec2 = *out_0_2;
    ae_int32x2 acc_row0_vec3 = *out_0_3;
                       
    ae_int32x2 acc_row1_vec0 = *out_1_0;
    ae_int32x2 acc_row1_vec1 = *out_1_1;
    ae_int32x2 acc_row1_vec2 = *out_1_2;
    ae_int32x2 acc_row1_vec3 = *out_1_3;

    AE_SW_PRIME_64(p_mat1_0, align_p_mat1_0);
    AE_SW_PRIME_64(p_mat1_1, align_p_mat1_1);
    AE_SW_PRIME_64(p_mat1_2, align_p_mat1_2);
    AE_SW_PRIME_64(p_mat1_3, align_p_mat1_3);

    int cols_count = cols -(cols & 15);
#pragma no_unroll
    for(c_itr = 0; c_itr < cols_count>>4; c_itr++)
    {
        AE_LA8X8_IP(vec0_batch_0, align_p_vec_0, (ae_int8x8 *)p_vec_0);
        AE_LA8X8_IP(vec0_batch_1, align_p_vec_0, (ae_int8x8 *)p_vec_0);
        AE_LA8X8_IP(vec1_batch_0, align_p_vec_1, (ae_int8x8 *)p_vec_1);
        AE_LA8X8_IP(vec1_batch_1, align_p_vec_1, (ae_int8x8 *)p_vec_1);
        AE_LA8X8_IP(vec2_batch_0, align_p_vec_2, (ae_int8x8 *)p_vec_2);
        AE_LA8X8_IP(vec2_batch_1, align_p_vec_2, (ae_int8x8 *)p_vec_2);
        AE_LA8X8_IP(vec3_batch_0, align_p_vec_3, (ae_int8x8 *)p_vec_3);
        AE_LA8X8_IP(vec3_batch_1, align_p_vec_3, (ae_int8x8 *)p_vec_3);

        AE_SW_LA8X8_IP(mat1_row0_0, align_p_mat1_0, p_mat1_0);
        AE_SW_LA8X8_IP(mat1_row0_1, align_p_mat1_0, p_mat1_0);
        AE_SW_LA8X8_IP(mat1_row1_0, align_p_mat1_1, p_mat1_1);
        AE_SW_LA8X8_IP(mat1_row1_1, align_p_mat1_1, p_mat1_1);
        AE_SW_LA8X8_IP(mat1_row2_0, align_p_mat1_2, p_mat1_2);
        AE_SW_LA8X8_IP(mat1_row2_1, align_p_mat1_2, p_mat1_2);
        AE_SW_LA8X8_IP(mat1_row3_0, align_p_mat1_3, p_mat1_3);
        AE_SW_LA8X8_IP(mat1_row3_1, align_p_mat1_3, p_mat1_3);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);
    }  

    //Remainder loop for cols
    c_itr <<= 4;
    while(c_itr < cols)
    {
        AE_SW_LA8X8_IP(mat1_row0_0, align_p_mat1_0, p_mat1_0);
        AE_SW_LA8X8_IP(mat1_row1_0, align_p_mat1_1, p_mat1_1);
        AE_SW_LA8X8_IP(mat1_row2_0, align_p_mat1_2, p_mat1_2);
        AE_SW_LA8X8_IP(mat1_row3_0, align_p_mat1_3, p_mat1_3);

        AE_LA8X8_IP(vec0_batch_0, align_p_vec_0, (ae_int8x8 *)p_vec_0);
        AE_LA8X8_IP(vec1_batch_0, align_p_vec_1, (ae_int8x8 *)p_vec_1);
        AE_LA8X8_IP(vec2_batch_0, align_p_vec_2, (ae_int8x8 *)p_vec_2);
        AE_LA8X8_IP(vec3_batch_0, align_p_vec_3, (ae_int8x8 *)p_vec_3);

        mat1_row0_0 = AE_SEL8X8(mat1_row0_0, neg_mat_bias, sel1);
        mat1_row1_0 = AE_SEL8X8(mat1_row1_0, neg_mat_bias, sel1);
        mat1_row2_0 = AE_SEL8X8(mat1_row2_0, neg_mat_bias, sel1);
        mat1_row3_0 = AE_SEL8X8(mat1_row3_0, neg_mat_bias, sel1);
    
        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);
        c_itr += 8;
        sel1 = sel2;
    }

    *out_0_0 = acc_row0_vec0;
    *out_0_1 = acc_row0_vec1;
    *out_0_2 = acc_row0_vec2;
    *out_0_3 = acc_row0_vec3;

    *out_1_0 = acc_row1_vec0;
    *out_1_1 = acc_row1_vec1;
    *out_1_2 = acc_row1_vec2;
    *out_1_3 = acc_row1_vec3;
}

static inline void _xa_nn_dot_product_4_rows_1_vecs_unaligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_1_0
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols
    ,WORD32      row_offset
    ,WORD32      vec1_zero_bias
    )
{
  int c_itr = 0;

  ae_int8x8 neg_vec_bias = AE_MOVDA8((UWORD8)vec1_zero_bias);
  ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (cols & 7)], post_loop_sel_pattern[2 * (cols & 7) + 1]));
  
  ae_int8x8 mat1_row0_0;
  ae_int8x8 mat1_row1_0;
  ae_int8x8 mat1_row2_0;
  ae_int8x8 mat1_row3_0;
  ae_int8x8 vec0_batch_0; 
  ae_int8x8 align_p_vec0;

  ae_int8x8 *p_mat1_1 = (ae_int8x8*)((WORD8 *)p_mat1_0 + row_offset); 
  ae_int8x8 *p_mat1_2 = (ae_int8x8*)((WORD8 *)p_mat1_1 + row_offset); 
  ae_int8x8 *p_mat1_3 = (ae_int8x8*)((WORD8 *)p_mat1_2 + row_offset); 

  ae_valign align_p_mat1_0 = AE_LA64_PP(p_mat1_0);
  ae_valign align_p_mat1_1 = AE_LA64_PP(p_mat1_1);
  ae_valign align_p_mat1_2 = AE_LA64_PP(p_mat1_2);
  ae_valign align_p_mat1_3 = AE_LA64_PP(p_mat1_3);

  ae_int32x2 acc_row0_vec0 = *out_0_0;
  ae_int32x2 acc_row1_vec0 = *out_1_0;

  AE_SW_PRIME_64(p_vec_0, align_p_vec0);

  int cols_count=cols-(cols&7);
  for(c_itr = 0; c_itr < cols_count>>3; c_itr++)
  {
    AE_LA8X8_IP(mat1_row0_0, align_p_mat1_0, p_mat1_0);
    AE_LA8X8_IP(mat1_row1_0, align_p_mat1_1, p_mat1_1);
    AE_LA8X8_IP(mat1_row2_0, align_p_mat1_2, p_mat1_2);
    AE_LA8X8_IP(mat1_row3_0, align_p_mat1_3, p_mat1_3);
    
    AE_SW_LA8X8_IP(vec0_batch_0, align_p_vec0, p_vec_0);

    AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
  }

  //Remainder loop for cols
  if(cols_count!=cols)
  {
    AE_LA8X8_IP(mat1_row0_0, align_p_mat1_0, p_mat1_0);
    AE_LA8X8_IP(mat1_row1_0, align_p_mat1_1, p_mat1_1);
    AE_LA8X8_IP(mat1_row2_0, align_p_mat1_2, p_mat1_2);
    AE_LA8X8_IP(mat1_row3_0, align_p_mat1_3, p_mat1_3);

    AE_SW_LA8X8_IP(vec0_batch_0, align_p_vec0, p_vec_0);
    vec0_batch_0 = AE_SEL8X8(vec0_batch_0, neg_vec_bias, sel1);

    AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
  }

  *out_0_0 = acc_row0_vec0;
  *out_1_0 = acc_row1_vec0;
}

static inline void _xa_nn_dot_product_1_rows_1_vecs_unaligned
    (ae_int32x2* out_0_0
    ,ae_int32x2* out_1_0
    ,ae_int8x8*  p_mat1_0
    ,ae_int8*    p_vec_0
    ,WORD32      cols1
    ,WORD32      vec1_zero_bias
    )
{
    int c_itr = 0;
    ae_int8x8 vec0_batch_0, vec0_batch_1; 
    ae_int8x8 mat1_row0_0, mat1_row0_1;

    ae_int32x2 acc_row0_vec0 = *out_0_0;
    ae_int32x2 acc_row0_vec1 = *out_1_0;

    ae_valignx2 align_p_mat1_0 = AE_LA128_PP(p_mat1_0);
    ae_valignx2 align_p_vec_0 = AE_LA128_PP(p_vec_0);

    int rem_cols = (cols1 & 15);
    int rem_g8 = (rem_cols > 8)?1:0;

    ae_int8x8 sel1 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * !rem_g8 + 1])); 
    ae_int8x8 sel2 = AE_MOVINT8X8_FROMINT32X2(AE_MOVDA32X2(post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8], post_loop_sel_pattern[2 * (rem_cols & 7) * rem_g8 + 1])); 

    ae_int8x8 neg_vec_bias = AE_MOVDA8((UWORD8)vec1_zero_bias);
    int cols_count = cols1 - (cols1 & 15);

    for(c_itr = 0; c_itr < cols_count >> 4; c_itr++)
    {
        AE_LA8X8X2_IP(mat1_row0_0, mat1_row0_1, align_p_mat1_0, (ae_int8x16 *)p_mat1_0);

        AE_LA8X8X2_IP(vec0_batch_0, vec0_batch_1, align_p_vec_0, (ae_int8x16 *)p_vec_0);

        AE_MULAUUZB8Q8X8(acc_row0_vec0, acc_row0_vec1, mat1_row0_0, mat1_row0_0, mat1_row0_0, mat1_row0_0, vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec0, acc_row0_vec1, mat1_row0_1, mat1_row0_1, mat1_row0_1, mat1_row0_1, vec0_batch_1);
    }

    //Remainder loop for cols1
    if(cols_count!=cols1)
    {
        AE_LA8X8X2_IP(mat1_row0_0, mat1_row0_1, align_p_mat1_0, (ae_int8x16 *)p_mat1_0);

        AE_LA8X8X2_IP(vec0_batch_0, vec0_batch_1, align_p_vec_0, (ae_int8x16 *)p_vec_0);

        vec0_batch_0 = AE_SEL8X8(vec0_batch_0, neg_vec_bias, sel1); 

        AE_MULAUUZB8Q8X8(acc_row0_vec0, acc_row0_vec1, mat1_row0_0, mat1_row0_0, mat1_row0_0, mat1_row0_0, vec0_batch_0);
    
        if(rem_g8)
        {
            vec0_batch_1 = AE_SEL8X8(vec0_batch_1, neg_vec_bias, sel2); 
            AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row0_vec1 , mat1_row0_1 , mat1_row0_1 , mat1_row0_1 , mat1_row0_1 ,vec0_batch_1);
        }
    }

    *out_0_0 = acc_row0_vec0;
    *out_1_0 = acc_row0_vec1;
}

WORD32 xa_nn_matmul_asym8xasym8_asym8(
    UWORD8 * __restrict__ p_out,
    const UWORD8 * __restrict__ p_mat1,
    const UWORD8 * __restrict__ p_vec1,
    const WORD32 * __restrict__ p_bias,
    WORD32 rows,
    WORD32 cols1,
    WORD32 row_stride1,
    WORD32 vec_count,
    WORD32 vec_offset,
    WORD32 out_offset,
    WORD32 out_stride,                      
    WORD32 mat1_zero_bias,
    WORD32 vec1_zero_bias,
    WORD32 out_multiplier,
    WORD32 out_shift,
    WORD32 out_zero_bias)
{
  /* NULL pointer checks */
  XA_NNLIB_ARG_CHK_PTR(p_out, -1);
  XA_NNLIB_ARG_CHK_PTR(p_mat1, -1);
  XA_NNLIB_ARG_CHK_PTR(p_vec1, -1);
  /* Pointer alignment checks */
  XA_NNLIB_ARG_CHK_ALIGN(p_bias, sizeof(WORD32), -1);
  /* Basic Parameter checks */
  XA_NNLIB_ARG_CHK_COND((rows <= 0), -1);
  XA_NNLIB_ARG_CHK_COND((cols1 <= 0), -1);
  XA_NNLIB_ARG_CHK_COND((row_stride1 < cols1), -1);
  XA_NNLIB_ARG_CHK_COND((vec_offset == 0), -1);
  XA_NNLIB_ARG_CHK_COND((out_offset == 0), -1);
  XA_NNLIB_ARG_CHK_COND((out_stride == 0), -1);
  XA_NNLIB_ARG_CHK_COND((mat1_zero_bias < -255 || mat1_zero_bias > 0), -1);
  XA_NNLIB_ARG_CHK_COND((vec1_zero_bias < -255 || vec1_zero_bias > 0), -1);
  XA_NNLIB_ARG_CHK_COND((out_shift < -31 || out_shift > 31), -1);
  XA_NNLIB_ARG_CHK_COND((out_zero_bias < 0 || out_zero_bias > 255), -1);

  ae_int32x2 bias_buffer[4];

  /* Iterators used in for loops */
  int m_itr, vec_itr;
  int ii;

  /* Shifts to match with Tensorflow */
  int left_shift, right_shift;

  left_shift = out_shift < 0 ? 0 : out_shift;
  right_shift = out_shift > 0 ? 0 : -out_shift;

  /*Load AE_BIASV8 and AE_BIASC8 state registers with mat1 and vec1 zero bias values*/
  ae_int64 biasvc1 = AE_MOVINT64_FROMINT32X2(AE_MOVDA32X2(-vec1_zero_bias, -mat1_zero_bias));
  ae_int64 biascv1 = AE_MOVINT64_FROMINT32X2(AE_MOVDA32X2(-mat1_zero_bias, -vec1_zero_bias));

  ae_int32x2 min_uint8 = AE_MOVDA32(0);
  ae_int32x2 l_mult = AE_MOVDA32(1 << left_shift);

  /* Assign initial value so this value will be used in trailing loop */
  m_itr = 0;
  vec_itr = 0;

  /* Special case for cols == 8 */
  if(
      (cols1 == 8) &&
      (row_stride1 == 8) &&
      (vec_offset == 8) &&
      ALIGNED_PTR(p_mat1, 16) &&
      ALIGNED_PTR(p_vec1, 16) &&
      ALIGNED_PTR(p_out, 4) &&
      (out_stride == 1) &&   // NHWC case
      ((out_offset & 0x3) == 0) &&   // NHWC case
      ((rows & 0x3) == 0) &&
      ((vec_count & 0x3) == 0)
    )
  {
    ae_int8x8 mat1_row0_0;
    ae_int8x8 mat1_row1_0;
    ae_int8x8 mat1_row2_0;
    ae_int8x8 mat1_row3_0;
    ae_int32x4 *pt_bias;
    ae_valignx2 bias_a;
    ae_int32x2 d_bias0, d_bias1;

    ae_int32x2 l_mult = AE_MOVDA32(1 << left_shift);

    AE_MOVZBVCDR(biasvc1);

    pt_bias = (ae_int32x4 *)p_bias;
    bias_a = AE_LA128_PP(pt_bias);

    for(m_itr = 0; m_itr < (rows & ~(4 - 1)); m_itr += 4)
    {
      /* Load bias values */
      AE_LA32X2X2_IP(d_bias0, d_bias1, bias_a, pt_bias);
      AE_S32X2X2_I(d_bias0, d_bias0, (ae_int32x4*)bias_buffer, 0);
      AE_S32X2X2_I(d_bias1, d_bias1, (ae_int32x4*)bias_buffer, 16);

      WORD8* p_dst_0 = (WORD8*)p_out + (m_itr + 0) * out_stride;
      ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1]; 
      ae_int8* p_vec_0  = (ae_int8 *)(p_vec1);

      /* Load 4 rows */
      AE_L8X8X2_IP(mat1_row0_0, mat1_row1_0, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row2_0, mat1_row3_0, (ae_int8x16*)p_mat1_0, 16);

      for (vec_itr = 0; vec_itr < (vec_count & ~(4 - 1)); vec_itr += 4)
      {
        ae_int32x2 acc_row0_vec0;
        ae_int32x2 acc_row1_vec0;
        ae_int32x2 acc_row0_vec1;
        ae_int32x2 acc_row1_vec1;
        ae_int32x2 acc_row0_vec2;
        ae_int32x2 acc_row1_vec2;
        ae_int32x2 acc_row0_vec3;
        ae_int32x2 acc_row1_vec3;

        ae_int8x8 vec0_batch_0; 
        ae_int8x8 vec1_batch_0; 
        ae_int8x8 vec2_batch_0; 
        ae_int8x8 vec3_batch_0; 

        /* Load  4 vectors  */
        AE_L8X8X2_IP(vec0_batch_0, vec1_batch_0, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec2_batch_0, vec3_batch_0, (ae_int8x16*)p_vec_0, 16);

        /* Initialize accumulators with bias */
        AE_L32X2X2_I(acc_row0_vec0, acc_row0_vec1, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row0_vec2, acc_row0_vec3, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row1_vec0, acc_row1_vec1, (ae_int32x4*)bias_buffer, 16);
        AE_L32X2X2_I(acc_row1_vec2, acc_row1_vec3, (ae_int32x4*)bias_buffer, 16);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

        /* Apply quantization */
        ae_int16x4 out_0, out_1, out_2, out_3;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_multiplier, l_mult, right_shift, out_zero_bias);

        /* Store output */
        ae_int8x8 out32_0, out32_1; 
        PACK_32X2(out32_0, out_0, out_1);
        PACK_32X2(out32_1, out_2, out_3);

        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
      }
      /*
         for (; vec_itr < vec_count; vec_itr++)
         {
         }
       */
    }
    return 0;
  } 

  /* Special case for cols == 16 */
  if(
      (cols1 == 16) &&
      (row_stride1 == 16) &&
      (vec_offset == 16) &&
      ALIGNED_PTR(p_mat1, 16) &&
      ALIGNED_PTR(p_vec1, 16) &&
      ALIGNED_PTR(p_out, 4) &&
      (out_stride == 1) &&   // NHWC case
      ((out_offset & 0x3) == 0) &&   // NHWC case
      ((rows & 0x3) == 0) &&
      ((vec_count & 0x3) == 0)
    )
  {
    ae_int8x8 mat1_row0_0, mat1_row0_1;
    ae_int8x8 mat1_row1_0, mat1_row1_1;
    ae_int8x8 mat1_row2_0, mat1_row2_1;
    ae_int8x8 mat1_row3_0, mat1_row3_1;

    ae_int32x4 *pt_bias;
    ae_valignx2 bias_a;
    ae_int32x2 d_bias0, d_bias1;

    ae_int32x2 l_mult = AE_MOVDA32(1 << left_shift);

    AE_MOVZBVCDR(biasvc1);

    pt_bias = (ae_int32x4 *)p_bias;
    bias_a = AE_LA128_PP(pt_bias);

    for(m_itr = 0; m_itr < (rows & ~(4 - 1)); m_itr += 4)
    {
      /* Load bias values */
      AE_LA32X2X2_IP(d_bias0, d_bias1, bias_a, pt_bias);
      AE_S32X2X2_I(d_bias0, d_bias0, (ae_int32x4*)bias_buffer, 0);
      AE_S32X2X2_I(d_bias1, d_bias1, (ae_int32x4*)bias_buffer, 16);

      WORD8* p_dst_0 = (WORD8*)p_out + (m_itr + 0) * out_stride;

      /* Load 4 rows */
      ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1]; 

      AE_L8X8X2_IP(mat1_row0_0, mat1_row0_1, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row1_0, mat1_row1_1, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row2_0, mat1_row2_1, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row3_0, mat1_row3_1, (ae_int8x16*)p_mat1_0, 16);

      ae_int8* p_vec_0  = (ae_int8 *)(p_vec1);

      for (vec_itr = 0; vec_itr < (vec_count & ~(4 - 1)); vec_itr += 4)
      {
        ae_int32x2 acc_row0_vec0;
        ae_int32x2 acc_row1_vec0;
        ae_int32x2 acc_row0_vec1;
        ae_int32x2 acc_row1_vec1;
        ae_int32x2 acc_row0_vec2;
        ae_int32x2 acc_row1_vec2;
        ae_int32x2 acc_row0_vec3;
        ae_int32x2 acc_row1_vec3;

        ae_int8x8 vec0_batch_0, vec0_batch_1; 
        ae_int8x8 vec1_batch_0, vec1_batch_1; 
        ae_int8x8 vec2_batch_0, vec2_batch_1; 
        ae_int8x8 vec3_batch_0, vec3_batch_1; 

        /* Load  4 vectors  */
        AE_L8X8X2_IP(vec0_batch_0, vec0_batch_1, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec1_batch_0, vec1_batch_1, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec2_batch_0, vec2_batch_1, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec3_batch_0, vec3_batch_1, (ae_int8x16*)p_vec_0, 16);

        /* Initialize accumulators with bias */
        AE_L32X2X2_I(acc_row0_vec0, acc_row0_vec1, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row0_vec2, acc_row0_vec3, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row1_vec0, acc_row1_vec1, (ae_int32x4*)bias_buffer, 16);
        AE_L32X2X2_I(acc_row1_vec2, acc_row1_vec3, (ae_int32x4*)bias_buffer, 16);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);

        /* Apply quantization */
        ae_int16x4 out_0, out_1, out_2, out_3;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_multiplier, l_mult, right_shift, out_zero_bias);

        /* Store output */
        ae_int8x8 out32_0, out32_1; 
        PACK_32X2(out32_0, out_0, out_1);
        PACK_32X2(out32_1, out_2, out_3);

        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
      }
      /*
         for (; vec_itr < vec_count; vec_itr++)
         {
         }
       */
    }
    return 0;
  } 

  /* Special case for cols == 24 */
  if(
      (cols1 == 24) &&
      (row_stride1 == 24) &&
      (vec_offset == 24) &&
      ALIGNED_PTR(p_mat1, 16) &&
      ALIGNED_PTR(p_vec1, 16) &&
      ALIGNED_PTR(p_out, 4) &&
      (out_stride == 1) &&   // NHWC case
      ((out_offset & 0x3) == 0) &&   // NHWC case
      ((rows & 0x3) == 0) &&
      ((vec_count & 0x3) == 0)
    )
  {
    ae_int8x8 mat1_row0_0, mat1_row0_1, mat1_row0_2;
    ae_int8x8 mat1_row1_0, mat1_row1_1, mat1_row1_2;
    ae_int8x8 mat1_row2_0, mat1_row2_1, mat1_row2_2;
    ae_int8x8 mat1_row3_0, mat1_row3_1, mat1_row3_2;

    ae_int32x4 *pt_bias;
    ae_valignx2 bias_a;
    ae_int32x2 d_bias0, d_bias1;

    ae_int32x2 l_mult = AE_MOVDA32(1 << left_shift);

    AE_MOVZBVCDR(biasvc1);

    pt_bias = (ae_int32x4 *)p_bias;
    bias_a = AE_LA128_PP(pt_bias);

    for(m_itr = 0; m_itr < (rows & ~(4 - 1)); m_itr += 4)
    {
      /* Load bias values */
      AE_LA32X2X2_IP(d_bias0, d_bias1, bias_a, pt_bias);
      AE_S32X2X2_I(d_bias0, d_bias0, (ae_int32x4*)bias_buffer, 0);
      AE_S32X2X2_I(d_bias1, d_bias1, (ae_int32x4*)bias_buffer, 16);

      WORD8* p_dst_0 = (WORD8*)p_out + (m_itr + 0) * out_stride;

      /* Load 4 rows */
      ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1]; 

      AE_L8X8_IP(mat1_row0_0, p_mat1_0, 8); AE_L8X8_IP(mat1_row0_1, p_mat1_0, 8); AE_L8X8_IP(mat1_row0_2, p_mat1_0, 8);
      AE_L8X8_IP(mat1_row1_0, p_mat1_0, 8); AE_L8X8_IP(mat1_row1_1, p_mat1_0, 8); AE_L8X8_IP(mat1_row1_2, p_mat1_0, 8);
      AE_L8X8_IP(mat1_row2_0, p_mat1_0, 8); AE_L8X8_IP(mat1_row2_1, p_mat1_0, 8); AE_L8X8_IP(mat1_row2_2, p_mat1_0, 8);
      AE_L8X8_IP(mat1_row3_0, p_mat1_0, 8); AE_L8X8_IP(mat1_row3_1, p_mat1_0, 8); AE_L8X8_IP(mat1_row3_2, p_mat1_0, 8);

      ae_int8* p_vec_0  = (ae_int8 *)(p_vec1);

      for (vec_itr = 0; vec_itr < (vec_count & ~(4 - 1)); vec_itr += 4)
      {
        ae_int32x2 acc_row0_vec0;
        ae_int32x2 acc_row1_vec0;
        ae_int32x2 acc_row0_vec1;
        ae_int32x2 acc_row1_vec1;
        ae_int32x2 acc_row0_vec2;
        ae_int32x2 acc_row1_vec2;
        ae_int32x2 acc_row0_vec3;
        ae_int32x2 acc_row1_vec3;

        ae_int8x8 vec0_batch_0, vec0_batch_1, vec0_batch_2; 
        ae_int8x8 vec1_batch_0, vec1_batch_1, vec1_batch_2; 
        ae_int8x8 vec2_batch_0, vec2_batch_1, vec2_batch_2; 
        ae_int8x8 vec3_batch_0, vec3_batch_1, vec3_batch_2; 

        /* Load  4 vectors  */
        AE_L8X8_IP(vec0_batch_0, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec0_batch_1, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec0_batch_2, (ae_int8x8*)p_vec_0, 8);
        AE_L8X8_IP(vec1_batch_0, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec1_batch_1, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec1_batch_2, (ae_int8x8*)p_vec_0, 8);
        AE_L8X8_IP(vec2_batch_0, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec2_batch_1, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec2_batch_2, (ae_int8x8*)p_vec_0, 8);
        AE_L8X8_IP(vec3_batch_0, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec3_batch_1, (ae_int8x8*)p_vec_0, 8); AE_L8X8_IP(vec3_batch_2, (ae_int8x8*)p_vec_0, 8);

        /* Initialize accumulators with bias */
        AE_L32X2X2_I(acc_row0_vec0, acc_row0_vec1, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row0_vec2, acc_row0_vec3, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row1_vec0, acc_row1_vec1, (ae_int32x4*)bias_buffer, 16);
        AE_L32X2X2_I(acc_row1_vec2, acc_row1_vec3, (ae_int32x4*)bias_buffer, 16);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec0_batch_2);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec1_batch_2);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec2_batch_2);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec3_batch_2);

        /* Apply quantization */
        ae_int16x4 out_0, out_1, out_2, out_3;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_multiplier, l_mult, right_shift, out_zero_bias);

        /* Store output */
        ae_int8x8 out32_0, out32_1; 
        PACK_32X2(out32_0, out_0, out_1);
        PACK_32X2(out32_1, out_2, out_3);

        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
      }
      /*
         for (; vec_itr < vec_count; vec_itr++)
         {
         }
       */
    }
    return 0;
  } 

  /* Special case for cols == 32 */
  if(
      (cols1 == 32) &&
      (row_stride1 == 32) &&
      (vec_offset == 32) &&
      ALIGNED_PTR(p_mat1, 16) &&
      ALIGNED_PTR(p_vec1, 16) &&
      ALIGNED_PTR(p_out, 4) &&
      (out_stride == 1) &&   // NHWC case
      ((out_offset & 0x3) == 0) &&   // NHWC case
      ((rows & 0x3) == 0) &&
      ((vec_count & 0x3) == 0)
    )
  {
    ae_int8x8 mat1_row0_0, mat1_row0_1, mat1_row0_2, mat1_row0_3;
    ae_int8x8 mat1_row1_0, mat1_row1_1, mat1_row1_2, mat1_row1_3;
    ae_int8x8 mat1_row2_0, mat1_row2_1, mat1_row2_2, mat1_row2_3;
    ae_int8x8 mat1_row3_0, mat1_row3_1, mat1_row3_2, mat1_row3_3;

    ae_int32x4 *pt_bias;
    ae_valignx2 bias_a;
    ae_int32x2 d_bias0, d_bias1;

    ae_int32x2 l_mult = AE_MOVDA32(1 << left_shift);

    AE_MOVZBVCDR(biasvc1);

    pt_bias = (ae_int32x4 *)p_bias;
    bias_a = AE_LA128_PP(pt_bias);

    for(m_itr = 0; m_itr < (rows & ~(4 - 1)); m_itr += 4)
    {
      /* Load bias values */
      AE_LA32X2X2_IP(d_bias0, d_bias1, bias_a, pt_bias);
      AE_S32X2X2_I(d_bias0, d_bias0, (ae_int32x4*)bias_buffer, 0);
      AE_S32X2X2_I(d_bias1, d_bias1, (ae_int32x4*)bias_buffer, 16);

      WORD8* p_dst_0 = (WORD8*)p_out + (m_itr + 0) * out_stride;

      /* Load 4 rows */
      ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1]; 

      AE_L8X8X2_IP(mat1_row0_0, mat1_row0_1, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row0_2, mat1_row0_3, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row1_0, mat1_row1_1, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row1_2, mat1_row1_3, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row2_0, mat1_row2_1, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row2_2, mat1_row2_3, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row3_0, mat1_row3_1, (ae_int8x16*)p_mat1_0, 16);
      AE_L8X8X2_IP(mat1_row3_2, mat1_row3_3, (ae_int8x16*)p_mat1_0, 16);

      ae_int8* p_vec_0  = (ae_int8 *)(p_vec1);

      for (vec_itr = 0; vec_itr < (vec_count & ~(4 - 1)); vec_itr += 4)
      {
        ae_int32x2 acc_row0_vec0;
        ae_int32x2 acc_row1_vec0;
        ae_int32x2 acc_row0_vec1;
        ae_int32x2 acc_row1_vec1;
        ae_int32x2 acc_row0_vec2;
        ae_int32x2 acc_row1_vec2;
        ae_int32x2 acc_row0_vec3;
        ae_int32x2 acc_row1_vec3;

        ae_int8x8 vec0_batch_0, vec0_batch_1, vec0_batch_2, vec0_batch_3; 
        ae_int8x8 vec1_batch_0, vec1_batch_1, vec1_batch_2, vec1_batch_3; 
        ae_int8x8 vec2_batch_0, vec2_batch_1, vec2_batch_2, vec2_batch_3; 
        ae_int8x8 vec3_batch_0, vec3_batch_1, vec3_batch_2, vec3_batch_3; 

        /* Load  4 vectors  */
        AE_L8X8X2_IP(vec0_batch_0, vec0_batch_1, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec0_batch_2, vec0_batch_3, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec1_batch_0, vec1_batch_1, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec1_batch_2, vec1_batch_3, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec2_batch_0, vec2_batch_1, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec2_batch_2, vec2_batch_3, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec3_batch_0, vec3_batch_1, (ae_int8x16*)p_vec_0, 16);
        AE_L8X8X2_IP(vec3_batch_2, vec3_batch_3, (ae_int8x16*)p_vec_0, 16);

        /* Initialize accumulators with bias */
        AE_L32X2X2_I(acc_row0_vec0, acc_row0_vec1, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row0_vec2, acc_row0_vec3, (ae_int32x4*)bias_buffer, 0);
        AE_L32X2X2_I(acc_row1_vec0, acc_row1_vec1, (ae_int32x4*)bias_buffer, 16);
        AE_L32X2X2_I(acc_row1_vec2, acc_row1_vec3, (ae_int32x4*)bias_buffer, 16);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec0_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec1_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec2_batch_0);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_0 , mat1_row1_0 , mat1_row2_0 , mat1_row3_0 ,vec3_batch_0);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec0_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec1_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec2_batch_1);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_1 , mat1_row1_1 , mat1_row2_1 , mat1_row3_1 ,vec3_batch_1);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec0_batch_2);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec1_batch_2);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec2_batch_2);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_2 , mat1_row1_2 , mat1_row2_2 , mat1_row3_2 ,vec3_batch_2);

        AE_MULAUUZB8Q8X8(acc_row0_vec0 , acc_row1_vec0 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec0_batch_3);
        AE_MULAUUZB8Q8X8(acc_row0_vec1 , acc_row1_vec1 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec1_batch_3);
        AE_MULAUUZB8Q8X8(acc_row0_vec2 , acc_row1_vec2 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec2_batch_3);
        AE_MULAUUZB8Q8X8(acc_row0_vec3 , acc_row1_vec3 , mat1_row0_3 , mat1_row1_3 , mat1_row2_3 , mat1_row3_3 ,vec3_batch_3);

        /* Apply quantization */
        ae_int16x4 out_0, out_1, out_2, out_3;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_multiplier, l_mult, right_shift, out_zero_bias);

        /* Store output */
        ae_int8x8 out32_0, out32_1; 
        PACK_32X2(out32_0, out_0, out_1);
        PACK_32X2(out32_1, out_2, out_3);

        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_0), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_H_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
        AE_S32_L_XP(AE_MOVINT32X2_FROMINT8X8(out32_1), (ae_int32 *)p_dst_0, out_offset);
      }
      /*
         for (; vec_itr < vec_count; vec_itr++)
         {
         }
       */
    }
    return 0;
  } 

  /* Special case for cols == 64 */
  if((cols1 == 64) &&
      (row_stride1 == 64) &&
      (vec_offset == 64) &&
      ALIGNED_PTR(p_mat1, 16) &&
      ALIGNED_PTR(p_vec1, 16) &&
      ALIGNED_PTR(p_out, 4) &&
      (out_stride == 1) &&   // NHWC case
      ((out_offset & 0x3) == 0) &&   // NHWC case
      ((rows & 0x3) == 0) &&
      ((vec_count & 0x3) == 0)
    )
  {
    ae_int32x2 l_mult = AE_MOVDA32(1 << left_shift);

    AE_MOVZBVCDR(biasvc1);

    special_function_for_cols_mul_32
      (p_out,
       p_mat1,
       p_vec1,
       p_bias,
       rows,
       vec_count,
       cols1,
       out_multiplier,
       l_mult,
       right_shift,
       out_zero_bias,
       out_stride,
       row_stride1,
       vec_offset,
       out_offset
      );

    return 0;
  }

  /* Special case for cols == 128 */
  if((cols1 == 128) &&
      (row_stride1 == 128) &&
      (vec_offset == 128) &&
      ALIGNED_PTR(p_mat1, 16) &&
      ALIGNED_PTR(p_vec1, 16) &&
      ALIGNED_PTR(p_out, 4) &&
      (out_stride == 1) &&   // NHWC case
      ((out_offset & 0x3) == 0) &&   // NHWC case
      ((rows & 0x3) == 0) &&
      ((vec_count & 0x3) == 0)
    )
  {
    ae_int32x2 l_mult = AE_MOVDA32(1 << left_shift);

    AE_MOVZBVCDR(biasvc1);

    special_function_for_cols_mul_32
      (p_out,
       p_mat1,
       p_vec1,
       p_bias,
       rows,
       vec_count,
       cols1,
       out_multiplier,
       l_mult,
       right_shift,
       out_zero_bias,
       out_stride,
       row_stride1,
       vec_offset,
       out_offset
      );

    return 0;
  }

#undef VEC_UNROLL
#define VEC_UNROLL 4

  if(
      ALIGNED_PTR(p_mat1, 16) &&
      ALIGNED_PTR(p_vec1, 16) &&
      ((row_stride1 & 15) == 0) &&
      ((vec_offset & 15) == 0)
      )
  {
    for(m_itr = 0; m_itr < (rows & ~(4 - 1)); m_itr += 4)
    {
      UWORD8* p_dst_0 = (UWORD8*)p_out + (m_itr + 0) * out_stride;
      UWORD8* p_dst_1 = (UWORD8*)p_out + (m_itr + 1) * out_stride;
      UWORD8* p_dst_2 = (UWORD8*)p_out + (m_itr + 2) * out_stride;
      UWORD8* p_dst_3 = (UWORD8*)p_out + (m_itr + 3) * out_stride;

      for (vec_itr = 0; vec_itr < (vec_count & ~(VEC_UNROLL-1)); vec_itr += VEC_UNROLL)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec0 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);
        ae_int32x2 acc_row0_vec1 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec1 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);
        ae_int32x2 acc_row0_vec2 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec2 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);
        ae_int32x2 acc_row0_vec3 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec3 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);

        ae_int8* p_vec_0  = (ae_int8 *)(p_vec1 + vec_itr * vec_offset);
        ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1]; 

        AE_MOVZBVCDR(biasvc1);
        _xa_nn_dot_product_4_rows_4_vecs_aligned
          (&acc_row0_vec0
           ,&acc_row0_vec1
           ,&acc_row0_vec2
           ,&acc_row0_vec3
           ,&acc_row1_vec0
           ,&acc_row1_vec1
           ,&acc_row1_vec2
           ,&acc_row1_vec3
           ,p_mat1_0
           ,p_vec_0
           ,cols1
           ,row_stride1
           ,vec_offset
           ,-mat1_zero_bias
          );

        ae_int16x4 out_0, out_1, out_2, out_3;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_multiplier, l_mult, right_shift, out_zero_bias);

        ae_int8x8 temp_vec0, temp_vec1;
        temp_vec0 = AE_SATU8X8X16(out_0, out_1);
        temp_vec1 = AE_SATU8X8X16(out_2, out_3);

        AE_SW_S8_7_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_6_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_5_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
        AE_SW_S8_4_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);
        AE_SW_S8_3_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_2_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_1_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);

        AE_SW_S8_7_XP(temp_vec1, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_6_XP(temp_vec1, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_5_XP(temp_vec1, (ae_int8 *) p_dst_2, out_offset);
        AE_SW_S8_4_XP(temp_vec1, (ae_int8 *) p_dst_3, out_offset);
        AE_SW_S8_3_XP(temp_vec1, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_2_XP(temp_vec1, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_1_XP(temp_vec1, (ae_int8 *) p_dst_2, out_offset);
        AE_S8_0_XP(temp_vec1, (ae_int8 *) p_dst_3, out_offset);
      }

      // Remaining vectors
      for (; vec_itr < vec_count; vec_itr++)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec0 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);

        ae_int8* p_vec_0  = (ae_int8 *)(p_vec1 + vec_itr * vec_offset);
        ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1];

        AE_MOVZBVCDR(biasvc1);
        _xa_nn_dot_product_4_rows_1_vecs_aligned
          (&acc_row0_vec0
           ,&acc_row1_vec0
           ,p_mat1_0
           ,p_vec_0
           ,cols1
           ,row_stride1
           ,-vec1_zero_bias
          );

        ae_int16x4 out_0;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);

        ae_int8x8 temp_vec0;
        temp_vec0 = AE_SATU8X8X16(out_0, out_0);

        AE_SW_S8_3_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_2_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_1_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);
      }
    }

    // remaining rows
    for(; m_itr < rows; m_itr++)
    {
      UWORD8* p_dst = (UWORD8*)p_out + (m_itr + 0) * out_stride;

      vec_itr = 0;

      for (vec_itr = 0; vec_itr < (vec_count & ~(VEC_UNROLL-1)); vec_itr += VEC_UNROLL)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32(p_bias[m_itr]);
        ae_int32x2 acc_row0_vec1 = AE_MOVDA32(p_bias[m_itr]);
        ae_int8x8* p_vec_0  = (ae_int8x8*)(p_vec1 + vec_itr * vec_offset);
        ae_int8 *p_mat1_0 = (ae_int8*) &p_mat1[m_itr * row_stride1]; 

        AE_MOVZBVCDR(biascv1);
        _xa_nn_dot_product_4_rows_1_vecs_aligned
          (&acc_row0_vec0
           ,&acc_row0_vec1
           ,p_vec_0
           ,p_mat1_0
           ,cols1
           ,vec_offset
           ,-mat1_zero_bias
          );

        ae_int16x4 out_0;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row0_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);

        ae_int8x8 temp_vec0;
        temp_vec0 = AE_SATU8X8X16(out_0, out_0);

        AE_SW_S8_3_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
        AE_SW_S8_2_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
        AE_SW_S8_1_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
      }

      // Remaining vectors
      for (; vec_itr < vec_count; vec_itr++)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32(p_bias[m_itr]);
        ae_int32x2 acc_row0_vec1 = AE_MOVDA32(p_bias[m_itr]);
        ae_int8* p_vec_0  = (ae_int8*)(p_vec1 + vec_itr * vec_offset);
        ae_int8x8 *p_mat1_0 = (ae_int8x8*) &p_mat1[m_itr * row_stride1]; 

        AE_MOVZBVCDR(biasvc1);
        _xa_nn_dot_product_1_rows_1_vecs_aligned
          (&acc_row0_vec0
           ,&acc_row0_vec1
           ,p_mat1_0
           ,p_vec_0
           ,cols1
           ,-vec1_zero_bias
          );

        ae_int8x8 temp_vec0;
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2(acc_row0_vec0, out_multiplier, left_shift, right_shift);
        acc_row0_vec0 = AE_ADD32S(acc_row0_vec0, out_zero_bias);
        acc_row0_vec0 = AE_MAX32(acc_row0_vec0, min_uint8);
        temp_vec0 = AE_SATU8X4X32_L(acc_row0_vec0, acc_row0_vec0);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
      }
    }
  }
  else if (p_mat1 && p_vec1)
  {
    for(m_itr = 0; m_itr < (rows & ~(32 - 1)); m_itr += 32)
    {
      for(ii = 0; ii < 8; ii++)
      {
        UWORD8* p_dst_0 = (UWORD8*)p_out + (m_itr + ii + 0) * out_stride;
        UWORD8* p_dst_1 = (UWORD8*)p_out + (m_itr + ii + 8) * out_stride;
        UWORD8* p_dst_2 = (UWORD8*)p_out + (m_itr + ii + 16) * out_stride;
        UWORD8* p_dst_3 = (UWORD8*)p_out + (m_itr + ii + 24) * out_stride;
        for (vec_itr = 0; vec_itr < (vec_count & ~(VEC_UNROLL-1)); vec_itr += VEC_UNROLL)
        {
          ae_int32x2 acc_row0_vec0 = AE_MOVDA32X2(p_bias[ 0 + ii + m_itr], p_bias[ 8 + ii + m_itr]);
          ae_int32x2 acc_row1_vec0 = AE_MOVDA32X2(p_bias[16 + ii + m_itr], p_bias[24 + ii + m_itr]);
          ae_int32x2 acc_row0_vec1 = AE_MOVDA32X2(p_bias[ 0 + ii + m_itr], p_bias[ 8 + ii + m_itr]);
          ae_int32x2 acc_row1_vec1 = AE_MOVDA32X2(p_bias[16 + ii + m_itr], p_bias[24 + ii + m_itr]);
          ae_int32x2 acc_row0_vec2 = AE_MOVDA32X2(p_bias[ 0 + ii + m_itr], p_bias[ 8 + ii + m_itr]);
          ae_int32x2 acc_row1_vec2 = AE_MOVDA32X2(p_bias[16 + ii + m_itr], p_bias[24 + ii + m_itr]);
          ae_int32x2 acc_row0_vec3 = AE_MOVDA32X2(p_bias[ 0 + ii + m_itr], p_bias[ 8 + ii + m_itr]);
          ae_int32x2 acc_row1_vec3 = AE_MOVDA32X2(p_bias[16 + ii + m_itr], p_bias[24 + ii + m_itr]);

          ae_int8* p_vec_0  = (ae_int8 *)(p_vec1 + vec_itr * vec_offset);
          ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + ii + 0) * row_stride1]; 

          AE_MOVZBVCDR(biasvc1);
          _xa_nn_dot_product_4_rows_4_vecs_offset_aligned
            (&acc_row0_vec0
             ,&acc_row0_vec1
             ,&acc_row0_vec2
             ,&acc_row0_vec3
             ,&acc_row1_vec0
             ,&acc_row1_vec1
             ,&acc_row1_vec2
             ,&acc_row1_vec3
             ,p_mat1_0
             ,p_vec_0
             ,cols1
             ,row_stride1
             ,vec_offset
             ,-mat1_zero_bias
            );

          ae_int16x4 out_0, out_1, out_2, out_3;

          MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);
          MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);
          MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_multiplier, l_mult, right_shift, out_zero_bias);
          MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_multiplier, l_mult, right_shift, out_zero_bias);

          ae_int8x8 temp_vec0, temp_vec1;
          temp_vec0 = AE_SATU8X8X16(out_0, out_1);
          temp_vec1 = AE_SATU8X8X16(out_2, out_3);

          AE_SW_S8_7_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
          AE_SW_S8_6_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
          AE_SW_S8_5_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
          AE_SW_S8_4_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);
          AE_SW_S8_3_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
          AE_SW_S8_2_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
          AE_SW_S8_1_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
          AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);

          AE_SW_S8_7_XP(temp_vec1, (ae_int8 *) p_dst_0, out_offset);
          AE_SW_S8_6_XP(temp_vec1, (ae_int8 *) p_dst_1, out_offset);
          AE_SW_S8_5_XP(temp_vec1, (ae_int8 *) p_dst_2, out_offset);
          AE_SW_S8_4_XP(temp_vec1, (ae_int8 *) p_dst_3, out_offset);
          AE_SW_S8_3_XP(temp_vec1, (ae_int8 *) p_dst_0, out_offset);
          AE_SW_S8_2_XP(temp_vec1, (ae_int8 *) p_dst_1, out_offset);
          AE_SW_S8_1_XP(temp_vec1, (ae_int8 *) p_dst_2, out_offset);
          AE_S8_0_XP(temp_vec1, (ae_int8 *) p_dst_3, out_offset);
        }

        // Remaining vectors
        for (; vec_itr < vec_count; vec_itr++)
        {
          ae_int32x2 acc_row0_vec0 = AE_MOVDA32X2(p_bias[ 0 + ii + m_itr], p_bias[ 8 + ii + m_itr]);
          ae_int32x2 acc_row1_vec0 = AE_MOVDA32X2(p_bias[ 16 + ii + m_itr], p_bias[ 24 + ii + m_itr]);

          WORD8* p_dst = (WORD8*)p_out + vec_itr * out_offset + (m_itr + ii) * out_stride;
          ae_int8* p_vec_0  = (ae_int8*)(p_vec1 + vec_itr * vec_offset);
          ae_int8x8* p_mat1_0 = (ae_int8x8*) &p_mat1[(m_itr + ii + 0)* row_stride1]; 

          AE_MOVZBVCDR(biasvc1);
          _xa_nn_dot_product_4_rows_1_vecs_offset_aligned
            (&acc_row0_vec0
             ,&acc_row1_vec0
             ,p_mat1_0
             ,p_vec_0
             ,cols1
             ,row_stride1
             ,-vec1_zero_bias
            );

          ae_int16x4 out_0;

          MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);

          ae_int8x8 temp_vec0;
          temp_vec0 = AE_SATU8X8X16(out_0, out_0);

          AE_SW_S8_3_X(temp_vec0, (ae_int8 *) p_dst, 0 * out_stride);
          AE_SW_S8_2_X(temp_vec0, (ae_int8 *) p_dst, 8 * out_stride);
          AE_SW_S8_1_X(temp_vec0, (ae_int8 *) p_dst, 16 * out_stride);
          AE_S8_0_X(temp_vec0, (ae_int8 *) p_dst, 24 * out_stride);
        }
      }
    }

    // Process loop for 4 rows and 4 vectors 
    for(; m_itr < (rows & ~(4 - 1)); m_itr += 4)
    {
      UWORD8* p_dst_0 = (UWORD8*)p_out + (m_itr + 0) * out_stride;
      UWORD8* p_dst_1 = (UWORD8*)p_out + (m_itr + 1) * out_stride;
      UWORD8* p_dst_2 = (UWORD8*)p_out + (m_itr + 2) * out_stride;
      UWORD8* p_dst_3 = (UWORD8*)p_out + (m_itr + 3) * out_stride;

      vec_itr = 0;

      for (vec_itr = 0; vec_itr < (vec_count & ~(VEC_UNROLL-1)); vec_itr += VEC_UNROLL)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec0 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);
        ae_int32x2 acc_row0_vec1 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec1 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);
        ae_int32x2 acc_row0_vec2 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec2 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);
        ae_int32x2 acc_row0_vec3 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec3 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);

        ae_int8* p_vec_0  = (ae_int8 *)(p_vec1 + vec_itr * vec_offset);
        ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1]; 

        AE_MOVZBVCDR(biasvc1);
        _xa_nn_dot_product_4_rows_4_vecs_unaligned
          (&acc_row0_vec0
           ,&acc_row0_vec1
           ,&acc_row0_vec2
           ,&acc_row0_vec3
           ,&acc_row1_vec0
           ,&acc_row1_vec1
           ,&acc_row1_vec2
           ,&acc_row1_vec3
           ,p_mat1_0
           ,p_vec_0
           ,cols1
           ,row_stride1
           ,vec_offset
           ,-mat1_zero_bias
          );

        ae_int16x4 out_0, out_1, out_2, out_3;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_1, acc_row0_vec1, acc_row1_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_2, acc_row0_vec2, acc_row1_vec2, out_multiplier, l_mult, right_shift, out_zero_bias);
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_3, acc_row0_vec3, acc_row1_vec3, out_multiplier, l_mult, right_shift, out_zero_bias);

        ae_int8x8 temp_vec0, temp_vec1;
        temp_vec0 = AE_SATU8X8X16(out_0, out_1);
        temp_vec1 = AE_SATU8X8X16(out_2, out_3);

        AE_SW_S8_7_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_6_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_5_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
        AE_SW_S8_4_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);
        AE_SW_S8_3_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_2_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_1_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);

        AE_SW_S8_7_XP(temp_vec1, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_6_XP(temp_vec1, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_5_XP(temp_vec1, (ae_int8 *) p_dst_2, out_offset);
        AE_SW_S8_4_XP(temp_vec1, (ae_int8 *) p_dst_3, out_offset);
        AE_SW_S8_3_XP(temp_vec1, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_2_XP(temp_vec1, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_1_XP(temp_vec1, (ae_int8 *) p_dst_2, out_offset);
        AE_S8_0_XP(temp_vec1, (ae_int8 *) p_dst_3, out_offset);
      }

      // Remaining vectors
      for (; vec_itr < vec_count; vec_itr++)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32X2(p_bias[m_itr + 0], p_bias[m_itr + 1]);
        ae_int32x2 acc_row1_vec0 = AE_MOVDA32X2(p_bias[m_itr + 2], p_bias[m_itr + 3]);

        ae_int8* p_vec_0  = (ae_int8 *)(p_vec1 + vec_itr * vec_offset);
        ae_int8x8 *p_mat1_0 = (ae_int8x8 *) &p_mat1[(m_itr + 0) * row_stride1];

        AE_MOVZBVCDR(biasvc1);
        _xa_nn_dot_product_4_rows_1_vecs_unaligned
          (&acc_row0_vec0
           ,&acc_row1_vec0
           ,p_mat1_0
           ,p_vec_0
           ,cols1
           ,row_stride1
           ,-vec1_zero_bias
          );

        ae_int16x4 out_0;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row1_vec0, out_multiplier, l_mult, right_shift, out_zero_bias);

        ae_int8x8 temp_vec0;
        temp_vec0 = AE_SATU8X8X16(out_0, out_0);

        AE_SW_S8_3_XP(temp_vec0, (ae_int8 *) p_dst_0, out_offset);
        AE_SW_S8_2_XP(temp_vec0, (ae_int8 *) p_dst_1, out_offset);
        AE_SW_S8_1_XP(temp_vec0, (ae_int8 *) p_dst_2, out_offset);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst_3, out_offset);
      }
    }

    // remaining rows
    for(; m_itr < rows; m_itr++)
    {
      UWORD8* p_dst = (UWORD8*)p_out + (m_itr + 0) * out_stride;

      vec_itr = 0;

      for (vec_itr = 0; vec_itr < (vec_count & ~(VEC_UNROLL-1)); vec_itr += VEC_UNROLL)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32(p_bias[m_itr]);
        ae_int32x2 acc_row0_vec1 = AE_MOVDA32(p_bias[m_itr]);
        ae_int8x8* p_vec_0  = (ae_int8x8*)(p_vec1 + vec_itr * vec_offset);
        ae_int8 *p_mat1_0 = (ae_int8*) &p_mat1[m_itr * row_stride1]; 

        AE_MOVZBVCDR(biascv1);
        _xa_nn_dot_product_4_rows_1_vecs_unaligned
          (&acc_row0_vec0
           ,&acc_row0_vec1
           ,p_vec_0
           ,p_mat1_0
           ,cols1
           ,vec_offset
           ,-mat1_zero_bias
          );

        ae_int16x4 out_0;

        MULTIPLYBYQUANTIZEDMULTIPLIER_X2_X2(out_0, acc_row0_vec0, acc_row0_vec1, out_multiplier, l_mult, right_shift, out_zero_bias);

        ae_int8x8 temp_vec0;
        temp_vec0 = AE_SATU8X8X16(out_0, out_0);

        AE_SW_S8_3_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
        AE_SW_S8_2_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
        AE_SW_S8_1_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
      }

      // Remaining vectors
      for (; vec_itr < vec_count; vec_itr++)
      {
        ae_int32x2 acc_row0_vec0 = AE_MOVDA32(p_bias[m_itr]);
        ae_int32x2 acc_row0_vec1 = AE_MOVDA32(p_bias[m_itr]);
        ae_int8* p_vec_0  = (ae_int8*)(p_vec1 + vec_itr * vec_offset);
        ae_int8x8 *p_mat1_0 = (ae_int8x8*) &p_mat1[m_itr * row_stride1]; 

        AE_MOVZBVCDR(biasvc1);
        _xa_nn_dot_product_1_rows_1_vecs_unaligned
          (&acc_row0_vec0
           ,&acc_row0_vec1
           ,p_mat1_0
           ,p_vec_0
           ,cols1
           ,-vec1_zero_bias
          );

        ae_int8x8 temp_vec0;
        MULTIPLYBYQUANTIZEDMULTIPLIER_X2(acc_row0_vec0, out_multiplier, left_shift, right_shift);
        acc_row0_vec0 = AE_ADD32S(acc_row0_vec0, out_zero_bias);
        acc_row0_vec0 = AE_MAX32(acc_row0_vec0, min_uint8);
        temp_vec0 = AE_SATU8X4X32_L(acc_row0_vec0, acc_row0_vec0);
        AE_S8_0_XP(temp_vec0, (ae_int8 *) p_dst, out_offset);
      }
    }
  }
  else
  {
    return -1;
  }
  return 0;
}
