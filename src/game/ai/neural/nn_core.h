/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake3e-HD source code.

Quake3e-HD source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake3e-HD source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
===========================================================================
*/

#ifndef NN_CORE_H
#define NN_CORE_H

#include "../../../engine/common/q_shared.h"

#define NN_MAX_LAYERS 8
#define NN_MAX_NEURONS_PER_LAYER 256
#define NN_MAX_WEIGHTS 65536
#define NN_BATCH_SIZE 32
#define NN_LEARNING_RATE 0.0003f
#define NN_GRADIENT_CLIP 1.0f

typedef enum {
    ACTIVATION_NONE,
    ACTIVATION_RELU,
    ACTIVATION_TANH,
    ACTIVATION_SIGMOID,
    ACTIVATION_LEAKY_RELU,
    ACTIVATION_ELU,
    ACTIVATION_SOFTMAX
} nn_activation_t;

typedef enum {
    NN_TYPE_DECISION,
    NN_TYPE_COMBAT,
    NN_TYPE_NAVIGATION,
    NN_TYPE_TEAM,
    NN_TYPE_MAX
} nn_type_t;

typedef struct nn_layer_s {
    int input_size;
    int output_size;
    float *weights;
    float *bias;
    float *output;
    float *gradients;
    float *weight_momentum;
    float *bias_momentum;
    nn_activation_t activation;
    float dropout_rate;
    qboolean use_batch_norm;
    float *batch_norm_gamma;
    float *batch_norm_beta;
    float *running_mean;
    float *running_variance;
} nn_layer_t;

typedef struct nn_network_s {
    nn_type_t type;
    int num_layers;
    nn_layer_t layers[NN_MAX_LAYERS];
    float *input_buffer;
    float *output_buffer;
    int input_size;
    int output_size;
    float learning_rate;
    float momentum;
    float weight_decay;
    int batch_count;
    float loss;
    qboolean training_mode;
    
    // Performance metrics
    int forward_passes;
    int backward_passes;
    float total_inference_time;
    float total_training_time;
} nn_network_t;

typedef struct nn_tensor_s {
    float *data;
    int dims[4];
    int num_dims;
    int total_size;
} nn_tensor_t;

// Core functions
void NN_Init(void);
void NN_Shutdown(void);
nn_network_t *NN_CreateNetwork(nn_type_t type, int *layer_sizes, int num_layers);
void NN_DestroyNetwork(nn_network_t *network);
void NN_Forward(nn_network_t *network, const float *input, float *output);
void NN_Backward(nn_network_t *network, const float *target, float *loss);
void NN_UpdateWeights(nn_network_t *network);
void NN_SaveNetwork(nn_network_t *network, const char *filename);
nn_network_t *NN_LoadNetwork(const char *filename);

// Activation functions
float NN_ReLU(float x);
float NN_ReLUDerivative(float x);
float NN_Tanh(float x);
float NN_TanhDerivative(float x);
float NN_Sigmoid(float x);
float NN_SigmoidDerivative(float x);
float NN_LeakyReLU(float x, float alpha);
float NN_LeakyReLUDerivative(float x, float alpha);
void NN_Softmax(float *input, float *output, int size);

// Tensor operations
nn_tensor_t *NN_CreateTensor(int *dims, int num_dims);
void NN_DestroyTensor(nn_tensor_t *tensor);
void NN_MatMul(const nn_tensor_t *a, const nn_tensor_t *b, nn_tensor_t *result);
void NN_Conv2D(const nn_tensor_t *input, const nn_tensor_t *kernel, nn_tensor_t *output, int stride, int padding);
void NN_MaxPool2D(const nn_tensor_t *input, nn_tensor_t *output, int pool_size, int stride);

// Utility functions
void NN_InitializeWeights(nn_layer_t *layer, float scale);
void NN_ApplyDropout(nn_layer_t *layer, float rate);
void NN_BatchNormForward(nn_layer_t *layer, qboolean training);
void NN_BatchNormBackward(nn_layer_t *layer);
float NN_ComputeLoss(const float *predicted, const float *target, int size);
void NN_ClipGradients(nn_network_t *network, float max_norm);

// SIMD optimizations
void NN_VectorAdd_SSE(const float *a, const float *b, float *result, int size);
void NN_VectorMul_SSE(const float *a, const float *b, float *result, int size);
void NN_MatMul_SSE(const float *a, const float *b, float *c, int m, int n, int k);

// GPU acceleration (if available)
qboolean NN_InitGPU(void);
void NN_ShutdownGPU(void);
void NN_ForwardGPU(nn_network_t *network, const float *input, float *output);
void NN_BackwardGPU(nn_network_t *network, const float *target, float *loss);

#endif // NN_CORE_H

