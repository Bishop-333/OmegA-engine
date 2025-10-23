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

#include "nn_core.h"
#include "../../../engine/core/qcommon.h"
#include "../game_interface.h"
#include "../ai_system.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <immintrin.h>
#endif

static struct {
    qboolean initialized;
    qboolean gpu_available;
    int total_networks;
    size_t total_memory;
    nn_network_t *networks[NN_TYPE_MAX];
} nn_global;

/*
==================
NN_Init
==================
*/
void NN_Init(void) {
    if (nn_global.initialized) {
        return;
    }
    
    memset(&nn_global, 0, sizeof(nn_global));
    nn_global.initialized = qtrue;
    
    // Try to initialize GPU acceleration
    nn_global.gpu_available = NN_InitGPU();
    
    Com_Printf("Neural Network System Initialized\n");
    if (nn_global.gpu_available) {
        Com_Printf("GPU acceleration available\n");
    }
}

/*
==================
NN_Shutdown
==================
*/
void NN_Shutdown(void) {
    int i;
    
    if (!nn_global.initialized) {
        return;
    }
    
    // Destroy all networks
    for (i = 0; i < NN_TYPE_MAX; i++) {
        if (nn_global.networks[i]) {
            NN_DestroyNetwork(nn_global.networks[i]);
        }
    }
    
    if (nn_global.gpu_available) {
        NN_ShutdownGPU();
    }
    
    nn_global.initialized = qfalse;
    Com_Printf("Neural Network System Shutdown\n");
}

/*
==================
NN_CreateNetwork
==================
*/
nn_network_t *NN_CreateNetwork(nn_type_t type, int *layer_sizes, int num_layers) {
    nn_network_t *network;
    int i, total_weights = 0;
    
    if (num_layers < 2 || num_layers > NN_MAX_LAYERS) {
        Com_Error(ERR_DROP, "Invalid number of layers: %d", num_layers);
        return NULL;
    }
    
    network = (nn_network_t *)Z_Malloc(sizeof(nn_network_t));
    memset(network, 0, sizeof(nn_network_t));
    
    network->type = type;
    network->num_layers = num_layers - 1; // Exclude input layer
    network->input_size = layer_sizes[0];
    network->output_size = layer_sizes[num_layers - 1];
    network->learning_rate = NN_LEARNING_RATE;
    network->momentum = 0.9f;
    network->weight_decay = 0.0001f;
    
    // Allocate input/output buffers
    network->input_buffer = (float *)Z_Malloc(network->input_size * sizeof(float));
    network->output_buffer = (float *)Z_Malloc(network->output_size * sizeof(float));
    
    // Initialize layers
    for (i = 0; i < network->num_layers; i++) {
        nn_layer_t *layer = &network->layers[i];
        layer->input_size = layer_sizes[i];
        layer->output_size = layer_sizes[i + 1];
        
        int weight_count = layer->input_size * layer->output_size;
        total_weights += weight_count;
        
        // Allocate layer memory
        layer->weights = (float *)Z_Malloc(weight_count * sizeof(float));
        layer->bias = (float *)Z_Malloc(layer->output_size * sizeof(float));
        layer->output = (float *)Z_Malloc(layer->output_size * sizeof(float));
        layer->gradients = (float *)Z_Malloc(layer->output_size * sizeof(float));
        layer->weight_momentum = (float *)Z_Malloc(weight_count * sizeof(float));
        layer->bias_momentum = (float *)Z_Malloc(layer->output_size * sizeof(float));
        
        // Set activation function
        if (i == network->num_layers - 1) {
            // Output layer
            if (type == NN_TYPE_DECISION || type == NN_TYPE_TEAM) {
                layer->activation = ACTIVATION_SOFTMAX;
            } else {
                layer->activation = ACTIVATION_TANH;
            }
        } else {
            // Hidden layers
            layer->activation = ACTIVATION_LEAKY_RELU;
        }
        
        // Initialize batch normalization for hidden layers
        if (i < network->num_layers - 1) {
            layer->use_batch_norm = qtrue;
            layer->batch_norm_gamma = (float *)Z_Malloc(layer->output_size * sizeof(float));
            layer->batch_norm_beta = (float *)Z_Malloc(layer->output_size * sizeof(float));
            layer->running_mean = (float *)Z_Malloc(layer->output_size * sizeof(float));
            layer->running_variance = (float *)Z_Malloc(layer->output_size * sizeof(float));
            
            // Initialize gamma to 1 and beta to 0
            for (int j = 0; j < layer->output_size; j++) {
                layer->batch_norm_gamma[j] = 1.0f;
                layer->batch_norm_beta[j] = 0.0f;
                layer->running_variance[j] = 1.0f;
            }
        }
        
        // Initialize weights using He initialization
        NN_InitializeWeights(layer, sqrtf(2.0f / layer->input_size));
    }
    
    nn_global.networks[type] = network;
    nn_global.total_networks++;
    nn_global.total_memory += sizeof(nn_network_t) + total_weights * sizeof(float) * 6;
    
    Com_Printf("Created %s network with %d parameters\n", 
               type == NN_TYPE_DECISION ? "Decision" :
               type == NN_TYPE_COMBAT ? "Combat" :
               type == NN_TYPE_NAVIGATION ? "Navigation" : "Team",
               total_weights);
    
    return network;
}

