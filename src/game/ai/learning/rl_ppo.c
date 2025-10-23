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

#include "rl_ppo.h"
#include "../../../engine/core/qcommon.h"
#include "../game_interface.h"
#include "../ai_system.h"
#include "../game_memory.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static struct {
    qboolean initialized;
    int total_agents;
    ppo_agent_t *agents[MAX_CLIENTS];
    float global_learning_rate;
    int global_update_counter;
} ppo_global;

/*
==================
PPO_Init
==================
*/
void PPO_Init(void) {
    if (ppo_global.initialized) {
        return;
    }
    
    memset(&ppo_global, 0, sizeof(ppo_global));
    ppo_global.initialized = qtrue;
    ppo_global.global_learning_rate = 3e-4f;
    
    // Initialize neural network system if not already done
    NN_Init();
    
    Com_Printf("PPO Reinforcement Learning System Initialized\n");
}

/*
==================
PPO_Shutdown
==================
*/
void PPO_Shutdown(void) {
    int i;
    
    if (!ppo_global.initialized) {
        return;
    }
    
    // Destroy all agents
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (ppo_global.agents[i]) {
            PPO_DestroyAgent(ppo_global.agents[i]);
        }
    }
    
    ppo_global.initialized = qfalse;
    Com_Printf("PPO Reinforcement Learning System Shutdown\n");
}

/*
==================
PPO_CreateAgent
==================
*/
ppo_agent_t *PPO_CreateAgent(int state_size, int action_size) {
    ppo_agent_t *agent;
    int actor_layers[4];
    int critic_layers[4];
    
    agent = (ppo_agent_t *)Z_Malloc(sizeof(ppo_agent_t));
    memset(agent, 0, sizeof(ppo_agent_t));
    
    // Configure actor network (policy)
    actor_layers[0] = state_size;
    actor_layers[1] = 256;
    actor_layers[2] = 128;
    actor_layers[3] = action_size;
    agent->actor_network = NN_CreateNetwork(NN_TYPE_DECISION, actor_layers, 4);
    
    // Configure critic network (value function)
    critic_layers[0] = state_size;
    critic_layers[1] = 256;
    critic_layers[2] = 128;
    critic_layers[3] = 1;  // Single value output
    agent->critic_network = NN_CreateNetwork(NN_TYPE_DECISION, critic_layers, 4);
    agent->target_critic = NN_CreateNetwork(NN_TYPE_DECISION, critic_layers, 4);
    
    // Initialize hyperparameters
    agent->learning_rate_actor = 3e-4f;
    agent->learning_rate_critic = 1e-3f;
    agent->clip_ratio = PPO_CLIP_RATIO;
    agent->gae_lambda = PPO_GAE_LAMBDA;
    agent->discount_factor = PPO_DISCOUNT_FACTOR;
    agent->entropy_coefficient = PPO_ENTROPY_COEF;
    agent->value_coefficient = PPO_VALUE_COEF;
    
    // Initialize replay buffer
    agent->replay_buffer_capacity = 10000;
    agent->replay_buffer = (rl_experience_t *)Z_Malloc(agent->replay_buffer_capacity * sizeof(rl_experience_t));
    
    // Initialize training parameters
    agent->training_enabled = qtrue;
    agent->update_frequency = 2048;
    
    // Add to global agent list
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!ppo_global.agents[i]) {
            ppo_global.agents[i] = agent;
            break;
        }
    }
    ppo_global.total_agents++;
    
    Com_Printf("Created PPO agent with state_size=%d, action_size=%d\n", state_size, action_size);
    
    return agent;
}

/*
==================
PPO_DestroyAgent
==================
*/
void PPO_DestroyAgent(ppo_agent_t *agent) {
    int i;
    
    if (!agent) {
        return;
    }
    
    // Remove from global list
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (ppo_global.agents[i] == agent) {
            ppo_global.agents[i] = NULL;
            break;
        }
    }
    
    // Destroy neural networks
    if (agent->actor_network) {
        NN_DestroyNetwork(agent->actor_network);
    }
    if (agent->critic_network) {
        NN_DestroyNetwork(agent->critic_network);
    }
    if (agent->target_critic) {
        NN_DestroyNetwork(agent->target_critic);
    }
    
    // Free replay buffer
    if (agent->replay_buffer) {
        Z_Free(agent->replay_buffer);
    }
    
    // Free trajectory experiences
    for (i = 0; i < agent->trajectory.length; i++) {
        if (agent->trajectory.experiences[i].state.features) {
            Z_Free(agent->trajectory.experiences[i].state.features);
        }
        if (agent->trajectory.experiences[i].action.probabilities) {
            Z_Free(agent->trajectory.experiences[i].action.probabilities);
        }
    }
    
    ppo_global.total_agents--;
    Z_Free(agent);
}

