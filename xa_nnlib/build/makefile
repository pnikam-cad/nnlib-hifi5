#
# Copyright (c) 2018-2021 Cadence Design Systems, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to use this Software with Cadence processor cores only and
# not with any other processors and platforms, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#


.PHONY: all install clean NNLIB

ROOTDIR = ..
CODEC_NAME = nnlib

CFLAGS += \
        -ffunction-sections \
        -DMODEL_INT16 \
        -Dhifi5 \

ifeq ($(DISABLE_ARG_CHK), 1)
CFLAGS += -DDISABLE_ARG_CHK
endif


vpath %.c $(ROOTDIR)/algo/kernels/matXvec/hifi5
vpath %.c $(ROOTDIR)/algo/kernels/activations/hifi5
vpath %.c $(ROOTDIR)/algo/kernels/basic/hifi5
vpath %.c $(ROOTDIR)/algo/kernels/cnn/hifi5
vpath %.c $(ROOTDIR)/algo/kernels/fc/hifi4
vpath %.c $(ROOTDIR)/algo/kernels/pool/hifi5
vpath %.c $(ROOTDIR)/algo/ndsp/hifi5/src
vpath %.c $(ROOTDIR)/algo/layers/gru/src
vpath %.c $(ROOTDIR)/algo/layers/lstm/src
vpath %.c $(ROOTDIR)/algo/layers/cnn/src
vpath %.c $(ROOTDIR)/algo/common/src
vpath %.c $(ROOTDIR)/algo/kernels/norm/hifi5

MATXVECO2OBJS = \
  xa_nn_matXvec_8x8.o \
  xa_nn_matXvec_16x8.o \
  xa_nn_matXvec_8x16.o \
  xa_nn_matXvec_16x16.o \
  xa_nn_matXvec_asym8xasym8.o \
  xa_nn_matXvec_sym8sxasym8s.o \
  xa_nn_matXvec_asym8xasym8_batch.o \
  xa_nn_matXvec_8x8_batch.o \
  xa_nn_matXvec_8x16_batch.o \
  xa_nn_matXvec_16x16_batch.o \
  xa_nn_matXvec_f32.o \
  xa_nn_matXvec_f32_batch.o \
  xa_nn_matmul_8x8.o \
  xa_nn_matmul_asym8xasym8.o \
  xa_nn_matmul_sym8sxasym8s.o 
  

ACTIVATIONSO2OBJS = \
  xa_nn_activations_32_32.o \
  xa_nn_activations_f32_f32.o \
  xa_nn_activations_32_8.o \
  xa_nn_activations_32_16.o \
  xa_nn_activations_asym8_asym8.o\
  xa_nn_softmax_asym8_asym8.o \
  xa_nn_activations_8_8.o \
  xa_nn_activations_16_16.o


NDSPO2OBJS = \
  expf_tbl.o \
  inff_tbl.o \
  nanf_tbl.o \
  pow2f_tbl.o \
  scl_sigmoidf_hifi5.o \
  scl_tanhf_hifi5.o \
  tanhf_tbl.o \
  vec_alognf_hifi5.o \
  vec_relu32x32_hifi5.o \
  vec_reluf_hifi5.o \
  vec_sigmoid32x32_hifi5.o \
  vec_sigmoidf_hifi5.o \
  vec_softmax32x32_hifi5.o \
  vec_softmaxf_hifi5.o \
  vec_tanh32x32_hifi5.o \
  vec_tanhf_hifi5.o 