/*
==================
NN_DestroyNetwork
==================
*/
void NN_DestroyNetwork(nn_network_t *network) {
    int i;
    
    if (!network) {
        return;
    }
    
    for (i = 0; i < network->num_layers; i++) {
        nn_layer_t *layer = &network->layers[i];
        
        Z_Free(layer->weights);
        Z_Free(layer->bias);
        Z_Free(layer->output);
        Z_Free(layer->gradients);
        Z_Free(layer->weight_momentum);
        Z_Free(layer->bias_momentum);
        
        if (layer->use_batch_norm) {
            Z_Free(layer->batch_norm_gamma);
            Z_Free(layer->batch_norm_beta);
            Z_Free(layer->running_mean);
            Z_Free(layer->running_variance);
        }
    }
    
    Z_Free(network->input_buffer);
    Z_Free(network->output_buffer);
    
    if (nn_global.networks[network->type] == network) {
        nn_global.networks[network->type] = NULL;
    }
    
    nn_global.total_networks--;
    Z_Free(network);
}

/*
==================
NN_Forward
==================
*/
void NN_Forward(nn_network_t *network, const float *input, float *output) {
    int i, j, k;
    float sum;
    const float *layer_input;
    int start_time = Sys_Milliseconds();
    
    if (!network || !input || !output) {
        return;
    }
    
    // Copy input to buffer
    memcpy(network->input_buffer, input, network->input_size * sizeof(float));
    layer_input = network->input_buffer;
    
    // Forward pass through each layer
    for (i = 0; i < network->num_layers; i++) {
        nn_layer_t *layer = &network->layers[i];
        
        // Compute layer output: output = activation(weights * input + bias)
        for (j = 0; j < layer->output_size; j++) {
            sum = layer->bias[j];
            
            // Dot product with weights
            for (k = 0; k < layer->input_size; k++) {
                sum += layer->weights[j * layer->input_size + k] * layer_input[k];
            }
            
            // Apply activation function
            switch (layer->activation) {
                case ACTIVATION_RELU:
                    layer->output[j] = NN_ReLU(sum);
                    break;
                case ACTIVATION_TANH:
                    layer->output[j] = NN_Tanh(sum);
                    break;
                case ACTIVATION_SIGMOID:
                    layer->output[j] = NN_Sigmoid(sum);
                    break;
                case ACTIVATION_LEAKY_RELU:
                    layer->output[j] = NN_LeakyReLU(sum, 0.01f);
                    break;
                case ACTIVATION_SOFTMAX:
                    layer->output[j] = sum; // Will apply softmax after all outputs computed
                    break;
                default:
                    layer->output[j] = sum;
                    break;
            }
        }
        
        // Apply softmax if needed
        if (layer->activation == ACTIVATION_SOFTMAX) {
            NN_Softmax(layer->output, layer->output, layer->output_size);
        }
        
        // Apply batch normalization if enabled
        if (layer->use_batch_norm) {
            NN_BatchNormForward(layer, network->training_mode);
        }
        
        // Apply dropout if in training mode
        if (network->training_mode && layer->dropout_rate > 0) {
            NN_ApplyDropout(layer, layer->dropout_rate);
        }
        
        // Set input for next layer
        layer_input = layer->output;
    }
    
    // Copy final output
    memcpy(output, network->layers[network->num_layers - 1].output, 
           network->output_size * sizeof(float));
    
    network->forward_passes++;
    network->total_inference_time += (Sys_Milliseconds() - start_time) * 0.001f;
}

