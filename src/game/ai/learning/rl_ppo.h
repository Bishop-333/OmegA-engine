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

#ifndef RL_PPO_H
#define RL_PPO_H

#include "../../../engine/common/q_shared.h"

// Weapon constants from q_shared.h
#ifndef MAX_WEAPONS
#define MAX_WEAPONS 16
#endif
#include "../neural/nn_core.h"

#define PPO_MAX_TRAJECTORY_LENGTH 2048
#define PPO_BATCH_SIZE 64
#define PPO_EPOCHS 4
#define PPO_CLIP_RATIO 0.2f
#define PPO_GAE_LAMBDA 0.95f
#define PPO_DISCOUNT_FACTOR 0.99f
#define PPO_ENTROPY_COEF 0.01f
#define PPO_VALUE_COEF 0.5f
#define PPO_MAX_GRADIENT_NORM 0.5f

typedef struct rl_state_s {
    float *features;
    int feature_size;
    vec3_t position;
    vec3_t velocity;
    float health;
    float armor;
    int weapon;
    int ammo[MAX_WEAPONS];
    int enemy_visible;
    float enemy_distance;
    vec3_t enemy_position;
    int team_score;
    int enemy_score;
    float time;
} rl_state_t;

typedef struct rl_action_s {
    float move_forward;
    float move_right;
    float move_up;
    float aim_pitch;
    float aim_yaw;
    int attack;
    int jump;
    int crouch;
    int weapon_switch;
    int use_item;
    float *probabilities;
    int action_size;
} rl_action_t;

typedef struct rl_reward_s {
    float immediate;
    float health_change;
    float damage_dealt;
    float damage_received;
    float kill_reward;
    float death_penalty;
    float objective_progress;
    float exploration_bonus;
    float team_cooperation;
    float tactical_positioning;
    float resource_efficiency;
    float total;
} rl_reward_t;

typedef struct rl_experience_s {
    rl_state_t state;
    rl_action_t action;
    rl_reward_t reward;
    rl_state_t next_state;
    qboolean done;
    float value;
    float log_prob;
    float advantage;
    float returns;
} rl_experience_t;

typedef struct rl_trajectory_s {
    rl_experience_t experiences[PPO_MAX_TRAJECTORY_LENGTH];
    int length;
    int current_idx;
    float total_reward;
    float average_value;
} rl_trajectory_t;

typedef struct ppo_agent_s {
    nn_network_t *actor_network;
    nn_network_t *critic_network;
    nn_network_t *target_critic;
    
    rl_trajectory_t trajectory;
    rl_experience_t *replay_buffer;
    int replay_buffer_size;
    int replay_buffer_capacity;
    
    float learning_rate_actor;
    float learning_rate_critic;
    float clip_ratio;
    float gae_lambda;
    float discount_factor;
    float entropy_coefficient;
    float value_coefficient;
    
    int total_steps;
    int episode_count;
    float episode_rewards[1000];
    float moving_average_reward;
    
    qboolean training_enabled;
    int update_frequency;
    int updates_performed;
    
    // Performance metrics
    float average_episode_length;
    float win_rate;
    float kill_death_ratio;
    float objective_completion_rate;
} ppo_agent_t;

// Core PPO functions
void PPO_Init(void);
void PPO_Shutdown(void);
ppo_agent_t *PPO_CreateAgent(int state_size, int action_size);
void PPO_DestroyAgent(ppo_agent_t *agent);

// State and action processing
void PPO_ObserveState(ppo_agent_t *agent, const rl_state_t *state);
void PPO_SelectAction(ppo_agent_t *agent, const rl_state_t *state, rl_action_t *action);
void PPO_StoreExperience(ppo_agent_t *agent, const rl_experience_t *exp);

// Training functions
void PPO_ComputeRewards(ppo_agent_t *agent, rl_reward_t *reward);
void PPO_ComputeAdvantages(ppo_agent_t *agent);
void PPO_UpdatePolicy(ppo_agent_t *agent);
void PPO_UpdateCritic(ppo_agent_t *agent);
void PPO_Train(ppo_agent_t *agent, int num_epochs);

// Utility functions
float PPO_ComputeGAE(rl_trajectory_t *trajectory, float lambda, float gamma);
void PPO_NormalizeAdvantages(float *advantages, int size);
float PPO_ComputePolicyLoss(ppo_agent_t *agent, const rl_experience_t *batch, int batch_size);
float PPO_ComputeValueLoss(ppo_agent_t *agent, const rl_experience_t *batch, int batch_size);
float PPO_ComputeEntropyBonus(const float *probabilities, int size);

// Exploration strategies
void PPO_EpsilonGreedy(float *probabilities, int size, float epsilon);
void PPO_BoltzmannExploration(float *logits, float *probabilities, int size, float temperature);
void PPO_AddNoiseToAction(rl_action_t *action, float noise_scale);

// Save/Load functionality
void PPO_SaveAgent(ppo_agent_t *agent, const char *filename);
ppo_agent_t *PPO_LoadAgent(const char *filename);

// Reward shaping functions
float PPO_ShapeHealthReward(float health_change, float current_health);
float PPO_ShapeCombatReward(float damage_dealt, float damage_received, int kills, int deaths);
float PPO_ShapeObjectiveReward(float progress, qboolean completed);
float PPO_ShapeExplorationReward(const vec3_t position, float *visited_map);
float PPO_ShapeTeamReward(float team_score_change, float cooperation_metric);

// Curriculum learning
void PPO_UpdateDifficulty(ppo_agent_t *agent, float performance_metric);
void PPO_SetCurriculumStage(ppo_agent_t *agent, int stage);
float PPO_GetAdaptiveLearningRate(ppo_agent_t *agent);

// Multi-agent coordination
void PPO_ShareExperience(ppo_agent_t *agent1, ppo_agent_t *agent2);
void PPO_CentralizedTraining(ppo_agent_t **agents, int num_agents);
void PPO_DecentralizedExecution(ppo_agent_t *agent, const rl_state_t *local_state);

#endif // RL_PPO_H


