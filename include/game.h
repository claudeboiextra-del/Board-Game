#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PLAYERS     4
#define MAX_NODES       64
#define MAX_ROUNDS_OPTS 4
#define BOARD_COUNT     2

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
    STATE_DICE_ROLL,
    STATE_MOVE,
    STATE_BRANCH,         /* human picks path at fork */
    STATE_LAND,
    STATE_SHOP,
    STATE_ROUND_END,
    STATE_MINIGAME_SIM,
    STATE_RESULTS,
    STATE_EXIT
} GameState;

typedef enum {
    DIFF_EASY = 0,
    DIFF_NORMAL,
    DIFF_HARD
} CpuDiff;

typedef struct {
    int  id;
    bool is_human;
    CpuDiff diff;
    int  coins;
    int  stars;
    int  node;
    int  color;
    bool celebrating;
    /* smooth display position (lerped toward node) */
    float disp_x, disp_y;
    float hop_t;          /* 0..1 during a hop */
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

/* short on-screen popup (coins gained/lost, star, etc.) */
typedef struct {
    int   active;
    float x, y;
    int   value;          /* +coins or flag */
    int   kind;           /* 0=coin, 1=star, 2=bad, 3=shop, 4=event */
    float life;           /* counts down */
} Popup;

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
    int        move_remaining;
    uint32_t   frame;
    float      anim_t;
    bool       star_relocated_this_round;

    /* pacing / juice */
    int        state_timer;       /* frames spent in current state */
    int        hop_cooldown;      /* frames between hops */
    int        branch_choice;     /* selected fork index */
    int        land_flash;        /* frames of land highlight */
    int        last_land_type;
    Popup      popup;
    int        contest_rewards[MAX_PLAYERS];
} GameContext;

void game_init(GameContext *g);
void game_update(GameContext *g, uint32_t pad_buttons);
void game_render(GameContext *g);
void game_shutdown(GameContext *g);

void board_load_visual(Board *b);
void board_load_geo(Board *b);

#endif /* GAME_H */