/*
==================
NN_Backward
==================
*/
void NN_Backward(nn_network_t *network, const float *target, float *loss) {
    int i, j, k;
    float error;
    int start_time = Sys_Milliseconds();
    
    if (!network || !target) {
        return;
    }
    
    // Compute loss and output layer gradients
    nn_layer_t *output_layer = &network->layers[network->num_layers - 1];
    *loss = 0;
    
    for (i = 0; i < output_layer->output_size; i++) {
        error = target[i] - output_layer->output[i];
        *loss += error * error;
        
        // Gradient of loss with respect to output
        switch (output_layer->activation) {
            case ACTIVATION_TANH:
                output_layer->gradients[i] = error * NN_TanhDerivative(output_layer->output[i]);
                break;
            case ACTIVATION_SIGMOID:
                output_layer->gradients[i] = error * NN_SigmoidDerivative(output_layer->output[i]);
                break;
            case ACTIVATION_SOFTMAX:
                // For softmax with cross-entropy loss
                output_layer->gradients[i] = output_layer->output[i] - target[i];
                break;
            default:
                output_layer->gradients[i] = error;
                break;
        }
    }
    
    *loss = sqrtf(*loss / output_layer->output_size); // RMSE
    
    // Backpropagate through hidden layers
    for (i = network->num_layers - 1; i >= 0; i--) {
        nn_layer_t *layer = &network->layers[i];
        const float *prev_output = (i > 0) ? network->layers[i - 1].output : network->input_buffer;
        
        // Compute weight gradients
        for (j = 0; j < layer->output_size; j++) {
            for (k = 0; k < layer->input_size; k++) {
                int weight_idx = j * layer->input_size + k;
                float grad = layer->gradients[j] * prev_output[k];
                
                // Add L2 regularization
                grad += network->weight_decay * layer->weights[weight_idx];
                
                // Update momentum
                layer->weight_momentum[weight_idx] = network->momentum * layer->weight_momentum[weight_idx] + grad;
            }
            
            // Bias gradient
            layer->bias_momentum[j] = network->momentum * layer->bias_momentum[j] + layer->gradients[j];
        }
        
        // Propagate gradients to previous layer
        if (i > 0) {
            nn_layer_t *prev_layer = &network->layers[i - 1];
            memset(prev_layer->gradients, 0, prev_layer->output_size * sizeof(float));
            
            for (j = 0; j < layer->output_size; j++) {
                for (k = 0; k < layer->input_size; k++) {
                    int weight_idx = j * layer->input_size + k;
                    prev_layer->gradients[k] += layer->gradients[j] * layer->weights[weight_idx];
                }
            }
            
            // Apply activation derivative
            for (j = 0; j < prev_layer->output_size; j++) {
                switch (prev_layer->activation) {
                    case ACTIVATION_RELU:
                        prev_layer->gradients[j] *= NN_ReLUDerivative(prev_layer->output[j]);
                        break;
                    case ACTIVATION_LEAKY_RELU:
                        prev_layer->gradients[j] *= NN_LeakyReLUDerivative(prev_layer->output[j], 0.01f);
                        break;
                    case ACTIVATION_TANH:
                        prev_layer->gradients[j] *= NN_TanhDerivative(prev_layer->output[j]);
                        break;
                    case ACTIVATION_SIGMOID:
                        prev_layer->gradients[j] *= NN_SigmoidDerivative(prev_layer->output[j]);
                        break;
                    default:
                        break;
                }
            }
        }
    }
    
    // Clip gradients to prevent explosion
    NN_ClipGradients(network, NN_GRADIENT_CLIP);
    
    network->backward_passes++;
    network->total_training_time += (Sys_Milliseconds() - start_time) * 0.001f;
    network->loss = *loss;
}

