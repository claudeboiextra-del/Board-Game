                            if (g->mg_p_meter[hi] < 0) g->mg_p_meter[hi] = 0;
                            sfx_play(SFX_FAIL);
                        }
                    }
                }
            }
            if (g->mg_timer >= 140) {
                for (hi = 0; hi < g->player_count; ++hi)
                    if (g->players[hi].is_human)
                        g->players[hi].mg_score = (int)(g->mg_p_meter[hi] * 100);
                g->mg_actor = g->player_count;
                mg_finish_play(g);
            }
        } break;
        case MG_REACTION:
        default:
            if (buttons & 0x10) {
                if (g->mg_timer < g->mg_go_at) {
                    g->mg_human_score = 5; g->mg_pressed = 1; sfx_play(SFX_FAIL); mg_finish_play(g);
                } else if (!g->mg_pressed) {
                    int delay = g->mg_timer - g->mg_go_at;
                    g->mg_human_score = 100 - delay * 2;
                    if (g->mg_human_score < 10) g->mg_human_score = 10;
                    g->mg_pressed = 1; spawn_fx(g, 424, 240, 0xFF2ECC71, 10); sfx_play(SFX_COIN);
                    mg_finish_play(g);
                }
            } else if (g->mg_timer >= MG_REACT_MAX) { g->mg_human_score = 8; mg_finish_play(g); }
            break;
        } break;
    case STATE_MG_RESULT:
        if (g->state_timer == 1) mg_rank_and_pay(g);
        if (g->state_timer >= MG_RESULT_HOLD || (buttons & 0x10))
            { g->state = STATE_TURN_START; g->state_timer = 0; }
        break;
    case STATE_PAUSE:
        if (buttons & 0x10) { g->state = g->pause_from; sfx_play(SFX_CLICK); }
        if (buttons & 0x80) { sfx_play(SFX_CLICK); g->state = STATE_TITLE; g->state_timer = 0; }
        break;
    case STATE_RESULTS:
        if (buttons & 0x10) {
            int i; sfx_play(SFX_CLICK);
            g->state = STATE_TITLE; g->state_timer = 0; g->round = 0; g->current_player = 0;
            for (i = 0; i < MAX_PLAYERS; ++i) {
                g->players[i].celebrating = false; g->players[i].coins = 12;
                g->players[i].stars = 0; g->players[i].item_count = 0; g->players[i].shielded = false;
            }
        }
        if (buttons & 0x100) { sfx_play(SFX_CLICK); g->state = STATE_EXIT; }
        break;
    default: break;
    }

    music_sync(g);
}
void game_shutdown(GameContext *g) { LOG_I("game_shutdown"); (void)g; }