CONVO2OBJS = \
  xa_nn_conv1d_std_8x16.o \
  xa_nn_conv1d_std_8x8.o \
  xa_nn_conv1d_std_16x16.o \
  xa_nn_conv1d_std_asym8xasym8.o \
  xa_nn_conv1d_std_f32.o \
  xa_nn_conv1d_std_circ_buf.o \
  xa_nn_matXvec_8x16_16_circ_nb.o \
  xa_nn_matXvec_8x8_8_circ_nb.o \
  xa_nn_matXvec_16x16_16_circ_nb.o \
  xa_nn_matXvec_asym8xasym8_asym8_circ_nb.o \
  xa_nn_matXvec_f32_circ_nb.o \
  xa_nn_conv2d_std_8x16.o \
  xa_nn_conv2d_std_8x8.o \
  xa_nn_conv2d_std_16x16.o \
  xa_nn_conv2d_std_asym8xasym8.o \
  xa_nn_conv2d_std_sym8sxasym8s.o \
  xa_nn_conv2d_std_f32.o \
  xa_nn_conv2d_std_circ_buf.o \
  xa_nn_matXvec_8x16_16_circ.o \
  xa_nn_matXvec_8x8_8_circ.o \
  xa_nn_matXvec_16x16_16_circ.o \
  xa_nn_matXvec_asym8xasym8_asym8_circ.o \
  xa_nn_matXvec_sym8sxasym8s_asym8s_circ.o \
  xa_nn_matXvec_f32_circ.o \
  xa_nn_circ_buf.o \
  xa_nn_conv2d_depthwise.o \
  xa_nn_conv2d_depthwise_16x16.o \
  xa_nn_conv2d_pointwise_16x16.o \
  xa_nn_conv2d_depthwise_8x16.o \
  xa_nn_conv2d_pointwise_8x16.o \
  xa_nn_conv2d_depthwise_8x8.o \
  xa_nn_conv2d_pointwise_8x8.o \
  xa_nn_conv2d_depthwise_asym8xasym8.o \
  xa_nn_conv2d_pointwise_asym8xasym8.o \
  xa_nn_conv2d_depthwise_sym8sxasym8s.o \
  xa_nn_conv2d_pointwise_sym8sxasym8s.o \
  xa_nn_conv2d_depthwise_f32.o \
  xa_nn_conv2d_pointwise_f32.o

FCO2OBJS = \
    xa_nn_fully_connected.o

POOLO2OBJS = \
  xa_nn_inv_256_tbl.o \
  xa_nn_avgpool.o \
  xa_nn_avgpool_8.o \
  xa_nn_avgpool_8_nhwc.o \
  xa_nn_avgpool_16.o \
  xa_nn_avgpool_16_nhwc.o \
  xa_nn_avgpool_f32.o \
  xa_nn_avgpool_f32_nhwc.o \
  xa_nn_avgpool_asym8.o \
  xa_nn_avgpool_asym8_nhwc.o \
  xa_nn_maxpool.o \
  xa_nn_maxpool_8.o \
  xa_nn_maxpool_8_nhwc.o\
  xa_nn_maxpool_16.o \
  xa_nn_maxpool_16_nhwc.o \
  xa_nn_maxpool_f32.o \
  xa_nn_maxpool_f32_nhwc.o \
  xa_nn_maxpool_asym8.o \
  xa_nn_maxpool_asym8_nhwc.o 

GRUO2OBJS = \
  xa_nn_gru_api.o 

LSTMO2OBJS = \
  xa_nn_lstm_api.o 

CNNO2OBJS = \
  xa_nn_cnn_api.o 

COMMONOSOBJS = \
  xa_nnlib_common_api.o 

BASICOBJS = \
    xa_nn_vec_interpolation_q15.o \
    xa_nn_elm_mul_16x16.o \
    xa_nn_elm_mul_f32.o\
    xa_nn_elm_add_quant8.o\
    xa_nn_elm_sub_quant8.o\
    xa_nn_elm_mul_quant8.o\
    xa_nn_elm_minmax_8.o\
    xa_nn_elm_quantize.o\
    xa_nn_elm_add_f32.o \
    xa_nn_elm_sub_f32.o \
    xa_nn_elm_div_f32.o \
    xa_nn_elm_mul_acc_f32.o \
    xa_nn_floor_f32.o \
    xa_nn_dot_prod_16x16.o \
    xa_nn_elm_compare_quant8.o

NORMO2OBJS = \
  xa_nn_l2_norm_f32.o

LIBO2OBJS = $(MATXVECO2OBJS) $(ACTIVATIONSO2OBJS) $(NDSPO2OBJS) $(CONVO2OBJS) $(FCO2OBJS) $(POOLO2OBJS) $(GRUO2OBJS) $(LSTMO2OBJS) $(CNNO2OBJS) $(BASICOBJS) $(NORMO2OBJS)
LIBOSOBJS = $(COMMONOSOBJS)

INCLUDES = \
    -I$(ROOTDIR)/include \
    -I$(ROOTDIR)/include/nnlib \
    -I$(ROOTDIR)/algo/ndsp/hifi5/include \
    -I$(ROOTDIR)/algo/common/include

ifeq ($(CPU), x86)
vpath %.c $(ROOTDIR)/algo/cstub/src
CSTUBOSOBJS = 

INCLUDES += \
    -I$(ROOTDIR)/algo/cstub/include
endif

NNLIB: $(CODEC_NAME) 

include $(ROOTDIR)/build/common.mk