/*
==================
NN_UpdateWeights
==================
*/
void NN_UpdateWeights(nn_network_t *network) {
    int i, j, k;
    
    if (!network) {
        return;
    }
    
    for (i = 0; i < network->num_layers; i++) {
        nn_layer_t *layer = &network->layers[i];
        
        // Update weights
        for (j = 0; j < layer->output_size; j++) {
            for (k = 0; k < layer->input_size; k++) {
                int weight_idx = j * layer->input_size + k;
                layer->weights[weight_idx] -= network->learning_rate * layer->weight_momentum[weight_idx];
            }
            
            // Update bias
            layer->bias[j] -= network->learning_rate * layer->bias_momentum[j];
        }
    }
    
    network->batch_count++;
}

/*
==================
NN_InitializeWeights
==================
*/
void NN_InitializeWeights(nn_layer_t *layer, float scale) {
    int i;
    int total_weights = layer->input_size * layer->output_size;
    
    // He initialization for ReLU variants
    for (i = 0; i < total_weights; i++) {
        layer->weights[i] = (((float)rand() / RAND_MAX) * 2.0f - 1.0f) * scale;
    }
    
    // Initialize biases to small positive values for ReLU
    for (i = 0; i < layer->output_size; i++) {
        layer->bias[i] = 0.01f;
    }
}

/*
==================
Activation Functions
==================
*/
float NN_ReLU(float x) {
    return x > 0 ? x : 0;
}

float NN_ReLUDerivative(float x) {
    return x > 0 ? 1.0f : 0.0f;
}

float NN_Tanh(float x) {
    return tanhf(x);
}

float NN_TanhDerivative(float x) {
    float t = tanhf(x);
    return 1.0f - t * t;
}

float NN_Sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

float NN_SigmoidDerivative(float x) {
    float s = NN_Sigmoid(x);
    return s * (1.0f - s);
}

float NN_LeakyReLU(float x, float alpha) {
    return x > 0 ? x : alpha * x;
}

float NN_LeakyReLUDerivative(float x, float alpha) {
    return x > 0 ? 1.0f : alpha;
}

void NN_Softmax(float *input, float *output, int size) {
    int i;
    float max_val = input[0];
    float sum = 0;
    
    // Find max for numerical stability
    for (i = 1; i < size; i++) {
        if (input[i] > max_val) {
            max_val = input[i];
        }
    }
    
    // Compute exp and sum
    for (i = 0; i < size; i++) {
        output[i] = expf(input[i] - max_val);
        sum += output[i];
    }
    
    // Normalize
    for (i = 0; i < size; i++) {
        output[i] /= sum;
    }
}

/*
==================
NN_ClipGradients
==================
*/
void NN_ClipGradients(nn_network_t *network, float max_norm) {
    int i, j;
    float total_norm = 0;
    
    // Compute total gradient norm
    for (i = 0; i < network->num_layers; i++) {
        nn_layer_t *layer = &network->layers[i];
        
        for (j = 0; j < layer->output_size; j++) {
            total_norm += layer->gradients[j] * layer->gradients[j];
        }
    }
    
    total_norm = sqrtf(total_norm);
    
    // Clip if necessary
    if (total_norm > max_norm) {
        float scale = max_norm / total_norm;
        
        for (i = 0; i < network->num_layers; i++) {
            nn_layer_t *layer = &network->layers[i];
            
            for (j = 0; j < layer->output_size; j++) {
                layer->gradients[j] *= scale;
            }
        }
    }
}

/*
==================
NN_ApplyDropout
==================
*/
void NN_ApplyDropout(nn_layer_t *layer, float rate) {
    int i;
    float scale = 1.0f / (1.0f - rate);
    
    for (i = 0; i < layer->output_size; i++) {
        if ((float)rand() / RAND_MAX < rate) {
            layer->output[i] = 0;
        } else {
            layer->output[i] *= scale;
        }
    }
}

