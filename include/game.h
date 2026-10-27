#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PLAYERS     4
#define MAX_NODES       64
#define MAX_ROUNDS_OPTS 4
#define BOARD_COUNT     2
#define MAX_ITEMS       2
#define MAX_FX          32
#define MG_TYPE_COUNT   7

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
    SPACE_HAZARD
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
    MG_ZONE
} MiniGameType;

/* setup rows: 0=players 1=humans 2=team 3=diff 4=rounds */
#define SETUP_ROWS 5

typedef struct {
    int  id;
    bool is_human;
    CpuDiff diff;
    int  coins;
    int  stars;
    int  node;
    int  color;
    bool celebrating;
    float disp_x, disp_y;
    float hop_t;
    float shadow_s;
    ItemType items[MAX_ITEMS];
    int  item_count;
    bool shielded;
    int  team;
    int  mg_score;
    int  mg_rank;
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
    GameState  state;
    GameState  pause_from;   /* resume target */
    int        board_idx;
    Board     *board;
    Player     players[MAX_PLAYERS];
    int        player_count;
    int        human_count;  /* 1..player_count */
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

    bool       team_mode;
    bool       double_dice;
    int        item_cursor;
    int        boost_steps;
    int        setup_cursor;
    CpuDiff    cpu_diff;

    FxSpark    fx[MAX_FX];

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
} GameContext;

void game_init(GameContext *g);
void game_update(GameContext *g, uint32_t pad_buttons);
void game_render(GameContext *g);
void game_shutdown(GameContext *g);

void board_load_visual(Board *b);
void board_load_geo(Board *b);

#endif /* GAME_H */
