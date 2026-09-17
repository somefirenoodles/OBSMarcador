#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SCOREBOARD_INNINGS 6
#define SCOREBOARD_HISTORY 64

enum scoreboard_action {
	SCOREBOARD_BALL,
	SCOREBOARD_STRIKE,
	SCOREBOARD_OUT,
	SCOREBOARD_AWAY_RUN,
	SCOREBOARD_HOME_RUN,
	SCOREBOARD_NEXT_HALF,
	SCOREBOARD_RESET_COUNT,
	SCOREBOARD_AWAY_RUN_REMOVE,
	SCOREBOARD_HOME_RUN_REMOVE,
	SCOREBOARD_PREVIOUS_HALF,
};

struct scoreboard_state {
	unsigned char balls;
	unsigned char strikes;
	unsigned char outs;
	unsigned char inning;
	bool bottom;
	unsigned short runs[2][SCOREBOARD_INNINGS];
};

struct scoreboard {
	struct scoreboard_state state;
	struct scoreboard_state history[SCOREBOARD_HISTORY];
	size_t history_count;
};

void scoreboard_init(struct scoreboard *board);
bool scoreboard_apply(struct scoreboard *board, enum scoreboard_action action);
bool scoreboard_undo(struct scoreboard *board);
unsigned scoreboard_total(const struct scoreboard_state *state, unsigned team);