/*
==================
NN_BatchNormForward
==================
*/
void NN_BatchNormForward(nn_layer_t *layer, qboolean training) {
    int i;
    float mean = 0, variance = 0;
    float epsilon = 1e-5f;
    
    if (!layer->use_batch_norm) {
        return;
    }
    
    // Compute mean
    for (i = 0; i < layer->output_size; i++) {
        mean += layer->output[i];
    }
    mean /= layer->output_size;
    
    // Compute variance
    for (i = 0; i < layer->output_size; i++) {
        float diff = layer->output[i] - mean;
        variance += diff * diff;
    }
    variance /= layer->output_size;
    
    // Update running statistics if training
    if (training) {
        float momentum = 0.9f;
        for (i = 0; i < layer->output_size; i++) {
            layer->running_mean[i] = momentum * layer->running_mean[i] + (1 - momentum) * mean;
            layer->running_variance[i] = momentum * layer->running_variance[i] + (1 - momentum) * variance;
        }
    } else {
        // Use running statistics during inference
        mean = layer->running_mean[0];
        variance = layer->running_variance[0];
    }
    
    // Normalize and scale
    float inv_std = 1.0f / sqrtf(variance + epsilon);
    for (i = 0; i < layer->output_size; i++) {
        float normalized = (layer->output[i] - mean) * inv_std;
        layer->output[i] = layer->batch_norm_gamma[i] * normalized + layer->batch_norm_beta[i];
    }
}

/*
==================
NN_SaveNetwork
==================
*/
void NN_SaveNetwork(nn_network_t *network, const char *filename) {
    fileHandle_t f;
    int i, j;
    
    if (!network || !filename) {
        return;
    }
    
    FS_FOpenFileWrite(filename, &f);
    if (!f) {
        Com_Printf("Failed to save network to %s\n", filename);
        return;
    }
    
    // Write header
    FS_Write(&network->type, sizeof(network->type), f);
    FS_Write(&network->num_layers, sizeof(network->num_layers), f);
    FS_Write(&network->input_size, sizeof(network->input_size), f);
    FS_Write(&network->output_size, sizeof(network->output_size), f);
    
    // Write layer data
    for (i = 0; i < network->num_layers; i++) {
        nn_layer_t *layer = &network->layers[i];
        
        FS_Write(&layer->input_size, sizeof(layer->input_size), f);
        FS_Write(&layer->output_size, sizeof(layer->output_size), f);
        FS_Write(&layer->activation, sizeof(layer->activation), f);
        FS_Write(&layer->use_batch_norm, sizeof(layer->use_batch_norm), f);
        
        int weight_count = layer->input_size * layer->output_size;
        FS_Write(layer->weights, weight_count * sizeof(float), f);
        FS_Write(layer->bias, layer->output_size * sizeof(float), f);
        
        if (layer->use_batch_norm) {
            FS_Write(layer->batch_norm_gamma, layer->output_size * sizeof(float), f);
            FS_Write(layer->batch_norm_beta, layer->output_size * sizeof(float), f);
            FS_Write(layer->running_mean, layer->output_size * sizeof(float), f);
            FS_Write(layer->running_variance, layer->output_size * sizeof(float), f);
        }
    }
    
    FS_FCloseFile(f);
    Com_Printf("Network saved to %s\n", filename);
}