/*
==================
PPO_ObserveState
==================
*/
void PPO_ObserveState(ppo_agent_t *agent, const rl_state_t *state) {
    if (!agent || !state) {
        return;
    }
    
    // Store current state for decision making
    rl_experience_t *current = &agent->trajectory.experiences[agent->trajectory.current_idx];
    
    // Copy state features
    if (!current->state.features) {
        current->state.features = (float *)Z_Malloc(state->feature_size * sizeof(float));
    }
    memcpy(current->state.features, state->features, state->feature_size * sizeof(float));
    current->state.feature_size = state->feature_size;
    
    // Copy other state information
    VectorCopy(state->position, current->state.position);
    VectorCopy(state->velocity, current->state.velocity);
    current->state.health = state->health;
    current->state.armor = state->armor;
    current->state.weapon = state->weapon;
    memcpy(current->state.ammo, state->ammo, sizeof(state->ammo));
    current->state.enemy_visible = state->enemy_visible;
    current->state.enemy_distance = state->enemy_distance;
    VectorCopy(state->enemy_position, current->state.enemy_position);
    current->state.team_score = state->team_score;
    current->state.enemy_score = state->enemy_score;
    current->state.time = state->time;
}

/*
==================
PPO_SelectAction
==================
*/
void PPO_SelectAction(ppo_agent_t *agent, const rl_state_t *state, rl_action_t *action) {
    float actor_output[64];  // Max action size
    float critic_output[1];
    float max_prob;
    int selected_action;
    int i;
    
    if (!agent || !state || !action) {
        return;
    }
    
    // Forward pass through actor network
    NN_Forward(agent->actor_network, state->features, actor_output);
    
    // Forward pass through critic network for value estimation
    NN_Forward(agent->critic_network, state->features, critic_output);
    
    // Store action probabilities
    if (!action->probabilities) {
        action->probabilities = (float *)Z_Malloc(agent->actor_network->output_size * sizeof(float));
    }
    action->action_size = agent->actor_network->output_size;
    memcpy(action->probabilities, actor_output, action->action_size * sizeof(float));
    
    // Apply exploration if in training mode
    if (agent->training_enabled) {
        // Add exploration noise
        PPO_BoltzmannExploration(actor_output, action->probabilities, action->action_size, 1.0f);
    }
    
    // Sample action from probability distribution
    float random_val = (float)rand() / RAND_MAX;
    float cumulative_prob = 0;
    selected_action = 0;
    
    for (i = 0; i < action->action_size; i++) {
        cumulative_prob += action->probabilities[i];
        if (random_val <= cumulative_prob) {
            selected_action = i;
            break;
        }
    }
    
    // Decode discrete action to continuous control
    // This is a simplified mapping - in practice would be more sophisticated
    action->move_forward = (selected_action & 1) ? 1.0f : -1.0f;
    action->move_right = (selected_action & 2) ? 1.0f : -1.0f;
    action->attack = (selected_action & 4) ? 1 : 0;
    action->jump = (selected_action & 8) ? 1 : 0;
    action->crouch = (selected_action & 16) ? 1 : 0;
    action->weapon_switch = (selected_action & 32) ? 1 : 0;
    
    // For continuous actions, use separate output heads
    action->aim_pitch = actor_output[action->action_size - 2] * 180.0f;  // [-180, 180]
    action->aim_yaw = actor_output[action->action_size - 1] * 180.0f;
    
    // Store value and log probability for PPO update
    rl_experience_t *current = &agent->trajectory.experiences[agent->trajectory.current_idx];
    current->value = critic_output[0];
    current->log_prob = logf(action->probabilities[selected_action] + 1e-8f);
}

