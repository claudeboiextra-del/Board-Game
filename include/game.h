#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PLAYERS     4
#define MAX_NODES       64
#define MAX_ROUNDS_OPTS 4
#define BOARD_COUNT     2
#define MAX_ITEMS       2
#define MAX_FX          24
#define MG_COUNT        3

typedef enum {
    SPACE_NORMAL = 0,
    SPACE_PLUS_COIN,
    SPACE_MINUS_COIN,
    SPACE_EVENT,
    SPACE_SHOP,
    SPACE_STAR,
    SPACE_HIDDEN_COIN,
    SPACE_HIDDEN_STAR
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
    STATE_MG_INTRO,       /* transition card */
    STATE_MG_PLAY,        /* active minigame */
    STATE_MG_RESULT,      /* rankings + coin payout */
    STATE_RESULTS,
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
    ITEM_SHIELD
} ItemType;

typedef enum {
    MG_MASH = 0,          /* mash Cross to fill meter */
    MG_TIMING,            /* stop marker in green zone */
    MG_REACTION           /* wait for GO, press Cross */
} MiniGameType;

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
    ItemType items[MAX_ITEMS];
    int  item_count;
    bool shielded;
    int  team;
    /* minigame score this round (higher = better) */
    int  mg_score;
    int  mg_rank;         /* 0 = best */
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
    int        board_idx;
    Board     *board;
    Player     players[MAX_PLAYERS];
    int        player_count;
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

    FxSpark    fx[MAX_FX];

    /* minigame runtime */
    MiniGameType mg_type;
    int          mg_timer;        /* frames remaining / elapsed */
    float        mg_meter;        /* 0..1 mash fill or marker pos */
    float        mg_marker;       /* timing marker 0..1 */
    float        mg_marker_dir;   /* +1 / -1 */
    int          mg_go_at;        /* reaction: frame when GO appears */
    int          mg_pressed;      /* human already pressed */
    int          mg_human_score;
    bool         mg_awarded;
} GameContext;

void game_init(GameContext *g);
void game_update(GameContext *g, uint32_t pad_buttons);
void game_render(GameContext *g);
void game_shutdown(GameContext *g);

void board_load_visual(Board *b);
void board_load_geo(Board *b);

#endif /* GAME_H */