/*
==================
NN_LoadNetwork
==================
*/
nn_network_t *NN_LoadNetwork(const char *filename) {
    fileHandle_t f;
    nn_network_t *network;
    int i, layer_sizes[NN_MAX_LAYERS + 1];
    nn_type_t type;
    int num_layers, input_size, output_size;
    
    FS_FOpenFileRead(filename, &f, qfalse);
    if (!f) {
        Com_Printf("Failed to load network from %s\n", filename);
        return NULL;
    }
    
    // Read header
    FS_Read(&type, sizeof(type), f);
    FS_Read(&num_layers, sizeof(num_layers), f);
    FS_Read(&input_size, sizeof(input_size), f);
    FS_Read(&output_size, sizeof(output_size), f);
    
    // Reconstruct layer sizes
    layer_sizes[0] = input_size;
    
    // Read layer data to get sizes
    for (i = 0; i < num_layers; i++) {
        int in_size, out_size;
        FS_Read(&in_size, sizeof(in_size), f);
        FS_Read(&out_size, sizeof(out_size), f);
        layer_sizes[i + 1] = out_size;
        
        // Skip the rest for now
        FS_Seek(f, sizeof(nn_activation_t) + sizeof(qboolean), FS_SEEK_CUR);
        FS_Seek(f, (in_size * out_size + out_size) * sizeof(float), FS_SEEK_CUR);
    }
    
    // Create network
    network = NN_CreateNetwork(type, layer_sizes, num_layers + 1);
    
    // Rewind and skip header
    FS_Seek(f, sizeof(nn_type_t) + 3 * sizeof(int), FS_SEEK_SET);
    
    // Load layer data
    for (i = 0; i < network->num_layers; i++) {
        nn_layer_t *layer = &network->layers[i];
        
        FS_Read(&layer->input_size, sizeof(layer->input_size), f);
        FS_Read(&layer->output_size, sizeof(layer->output_size), f);
        FS_Read(&layer->activation, sizeof(layer->activation), f);
        FS_Read(&layer->use_batch_norm, sizeof(layer->use_batch_norm), f);
        
        int weight_count = layer->input_size * layer->output_size;
        FS_Read(layer->weights, weight_count * sizeof(float), f);
        FS_Read(layer->bias, layer->output_size * sizeof(float), f);
        
        if (layer->use_batch_norm) {
            FS_Read(layer->batch_norm_gamma, layer->output_size * sizeof(float), f);
            FS_Read(layer->batch_norm_beta, layer->output_size * sizeof(float), f);
            FS_Read(layer->running_mean, layer->output_size * sizeof(float), f);
            FS_Read(layer->running_variance, layer->output_size * sizeof(float), f);
        }
    }
    
    FS_FCloseFile(f);
    Com_Printf("Network loaded from %s\n", filename);
    
    return network;
}

/*
==================
GPU Acceleration Stubs
==================
*/
qboolean NN_InitGPU(void) {
    // TODO: Implement Vulkan compute shader support
    return qfalse;
}

void NN_ShutdownGPU(void) {
    // TODO: Cleanup GPU resources
}

void NN_ForwardGPU(nn_network_t *network, const float *input, float *output) {
    // Fallback to CPU
    NN_Forward(network, input, output);
}

void NN_BackwardGPU(nn_network_t *network, const float *target, float *loss) {
    // Fallback to CPU
    NN_Backward(network, target, loss);
}

/*
==================
SIMD Optimizations
==================
*/
#ifdef _WIN32
void NN_VectorAdd_SSE(const float *a, const float *b, float *result, int size) {
    int i;
    int simd_size = size - (size % 4);
    
    for (i = 0; i < simd_size; i += 4) {
        __m128 va = _mm_load_ps(&a[i]);
        __m128 vb = _mm_load_ps(&b[i]);
        __m128 vr = _mm_add_ps(va, vb);
        _mm_store_ps(&result[i], vr);
    }
    
    // Handle remaining elements
    for (i = simd_size; i < size; i++) {
        result[i] = a[i] + b[i];
    }
}

void NN_VectorMul_SSE(const float *a, const float *b, float *result, int size) {
    int i;
    int simd_size = size - (size % 4);
    
    for (i = 0; i < simd_size; i += 4) {
        __m128 va = _mm_load_ps(&a[i]);
        __m128 vb = _mm_load_ps(&b[i]);
        __m128 vr = _mm_mul_ps(va, vb);
        _mm_store_ps(&result[i], vr);
    }
    
    // Handle remaining elements
    for (i = simd_size; i < size; i++) {
        result[i] = a[i] * b[i];
    }
}
#else
void NN_VectorAdd_SSE(const float *a, const float *b, float *result, int size) {
    int i;
    for (i = 0; i < size; i++) {
        result[i] = a[i] + b[i];
    }
}

void NN_VectorMul_SSE(const float *a, const float *b, float *result, int size) {
    int i;
    for (i = 0; i < size; i++) {
        result[i] = a[i] * b[i];
    }
}
#endif