/*
==================
PPO_StoreExperience
==================
*/
void PPO_StoreExperience(ppo_agent_t *agent, const rl_experience_t *exp) {
    if (!agent || !exp) {
        return;
    }
    
    // Add to trajectory
    if (agent->trajectory.current_idx < PPO_MAX_TRAJECTORY_LENGTH) {
        rl_experience_t *stored = &agent->trajectory.experiences[agent->trajectory.current_idx];
        memcpy(stored, exp, sizeof(rl_experience_t));
        
        agent->trajectory.current_idx++;
        agent->trajectory.length = agent->trajectory.current_idx;
        agent->trajectory.total_reward += exp->reward.total;
    }
    
    // Add to replay buffer
    if (agent->replay_buffer_size < agent->replay_buffer_capacity) {
        memcpy(&agent->replay_buffer[agent->replay_buffer_size], exp, sizeof(rl_experience_t));
        agent->replay_buffer_size++;
    } else {
        // Overwrite oldest experience
        int idx = agent->total_steps % agent->replay_buffer_capacity;
        memcpy(&agent->replay_buffer[idx], exp, sizeof(rl_experience_t));
    }
    
    agent->total_steps++;
    
    // Check if we should update the policy
    if (agent->training_enabled && agent->total_steps % agent->update_frequency == 0) {
        PPO_ComputeAdvantages(agent);
        PPO_Train(agent, PPO_EPOCHS);
        
        // Reset trajectory
        agent->trajectory.current_idx = 0;
        agent->trajectory.total_reward = 0;
    }
}

/*
==================
PPO_ComputeRewards
==================
*/
void PPO_ComputeRewards(ppo_agent_t *agent, rl_reward_t *reward) {
    if (!agent || !reward) {
        return;
    }
    
    // Compute shaped rewards
    reward->total = 0;
    
    // Health reward/penalty
    reward->total += PPO_ShapeHealthReward(reward->health_change, 100.0f);
    
    // Combat rewards
    reward->total += PPO_ShapeCombatReward(reward->damage_dealt, reward->damage_received, 
                                           reward->kill_reward > 0 ? 1 : 0,
                                           reward->death_penalty < 0 ? 1 : 0);
    
    // Objective rewards
    reward->total += PPO_ShapeObjectiveReward(reward->objective_progress, qfalse);
    
    // Exploration bonus
    reward->total += reward->exploration_bonus * 0.1f;
    
    // Team cooperation
    reward->total += reward->team_cooperation * 0.5f;
    
    // Tactical positioning
    reward->total += reward->tactical_positioning * 0.3f;
    
    // Resource efficiency
    reward->total += reward->resource_efficiency * 0.2f;
    
    // Add immediate reward
    reward->total += reward->immediate;
}

/*
==================
PPO_ComputeAdvantages
==================
*/
void PPO_ComputeAdvantages(ppo_agent_t *agent) {
    int t;
    float *advantages;
    float *returns;
    float next_value = 0;
    float gae = 0;
    
    if (!agent || agent->trajectory.length == 0) {
        return;
    }
    
    advantages = (float *)Z_Malloc(agent->trajectory.length * sizeof(float));
    returns = (float *)Z_Malloc(agent->trajectory.length * sizeof(float));
    
    // Compute GAE backwards through trajectory
    for (t = agent->trajectory.length - 1; t >= 0; t--) {
        rl_experience_t *exp = &agent->trajectory.experiences[t];
        
        if (t == agent->trajectory.length - 1) {
            next_value = exp->done ? 0 : exp->value;
        } else {
            next_value = agent->trajectory.experiences[t + 1].value;
        }
        
        float delta = exp->reward.total + agent->discount_factor * next_value - exp->value;
        gae = delta + agent->discount_factor * agent->gae_lambda * gae * (1 - exp->done);
        
        advantages[t] = gae;
        returns[t] = gae + exp->value;
        
        // Store computed values
        exp->advantage = advantages[t];
        exp->returns = returns[t];
    }
    
    // Normalize advantages
    PPO_NormalizeAdvantages(advantages, agent->trajectory.length);
    
    // Update stored advantages
    for (t = 0; t < agent->trajectory.length; t++) {
        agent->trajectory.experiences[t].advantage = advantages[t];
    }
    
    Z_Free(advantages);
    Z_Free(returns);
}

