#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PLAYERS     4
#define PAD_COUNT       4
#define MAX_NODES       96
#define MAX_ROUNDS_OPTS 4
#define BOARD_COUNT     3
#define MAX_ITEMS       2
#define MAX_FX          32
#define MG_TYPE_COUNT   11

typedef enum {
    SPACE_NORMAL = 0,
    SPACE_PLUS_COIN,
    SPACE_MINUS_COIN,
    SPACE_EVENT,
    SPACE_SHOP,
    SPACE_STAR,
    SPACE_HIDDEN_COIN,
    SPACE_HIDDEN_STAR,
    SPACE_WARP,
    SPACE_HAZARD,
    SPACE_BOWSER
} SpaceType;

typedef enum {
    STATE_BOOT = 0,
    STATE_TITLE,
    STATE_SETUP,
    STATE_BOARD_SELECT,
    STATE_MATCH,
    STATE_TURN_START,
    STATE_ITEM,
    STATE_DICE_ROLL,
    STATE_MOVE,
    STATE_BRANCH,
    STATE_LAND,
    STATE_SHOP,
    STATE_ROUND_END,
    STATE_MG_INTRO,
    STATE_MG_PLAY,
    STATE_MG_RESULT,
    STATE_RESULTS,
    STATE_PAUSE,
    STATE_EXIT
} GameState;

typedef enum {
    DIFF_EASY = 0,
    DIFF_NORMAL,
    DIFF_HARD
} CpuDiff;

typedef enum {
    ITEM_NONE = 0,
    ITEM_DOUBLE_DICE,
    ITEM_BOOST,
    ITEM_STEAL,
    ITEM_SHIELD,
    ITEM_SWAP
} ItemType;

typedef enum {
    MG_MASH = 0,
    MG_TIMING,
    MG_REACTION,
    MG_HOLD,
    MG_COUNT,
    MG_AVOID,
    MG_ZONE,
    MG_RHYTHM,
    MG_CATCH,
    MG_SIMON,
    MG_HOPPER
} MiniGameType;

/* MG_INTRO sub-phases */
#define INTRO_READY   0   /* ready-up screen (all players)            */
#define INTRO_COUNT   1   /* 3-2-1-GO countdown                       */
#define INTRO_GATE    2   /* "PLAYER N - GET READY" (turn-based only) */

#define SETUP_ROWS 5

/* minigame stage: x position of player slot i when n players are on stage */
#define STAGE_SLOT_X(i, n) (((float)(i) - ((float)(n) - 1.0f) * 0.5f) * 6.4f)

/* World units: board X = (node.x-0.5)*WORLD_SX, Z = (node.y-0.5)*WORLD_SZ */
#define WORLD_SX 56.0f
#define WORLD_SZ 32.0f

typedef struct {
    int  id;
    bool is_human;
    CpuDiff diff;
    int  coins;
    int  stars;
    int  node;
    int  color;
    bool celebrating;
    float disp_x, disp_y;   /* world X / world Z */
    float hop_t;
    float shadow_s;
    ItemType items[MAX_ITEMS];
    int  item_count;
    bool shielded;
    int  team;
    int  mg_score;
    int  mg_rank;
    int  pad_id;
} Player;

typedef struct {
    int        id;
    SpaceType  type;
    int        next[4];
    int        next_count;
    float      x, y;
    bool       hidden_revealed;
} BoardNode;

typedef struct {
    const char *name;
    int         node_count;
    BoardNode   nodes[MAX_NODES];
    int         start_node;
    int         star_node;
} Board;

/* popup / fx positions are SCREEN pixels (projected at spawn time) */
typedef struct {
    int   active;
    float x, y;
    int   value;
    int   kind;
    float life;
} Popup;

typedef struct {
    int   active;
    float x, y;
    float vx, vy;
    float life;
    uint32_t color;
} FxSpark;

typedef struct {
    const char *name;
    const char *goal;
    const char *rule;
    const char *controls;
    bool        simul;
} MgInfo;

typedef struct {
    GameState  state;
    GameState  pause_from;
    int        board_idx;
    Board     *board;
    Player     players[MAX_PLAYERS];
    int        player_count;
    int        human_count;
    int        current_player;
    int        round;
    int        max_rounds;
    int        dice_value;
    int        dice_value2;
    int        move_remaining;
    uint32_t   frame;
    float      anim_t;
    bool       star_relocated_this_round;

    int        state_timer;
    int        hop_cooldown;
    int        branch_choice;
    int        land_flash;
    int        last_land_type;
    Popup      popup;
    int        contest_rewards[MAX_PLAYERS];
    ItemType   shop_offer[3];       /* items for sale on this shop visit (ITEM_NONE = sold) */
    int        shop_cost[3];
    int        shop_star_cost;

    bool       team_mode;
    bool       double_dice;
    int        item_cursor;
    int        boost_steps;
    int        setup_cursor;
    CpuDiff    cpu_diff;

    FxSpark    fx[MAX_FX];

    /* camera (written by game.c, read-only for the renderer) */
    float      cam_pos[3];
    float      cam_look[3];
    float      cam_fov;       /* vertical FOV, degrees */

    MiniGameType mg_type;
    int          mg_timer;
    float        mg_meter;
    float        mg_marker;
    float        mg_marker_dir;
    int          mg_go_at;
    int          mg_pressed;
    int          mg_human_score;
    bool         mg_awarded;
    int          mg_hold_frames;
    int          mg_target;
    int          mg_flash_count;
    int          mg_next_flash;
    int          mg_phase;
    float        mg_zone_c;
    float        mg_zone_w;
    int          mg_zone_hits;
    int          mg_beats_hit;
    int          mg_beats_total;
    float        mg_fall_y;
    float        mg_fall_x;
    int          mg_actor;
    bool         mg_simul;
    float        mg_p_meter[MAX_PLAYERS];
    int          mg_p_latched[MAX_PLAYERS];
    uint32_t     mg_prompt;
    int          mg_safe;

    /* extra minigame state */
    int          mg_phase_start;     /* mg_timer when the input phase began */
    int          mg_misses;
    int          mg_seq[8];
    int          mg_seq_len;
    int          mg_input_idx;
    int          mg_beat_mask;       /* rhythm: beats already hit        */
    int          mg_hop_swap;        /* hopper: next light change frame  */
    int          mg_time_max;        /* frames for the time bar (0 = none) */
    int          mg_locked;          /* turn-based: result locked in     */
    int          mg_lock_t;
    int          mg_hop_flash[MAX_PLAYERS]; /* hopper: fail flash timers */
    int          mg_last_press[MAX_PLAYERS];/* frames since last good press */

    /* minigame intro / ready-up */
    int          mg_intro_phase;
    int          mg_intro_t;         /* frames spent in the current phase */
    bool         mg_ready[MAX_PLAYERS];
    int          mg_gate_player;
} GameContext;

void game_init(GameContext *g);
void game_update(GameContext *g, const uint32_t pad_pressed[PAD_COUNT],
                 const uint32_t pad_held[PAD_COUNT]);
void game_render(GameContext *g);
void game_shutdown(GameContext *g);

void board_load_visual(Board *b);
void board_load_geo(Board *b);
void board_load_metro(Board *b);

/* minigame descriptions (name / goal / rule / controls / simultaneous) */
const MgInfo *mg_info(MiniGameType t);

/* Project a world point to screen pixels with the camera stored in g.
   Returns 1 if the point is in front of the camera, 0 otherwise. */
int render_project(const GameContext *g, float x, float y, float z,
                   float *sx, float *sy);

#endif /* GAME_H */
