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
    STATE_SETUP,          /* player count / CPU / rounds */
    STATE_BOARD_SELECT,
    STATE_MATCH,
    STATE_TURN_START,
    STATE_DICE_ROLL,
    STATE_MOVE,
    STATE_LAND,
    STATE_SHOP,
    STATE_ROUND_END,
    STATE_MINIGAME_SIM,   /* RNG substitute */
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
    int  node;            /* current board node index */
    int  color;           /* 0-3 visual token color */
    bool celebrating;     /* high roll animation flag */
} Player;

typedef struct {
    int        id;
    SpaceType  type;
    int        next[4];   /* adjacency (up to 4 outgoing) */
    int        next_count;
    float      x, y;      /* screen position for Tiny3D */
    bool       hidden_revealed;
} BoardNode;

typedef struct {
    const char *name;
    int         node_count;
    BoardNode   nodes[MAX_NODES];
    int         start_node;
    int         star_node;    /* current star location */
} Board;

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
    float      anim_t;        /* simple animation timer */
    bool       star_relocated_this_round;
} GameContext;

void game_init(GameContext *g);
void game_update(GameContext *g, uint32_t pad_buttons);
void game_render(GameContext *g);
void game_shutdown(GameContext *g);

/* Board loaders */
void board_load_visual(Board *b);
void board_load_geo(Board *b);

#endif /* GAME_H */