/*
==================
PPO_UpdatePolicy
==================
*/
void PPO_UpdatePolicy(ppo_agent_t *agent) {
    int batch_start, batch_end, i;
    float policy_loss = 0;
    float value_loss = 0;
    float entropy_bonus = 0;
    float total_loss;
    
    if (!agent || agent->trajectory.length < PPO_BATCH_SIZE) {
        return;
    }
    
    // Process in batches
    for (batch_start = 0; batch_start < agent->trajectory.length; batch_start += PPO_BATCH_SIZE) {
        batch_end = MIN(batch_start + PPO_BATCH_SIZE, agent->trajectory.length);
        int batch_size = batch_end - batch_start;
        
        // Compute losses for this batch
        policy_loss = PPO_ComputePolicyLoss(agent, &agent->trajectory.experiences[batch_start], batch_size);
        value_loss = PPO_ComputeValueLoss(agent, &agent->trajectory.experiences[batch_start], batch_size);
        
        // Compute entropy bonus for exploration
        for (i = batch_start; i < batch_end; i++) {
            rl_experience_t *exp = &agent->trajectory.experiences[i];
            if (exp->action.probabilities) {
                entropy_bonus += PPO_ComputeEntropyBonus(exp->action.probabilities, exp->action.action_size);
            }
        }
        entropy_bonus /= batch_size;
        
        // Total loss
        total_loss = policy_loss - agent->entropy_coefficient * entropy_bonus + agent->value_coefficient * value_loss;
        
        // Backpropagate and update weights
        agent->actor_network->training_mode = qtrue;
        NN_Backward(agent->actor_network, NULL, &policy_loss);
        NN_UpdateWeights(agent->actor_network);
        
        agent->critic_network->training_mode = qtrue;
        NN_Backward(agent->critic_network, NULL, &value_loss);
        NN_UpdateWeights(agent->critic_network);
    }
    
    agent->updates_performed++;
}

/*
==================
PPO_Train
==================
*/
void PPO_Train(ppo_agent_t *agent, int num_epochs) {
    int epoch;
    
    if (!agent || !agent->training_enabled) {
        return;
    }
    
    Com_DPrintf("PPO Training: %d epochs with %d experiences\n", num_epochs, agent->trajectory.length);
    
    for (epoch = 0; epoch < num_epochs; epoch++) {
        // Update policy network
        PPO_UpdatePolicy(agent);
        
        // Update critic network
        PPO_UpdateCritic(agent);
        
        // Decay learning rate
        agent->learning_rate_actor *= 0.999f;
        agent->learning_rate_critic *= 0.999f;
        
        // Update target critic periodically
        if (agent->updates_performed % 100 == 0) {
            // Soft update: target = tau * current + (1-tau) * target
            float tau = 0.005f;
            // Copy weights with soft update (simplified - would need proper implementation)
            memcpy(agent->target_critic, agent->critic_network, sizeof(nn_network_t));
        }
    }
    
    // Update episode statistics
    if (agent->trajectory.experiences[agent->trajectory.length - 1].done) {
        agent->episode_count++;
        agent->episode_rewards[agent->episode_count % 1000] = agent->trajectory.total_reward;
        
        // Compute moving average
        float sum = 0;
        int count = MIN(agent->episode_count, 100);
        for (int i = 0; i < count; i++) {
            sum += agent->episode_rewards[(agent->episode_count - i) % 1000];
        }
        agent->moving_average_reward = sum / count;
        
        Com_DPrintf("Episode %d: Reward=%.2f, Avg=%.2f\n", 
                   agent->episode_count, agent->trajectory.total_reward, agent->moving_average_reward);
    }
}

/*
==================
PPO_UpdateCritic
==================
*/
void PPO_UpdateCritic(ppo_agent_t *agent) {
    int i;
    float value_loss = 0;
    
    if (!agent) {
        return;
    }
    
    // Compute value loss over entire trajectory
    for (i = 0; i < agent->trajectory.length; i++) {
        rl_experience_t *exp = &agent->trajectory.experiences[i];
        float value_pred[1];
        
        // Get current value prediction
        NN_Forward(agent->critic_network, exp->state.features, value_pred);
        
        // Compute TD error
        float td_error = exp->returns - value_pred[0];
        value_loss += td_error * td_error;
    }
    
    value_loss /= agent->trajectory.length;
    
    // Update critic network
    agent->critic_network->training_mode = qtrue;
    NN_Backward(agent->critic_network, NULL, &value_loss);
    NN_UpdateWeights(agent->critic_network);
}

/*
==================
PPO_ComputePolicyLoss
==================
*/
float PPO_ComputePolicyLoss(ppo_agent_t *agent, const rl_experience_t *batch, int batch_size) {
    float loss = 0;
    int i;
    
    for (i = 0; i < batch_size; i++) {
        const rl_experience_t *exp = &batch[i];
        float new_probs[64];
        
        // Get current policy probabilities
        NN_Forward(agent->actor_network, exp->state.features, new_probs);
        
        // Compute probability ratio
        float old_prob = expf(exp->log_prob);
        float new_prob = new_probs[0];  // Simplified - would need action index
        float ratio = new_prob / (old_prob + 1e-8f);
        
        // Clipped surrogate objective
        float surr1 = ratio * exp->advantage;
        float surr2 = CLAMP(ratio, 1.0f - agent->clip_ratio, 1.0f + agent->clip_ratio) * exp->advantage;
        
        loss -= MIN(surr1, surr2);
    }
    
    return loss / batch_size;
}

/*
==================
PPO_ComputeValueLoss
==================
*/
float PPO_ComputeValueLoss(ppo_agent_t *agent, const rl_experience_t *batch, int batch_size) {
    float loss = 0;
    int i;
    
    for (i = 0; i < batch_size; i++) {
        const rl_experience_t *exp = &batch[i];
        float value_pred[1];
        
        // Get current value prediction
        NN_Forward(agent->critic_network, exp->state.features, value_pred);
        
        // MSE loss
        float error = exp->returns - value_pred[0];
        loss += error * error;
    }
    
    return loss / batch_size;
}

/*
==================
PPO_ComputeEntropyBonus
==================
*/
float PPO_ComputeEntropyBonus(const float *probabilities, int size) {
    float entropy = 0;
    int i;
    
    for (i = 0; i < size; i++) {
        if (probabilities[i] > 0) {
            entropy -= probabilities[i] * logf(probabilities[i] + 1e-8f);
        }
    }
    
    return entropy;
}

/*
==================
PPO_NormalizeAdvantages
==================
*/
void PPO_NormalizeAdvantages(float *advantages, int size) {
    float mean = 0, std = 0;
    int i;
    
    // Compute mean
    for (i = 0; i < size; i++) {
        mean += advantages[i];
    }
    mean /= size;
    
    // Compute standard deviation
    for (i = 0; i < size; i++) {
        float diff = advantages[i] - mean;
        std += diff * diff;
    }
    std = sqrtf(std / size);
    
    // Normalize
    for (i = 0; i < size; i++) {
        advantages[i] = (advantages[i] - mean) / (std + 1e-8f);
    }
}

/*
==================
PPO_BoltzmannExploration
==================
*/
void PPO_BoltzmannExploration(float *logits, float *probabilities, int size, float temperature) {
    int i;
    float max_logit = logits[0];
    float sum = 0;
    
    // Find max for numerical stability
    for (i = 1; i < size; i++) {
        if (logits[i] > max_logit) {
            max_logit = logits[i];
        }
    }
    
    // Compute softmax with temperature
    for (i = 0; i < size; i++) {
        probabilities[i] = expf((logits[i] - max_logit) / temperature);
        sum += probabilities[i];
    }
    
    // Normalize
    for (i = 0; i < size; i++) {
        probabilities[i] /= sum;
    }
}

/*
==================
Reward Shaping Functions
==================
*/
float PPO_ShapeHealthReward(float health_change, float current_health) {
    // Survival bonus
    float survival_bonus = current_health > 0 ? 0.01f : -10.0f;
    
    // Health preservation
    float health_reward = health_change * 0.1f;
    
    // Critical health penalty
    if (current_health < 25 && current_health > 0) {
        health_reward -= 0.5f;
    }
    
    return health_reward + survival_bonus;
}

float PPO_ShapeCombatReward(float damage_dealt, float damage_received, int kills, int deaths) {
    float reward = 0;
    
    // Damage rewards
    reward += damage_dealt * 0.01f;
    reward -= damage_received * 0.005f;
    
    // Kill/death rewards
    reward += kills * 5.0f;
    reward -= deaths * 10.0f;
    
    // Efficiency bonus
    if (damage_dealt > damage_received * 2) {
        reward += 1.0f;
    }
    
    return reward;
}

float PPO_ShapeObjectiveReward(float progress, qboolean completed) {
    float reward = progress * 2.0f;
    
    if (completed) {
        reward += 10.0f;
    }
    
    return reward;
}

float PPO_ShapeExplorationReward(const vec3_t position, float *visited_map) {
    // Simplified exploration reward
    // In practice, would check against visited positions
    return 0.1f;
}

float PPO_ShapeTeamReward(float team_score_change, float cooperation_metric) {
    return team_score_change * 0.5f + cooperation_metric * 0.3f;
}

/*
==================
PPO_SaveAgent
==================
*/
void PPO_SaveAgent(ppo_agent_t *agent, const char *filename) {
    char actor_file[MAX_QPATH];
    char critic_file[MAX_QPATH];
    fileHandle_t f;
    
    if (!agent || !filename) {
        return;
    }
    
    // Save networks
    Com_sprintf(actor_file, sizeof(actor_file), "%s_actor.nn", filename);
    Com_sprintf(critic_file, sizeof(critic_file), "%s_critic.nn", filename);
    
    NN_SaveNetwork(agent->actor_network, actor_file);
    NN_SaveNetwork(agent->critic_network, critic_file);
    
    // Save agent metadata
    f = FS_FOpenFileWrite(filename);
    if (!f) {
        return;
    }
    
    FS_Write(&agent->total_steps, sizeof(agent->total_steps), f);
    FS_Write(&agent->episode_count, sizeof(agent->episode_count), f);
    FS_Write(&agent->moving_average_reward, sizeof(agent->moving_average_reward), f);
    FS_Write(&agent->learning_rate_actor, sizeof(agent->learning_rate_actor), f);
    FS_Write(&agent->learning_rate_critic, sizeof(agent->learning_rate_critic), f);
    
    FS_FCloseFile(f);
    
    Com_Printf("PPO agent saved to %s\n", filename);
}

/*
==================
PPO_LoadAgent
==================
*/
ppo_agent_t *PPO_LoadAgent(const char *filename) {
    char actor_file[MAX_QPATH];
    char critic_file[MAX_QPATH];
    fileHandle_t f;
    ppo_agent_t *agent;
    
    if (!filename) {
        return NULL;
    }
    
    // Load networks
    Com_sprintf(actor_file, sizeof(actor_file), "%s_actor.nn", filename);
    Com_sprintf(critic_file, sizeof(critic_file), "%s_critic.nn", filename);
    
    nn_network_t *actor = NN_LoadNetwork(actor_file);
    nn_network_t *critic = NN_LoadNetwork(critic_file);
    
    if (!actor || !critic) {
        if (actor) NN_DestroyNetwork(actor);
        if (critic) NN_DestroyNetwork(critic);
        return NULL;
    }
    
    // Create agent
    agent = (ppo_agent_t *)Z_Malloc(sizeof(ppo_agent_t));
    memset(agent, 0, sizeof(ppo_agent_t));
    
    agent->actor_network = actor;
    agent->critic_network = critic;
    
    // Load metadata
    FS_FOpenFileRead(filename, &f, qfalse);
    if (f) {
        FS_Read(&agent->total_steps, sizeof(agent->total_steps), f);
        FS_Read(&agent->episode_count, sizeof(agent->episode_count), f);
        FS_Read(&agent->moving_average_reward, sizeof(agent->moving_average_reward), f);
        FS_Read(&agent->learning_rate_actor, sizeof(agent->learning_rate_actor), f);
        FS_Read(&agent->learning_rate_critic, sizeof(agent->learning_rate_critic), f);
        FS_FCloseFile(f);
    }
    
    // Initialize other parameters
    agent->clip_ratio = PPO_CLIP_RATIO;
    agent->gae_lambda = PPO_GAE_LAMBDA;
    agent->discount_factor = PPO_DISCOUNT_FACTOR;
    agent->entropy_coefficient = PPO_ENTROPY_COEF;
    agent->value_coefficient = PPO_VALUE_COEF;
    agent->training_enabled = qfalse;  // Loaded agents start in inference mode
    
    Com_Printf("PPO agent loaded from %s\n", filename);
    
    return agent;
}

